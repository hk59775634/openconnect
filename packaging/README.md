# Client packaging

Build Windows / Android client packages for openconnect-tunnel releases.

## Windows (x64 portable zip)

Requires Docker:

```bash
docker build --network=host -f packaging/Dockerfile.windows -t openconnect-win-builder:40 .
mkdir -p dist
docker run --rm --network=host \
  -v "$PWD":/src:ro -v "$PWD/dist":/out \
  -v "$PWD/packaging/build-windows.sh":/usr/local/bin/build-windows.sh:ro \
  -e SRC_DIR=/src -e OUT_DIR=/out -e OC_VERSION="$(git describe --tags)" \
  -e FORCE_COPY=1 \
  --entrypoint /usr/local/bin/build-windows.sh \
  openconnect-win-builder:40
```

Output: `dist/openconnect-windows-x64-*.zip`

## Android (signed APK + JNI)

Requires Android SDK, NDK r27c, JDK 17+, ant.

### Release signing

Place a keystore and properties file (gitignored):

```bash
mkdir -p packaging/secrets
# packaging/secrets/openconnect-tunnel-release.jks
# packaging/secrets/release.properties:
#   path=/absolute/path/to/openconnect-tunnel-release.jks
#   alias=openconnect-tunnel
#   password=...
```

**Back up `packaging/secrets/` securely.** Losing the keystore blocks signed updates.

### Build

```bash
export ANDROID_HOME=...
export NDK=/opt/android-sdk-linux_x86/android-ndk-r27c
export ANDROID_ABIS=arm64   # or: arm arm64 x86 x86_64
./packaging/build-android.sh
```

Uses [ics-openconnect](https://gitlab.com/openconnect/ics-openconnect) UI with this tree as `libopenconnect`.

## CI

`.github/workflows/build-clients.yml` builds both on tag push / `workflow_dispatch`.
