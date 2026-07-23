#!/usr/bin/env bash
# Build Android APK using ics-openconnect UI + this openconnect-tunnel tree as the native core.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT_DIR:-$ROOT/dist}"
WORK="${WORK_DIR:-/tmp/openconnect-android-build}"
ICS_URL="${ICS_URL:-https://gitlab.com/openconnect/ics-openconnect.git}"
NDK_ROOT="${NDK:-/opt/android-sdk-linux_x86/android-ndk-r27c}"
NDK_ZIP_URL="${NDK_ZIP_URL:-https://dl.google.com/android/repository/android-ndk-r27c-linux.zip}"

mkdir -p "$OUT" "$WORK"

if [[ ! -d "$NDK_ROOT" ]]; then
  echo "NDK not found at $NDK_ROOT — downloading r27c..."
  mkdir -p "$(dirname "$NDK_ROOT")"
  ZIP="/tmp/android-ndk-r27c-linux.zip"
  if [[ ! -f "$ZIP" ]]; then
    curl -L --fail -o "$ZIP" "$NDK_ZIP_URL"
  fi
  unzip -q "$ZIP" -d "$(dirname "$NDK_ROOT")"
  # archive extracts as android-ndk-r27c
  if [[ ! -d "$NDK_ROOT" ]]; then
    echo "Expected NDK dir missing after unzip" >&2
    exit 1
  fi
fi

if [[ ! -d "$WORK/ics-openconnect/.git" ]]; then
  rm -rf "$WORK/ics-openconnect"
  git clone --depth 1 "$ICS_URL" "$WORK/ics-openconnect"
fi

cd "$WORK/ics-openconnect"

# Replace openconnect submodule with our tunnel fork (copy, keep android/ Makefile)
rm -rf external/openconnect
mkdir -p external/openconnect
# Copy sources needed for android build (avoid huge .git / build artifacts)
tar -C "$ROOT" \
  --exclude='.git' \
  --exclude='.libs' \
  --exclude='*.o' \
  --exclude='*.lo' \
  --exclude='*.la' \
  --exclude='openconnect' \
  --exclude='android/*-linux-android*' \
  --exclude='android/sources' \
  --exclude='dist' \
  --exclude='packaging' \
  -cf - . | tar -C external/openconnect -xf -

# Ensure configure exists and is not host-configured
if [[ ! -x external/openconnect/configure ]]; then
  (cd external/openconnect && NOCONFIGURE=1 ./autogen.sh)
fi
# Android builds out-of-tree; host config.status would break configure
(
  cd external/openconnect
  make distclean >/dev/null 2>&1 || true
  rm -f config.status config.log Makefile libtool
)

# stoken submodule
if [[ ! -d external/stoken/.git ]]; then
  git submodule update --init external/stoken || \
    git clone --depth 1 https://github.com/cernekee/stoken.git external/stoken
fi

# Prefer fewer ABIs if ANDROID_ABIS is set (default: arm64 only for faster CI/local)
export NDK
ARCH_LIST="${ANDROID_ABIS:-arm64}"
# Patch external Makefile ARCH_LIST if needed
if [[ "$ARCH_LIST" != "arm arm64 x86 x86_64" ]]; then
  sed -i "s/^ARCH_LIST := .*/ARCH_LIST := ${ARCH_LIST}/" external/Makefile
fi

make -C external install NDK="$NDK_ROOT" -j"$(nproc)"

# Android SDK required for gradle
: "${ANDROID_HOME:?Set ANDROID_HOME to Android SDK}"
export ANDROID_HOME

VERSION="$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo v9.12-tunnel)"
SAFE_VER="$(echo "$VERSION" | tr '/' '-')"

# Optional release signing (packaging/secrets/release.properties)
SECRETS_PROPS="${RELEASE_PROPERTIES:-$ROOT/packaging/secrets/release.properties}"
if [[ -f "$SECRETS_PROPS" ]]; then
  cp -a "$SECRETS_PROPS" app/release.properties
  # Fix absolute path inside properties if relative
  echo "Using release signing from $SECRETS_PROPS"
else
  rm -f app/release.properties
  echo "WARNING: no release.properties — APK may be unsigned" >&2
fi

# Align app versionName with openconnect-tunnel tag
sed -i "s/versionName \".*\"/versionName \"${SAFE_VER#v}\"/" app/build.gradle

./gradlew assembleRelease --no-daemon

APK="$(find app/build/outputs/apk/release -name '*.apk' 2>/dev/null | head -1 || true)"
if [[ -z "$APK" ]]; then
  APK="$(find app/build/outputs/apk -name '*.apk' | head -1)"
fi
AAB="$(find app/build/outputs/bundle -name '*.aab' 2>/dev/null | head -1 || true)"

if [[ -n "$APK" ]]; then
  cp -a "$APK" "$OUT/openconnect-tunnel-android-${SAFE_VER}.apk"
fi
if [[ -n "$AAB" ]]; then
  cp -a "$AAB" "$OUT/openconnect-tunnel-android-${SAFE_VER}.aab"
fi

# Also ship native libs tarball for SDK integrators
tar -C app/src/main -czf "$OUT/openconnect-tunnel-android-jni-arm64-${SAFE_VER}.tar.gz" jniLibs 2>/dev/null || true

echo "Android artifacts:"
ls -lah "$OUT"/openconnect-tunnel-android-*
