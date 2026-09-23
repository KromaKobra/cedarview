{
  description = "myCU — Cedarville student data (chapel attendance first), desktop + Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Every Qt module whose plugins / QML imports the app needs at runtime.
        # QtWebEngine is the desktop login + transport backend; it has no Android
        # equivalent (Android uses QtWebView, which is provided by the Android
        # build of Qt, not by this devShell).
        qtModules = with pkgs.qt6; [
          qtbase
          qtdeclarative
          qtsvg
          qtwebengine
          qtwebchannel
          qtpositioning
        ];

        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          pyside6
          shiboken6

          # Desktop tooling only — `scripts/discover` uses it to pretty-print
          # captured pages, and degrades to a notice if it is missing. The app
          # itself no longer depends on it: the meal-plan page is parsed by
          # `mycu.core.minihtml` on the stdlib, so that the APK needs no
          # cross-compiled C extension. Do not reintroduce it into
          # pyproject.toml's `dependencies`.
          lxml

          # Only used by scripts/check-live and offline tooling, never by the
          # app itself — the app's only HTTP client is the WebView.
          httpx

          pytest
          pytest-cov
        ]);

        # Android packaging needs an FHS layout: pyside6-android-deploy is NOT
        # in nixpkgs, it comes from the PyPI wheels and drives buildozer /
        # python-for-android, which download their own SDK/NDK and hardcode
        # /usr/bin-style paths. Keeping this in a project-local FHS env avoids
        # enabling programs.nix-ld globally. See docs/android.md.
        androidFhs = pkgs.buildFHSEnv {
          name = "mycu-android-build";
          targetPkgs = p: with p; [
            # Python 3.11 SPECIFICALLY, and not the 3.13 the desktop devShell
            # uses. Two independent reasons, both hard failures:
            #
            #   1. pyside6-android-deploy refuses to start on anything newer
            #      ("Android deployment requires Python version 3.11 or lower.
            #      This is due to a restriction in buildozer.").
            #   2. Qt only publishes Android wheels tagged cp311
            #      (…-cp311-cp311-android_aarch64.whl), because that is the
            #      CPython python-for-android builds for the device.
            #
            # The two versions never meet: 3.11 builds the APK, 3.13 runs the
            # app and the tests on this machine.
            python311
            python311Packages.pip
            python311Packages.virtualenv
            python311Packages.setuptools
            python311Packages.wheel

            jdk17
            android-tools   # adb, for installing and reading logcat
            unzip
            zip
            which
            git
            wget
            curl
            ccache
            pkg-config

            # autoconf 2.69, NOT the current 2.73. python-for-android builds
            # libffi v3.4.2, whose configure.ac still calls the legacy
            # AC_PROG_LIBTOOL. libtool provides that only as
            # `AU_ALIAS([AC_PROG_LIBTOOL], [LT_INIT])`, and autoconf 2.73 no
            # longer expands the alias, so the token survives into configure
            # and trips the `^AC_` pattern check:
            #
            #   configure.ac:41: error: undefined or overquoted macro: AC_PROG_LIBTOOL
            #   configure.ac:418: warning: AC_PROG_LD is m4_require'd but not m4_defun'd
            #
            # which kills `autoreconf -vif` in the libffi recipe. 2.69 is the
            # autoconf libffi 3.4.2 was released against and expands it fine.
            autoconf269
            automake
            libtool
            m4            # libtoolize shells out to it: "Please install GNU M4"
            libffi
            openssl
            zlib

            # ncurses AND its headers. nixpkgs splits headers into the `dev`
            # output, and targetPkgs only brings in the default one — which
            # leaves /usr/lib/libncurses.so present but /usr/include/curses.h
            # absent. python-for-android builds a host CPython 3.11, whose
            # configure then detects curses from the library alone, enables
            # _curses and _curses_panel, and dies at compile time with a wall
            # of "unknown type name 'WINDOW'". Because those are Makefile
            # targets rather than optional setup.py modules, `make` ABORTS
            # rather than skipping them, and the whole APK build fails maybe
            # fifteen minutes in. Nothing here uses curses; the headers are
            # present purely so the host interpreter finishes building.
            ncurses
            ncurses.dev
            cmake
            ninja
            file

            # Reads the manifest out of an .aab (`bundletool dump manifest`),
            # which aapt cannot, and turns a bundle into the split APKs Play
            # would serve so it can be installed and sized locally. Used by
            # scripts/build-apk --aab.
            bundletool

            # Shared libraries for the *host* Qt tools that ship inside the
            # PySide6 PyPI wheel — `qmlimportscanner`, `qmlcachegen`, `rcc`.
            # pyside6-android-deploy shells out to them while generating
            # pysidedeploy.spec, and a manylinux wheel expects an FHS system to
            # supply these. A missing one shows up only as a bare `exit 127`
            # from a subprocess, so if the deploy tool dies that way, run the
            # binary by hand to see which `.so` it is actually asking for:
            #
            #   .venv-android/lib/python3.11/site-packages/PySide6/Qt/libexec/qmlimportscanner
            #
            # zstd was the first one missing; the rest are the usual Qt host
            # dependencies and are cheap insurance against another round trip.
            zstd
            krb5          # libgssapi_krb5.so.2, via Qt's network stack
            brotli        # libbrotlidec.so.1, likewise
            glib
            dbus
            fontconfig
            freetype
            libGL
            libxkbcommon
            libx11
            libxcb
            libxext
            libxrender
          ];
          # buildozer wants a writable HOME for its cache. Note that setting
          # this does NOT keep the downloads inside the project: buildozer
          # 1.5.0 ignores BUILDOZER_HOME for its global directory, so the SDK
          # lands in ~/.buildozer and the NDK in ~/.pyside6_android_deploy,
          # several GB between them. `git clean` will not reclaim those — see
          # docs/android.md. It is still set because scripts/build-apk uses it
          # as its "am I inside the FHS shell?" check.
          profile = ''
            export BUILDOZER_HOME="$PWD/.buildozer-home"
            export JAVA_HOME="${pkgs.jdk17}"
            # The HOST shiboken6 generator, same 6.11.0 as the Android wheels.
            # scripts/android/recipes/shiboken6 rebuilds libshiboken for 16 KB
            # pages, and the Shiboken module's wrapper source is generated by
            # this binary. Qt's Android wheel ships no generator.
            export SHIBOKEN6_HOST_PATH="${pkgs.python3Packages.shiboken6-generator}"
            echo "mycu android FHS shell. See docs/android.md for the build steps."
          '';
          runScript = "bash";
        };
      in
      {
        devShells.default = pkgs.mkShell {
          # android-tools is here as well as in the FHS env so `adb` works from
          # the ordinary devShell. Note that NO system configuration is needed
          # for it: systemd-logind already grants the seat's active user an
          # ACL on the phone's /dev/bus/usb node (`getfacl` shows
          # `user:<you>:rw-`), so neither `programs.adb.enable` nor membership
          # of `adbusers` is required to talk to a plugged-in device.
          packages = [ pythonEnv pkgs.qt6.qttools pkgs.android-tools androidFhs ];

          shellHook = ''
            export QT_PLUGIN_PATH="${pkgs.lib.makeSearchPath "lib/qt-6/plugins" qtModules}"
            export QML2_IMPORT_PATH="${pkgs.lib.makeSearchPath "lib/qt-6/qml" qtModules}"
            export QML_IMPORT_PATH="$QML2_IMPORT_PATH"

            # QtWebEngine ships a helper process that must be found at runtime.
            export QTWEBENGINEPROCESS_PATH="${pkgs.qt6.qtwebengine}/libexec/QtWebEngineProcess"

            # Keep the repo importable without an editable install.
            export PYTHONPATH="$PWD''${PYTHONPATH:+:$PYTHONPATH}"

            echo "mycu devShell — python $(python3 -c 'import sys; print(sys.version.split()[0])')"
          '';
        };

        # `nix run .#android-shell` drops you into the FHS env for M2/M3.
        apps.android-shell = {
          type = "app";
          program = "${androidFhs}/bin/mycu-android-build";
        };
      });
}
