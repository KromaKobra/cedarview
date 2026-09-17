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

          # Core parsing. The chapel page may be JSON or server-rendered HTML
          # (see docs/discovery.md, M0) — lxml covers the HTML case and costs
          # nothing if it turns out to be JSON.
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
            python3
            python3Packages.pip
            python3Packages.virtualenv
            python3Packages.setuptools
            python3Packages.wheel

            jdk17
            unzip
            zip
            which
            git
            wget
            curl
            ccache
            pkg-config
            autoconf
            automake
            libtool
            libffi
            openssl
            zlib
            ncurses
            cmake
            ninja
            file
          ];
          # buildozer wants a writable HOME for its .buildozer cache; keep it
          # inside the project so `git clean` can reclaim it.
          profile = ''
            export BUILDOZER_HOME="$PWD/.buildozer-home"
            export JAVA_HOME="${pkgs.jdk17}"
            echo "mycu android FHS shell. See docs/android.md for the build steps."
          '';
          runScript = "bash";
        };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = [ pythonEnv pkgs.qt6.qttools androidFhs ];

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
