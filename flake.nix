{
  description = "CedarView — Cedarville student data (chapel attendance first), desktop + Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Every Qt module the desktop build links or loads at runtime.
        # QtWebEngine is the desktop login + transport backend; it has no Android
        # equivalent (Android uses QtWebView, which comes from the Android Qt kit
        # that scripts/build-apk fetches, not from this devShell).
        qtModules = with pkgs.qt6; [
          qtbase
          qtdeclarative
          qtsvg
          qtwebengine
          qtwebchannel
          qtpositioning
        ];

        # Desktop-only dev tooling. The app itself has no Python in it any more;
        # this is for scripts/discover, scripts/check-live and
        # scripts/smoke-transport, which drive QtWebEngine through PySide6 and
        # share their helpers through scripts/devkit.py. Nothing here reaches
        # the APK.
        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          pyside6
          shiboken6
          # `scripts/discover` pretty-prints captured pages with it, and
          # degrades to a notice if it is missing.
          lxml
          # Only scripts/check-live and offline tooling use it.
          httpx
        ]);

        # Android packaging needs an FHS layout. Everything the Android build
        # runs is a prebuilt Linux binary that expects /usr/lib-style paths:
        # the NDK's clang, the Android SDK tools, Gradle's JVM, and the host Qt
        # tools (moc, rcc, qmlcachegen, androiddeployqt) from the Qt kits that
        # scripts/build-apk fetches with aqtinstall. Keeping this in a
        # project-local FHS env avoids enabling programs.nix-ld globally.
        # See docs/android.md.
        androidFhs = pkgs.buildFHSEnv {
          name = "cedarview-android-build";
          targetPkgs = p: with p; [
            # Host-only: runs aqtinstall (from a venv under .qt/) and the gate
            # scripts in scripts/build-apk. Never enters the APK.
            python3
            python3Packages.pip
            python3Packages.virtualenv

            jdk17
            android-tools   # adb, for installing and reading logcat
            unzip
            zip
            which
            git
            wget
            curl
            file
            cmake
            ninja
            pkg-config

            # Reads the manifest out of an .aab (`bundletool dump manifest`),
            # which aapt cannot, and turns a bundle into the split APKs Play
            # would serve so it can be installed and sized locally. Used by
            # scripts/build-apk --aab.
            bundletool

            # Shared libraries for the host Qt tools in the aqtinstall kits
            # (linux_gcc_64): moc, rcc, qmlcachegen, qmlimportscanner and
            # androiddeployqt. They are manylinux-style binaries and expect an
            # FHS system to supply these. A missing one shows up only as a bare
            # `exit 127`, so if a host tool dies that way, run it by hand to see
            # which `.so` it wants, e.g.
            #
            #   .qt/6.11.1/gcc_64/libexec/qmlimportscanner --help
            zstd
            zlib
            openssl
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
          profile = ''
            export JAVA_HOME="${pkgs.jdk17}"
            # scripts/build-apk uses this as its "am I inside the FHS shell?"
            # check.
            export CEDARVIEW_ANDROID_FHS=1
            echo "cedarview android FHS shell. See docs/android.md for the build steps."
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
          # buildInputs, not packages: cmake's setup hook only adds *build
          # inputs* to CMAKE_PREFIX_PATH, which is how find_package(Qt6) finds
          # the split nixpkgs Qt modules.
          buildInputs = qtModules;

          packages = [
            pkgs.cmake
            pkgs.ninja
            pkgs.qt6.qttools
            pythonEnv
            pkgs.android-tools
            androidFhs
          ];

          shellHook = ''
            export QT_PLUGIN_PATH="${pkgs.lib.makeSearchPath "lib/qt-6/plugins" qtModules}"
            export QML2_IMPORT_PATH="${pkgs.lib.makeSearchPath "lib/qt-6/qml" qtModules}"
            export QML_IMPORT_PATH="$QML2_IMPORT_PATH"

            # QtWebEngine ships a helper process that must be found at runtime.
            export QTWEBENGINEPROCESS_PATH="${pkgs.qt6.qtwebengine}/libexec/QtWebEngineProcess"

            echo "cedarview devShell — cmake -B build -G Ninja && cmake --build build"
          '';
        };

        # `nix run .#android-shell` drops you into the FHS env for Android builds.
        apps.android-shell = {
          type = "app";
          program = "${androidFhs}/bin/cedarview-android-build";
        };
      });
}
