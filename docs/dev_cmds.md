```
nix develop
cmake -B build -G Ninja                             # first time only
cmake --build build && ./build/cedarview --demo
```

2. Install to phone for testing

With the phone plugged in over USB:
```
nix develop --command cedarview-android-build -c './scripts/build-apk --install'
This is signed with your debug key, so each run installs over the last one. Add --clean only if the build itself breaks.
```

3. Release APK (for GitHub Releases)

First, bump project(CedarView VERSION x.y.z …) in CMakeLists.txt
export QT_ANDROID_KEYSTORE_PATH=$HOME/keys/cedarview-release.jks
export QT_ANDROID_KEYSTORE_ALIAS=cedarview
export QT_ANDROID_KEYSTORE_STORE_PASS=...
export QT_ANDROID_KEYSTORE_KEY_PASS=...
nix develop --command cedarview-android-build -c './scripts/build-apk --release'
This writes cedarview-<version>-arm64-v8a.apk to the repo root. Then publish it:
gh release create v<version> cedarview-<version>-arm64-v8a.apk --title "CedarView <version>" --notes "..."

4. Release AAB (for Google Play)

Bump the version and set the same four exports as above, then:
nix develop --command cedarview-android-build -c './scripts/build-apk --aab'
This writes cedarview-<version>.aab, which is the file you upload, and cedarview-<version>.apks. To install it on your phone the same way Play would:
nix develop --command cedarview-android-build -c 'bundletool install-apks --apks=cedarview-<version>.apks'

Things to remember:
- Never reuse a version number. Play rejects a reused versionCode, and a phone won't install it as an update.
- Build the first AAB you upload with your real key. That first upload locks in your upload key for good. If you're only testing the build with a throwaway key, add --out-dir /some/scratch/dir so that file can never end up in the repo root and get uploaded by mistake.
- Back up ~/keys/cedarview-release.jks and its passwords. If you lose them, you can't ship an update that installs over the copies people already have.