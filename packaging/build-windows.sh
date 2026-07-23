#!/usr/bin/env bash
# Cross-compile openconnect-tunnel Windows x64 NSIS installer (MinGW64 + GnuTLS).
set -euo pipefail

SRC="${SRC_DIR:-/src}"
OUT="${OUT_DIR:-/out}"
BUILD="${BUILD_DIR:-/build/win64}"

mkdir -p "$OUT" "$BUILD"
cd "$BUILD"

# Fresh copy so configure/make don't touch the bind-mounted tree unexpectedly
FORCE_COPY="${FORCE_COPY:-0}"
if [[ ! -f "$BUILD/.copied" || "$FORCE_COPY" = "1" ]]; then
  rm -rf "$BUILD/src"
  mkdir -p "$BUILD/src"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --exclude='.git' --exclude='android/*-linux-android*' \
      --exclude='.libs' --exclude='*.o' --exclude='*.lo' \
      --exclude='dist' --exclude='packaging' \
      "$SRC"/ "$BUILD/src"/
  else
    tar -C "$SRC" --exclude='.git' --exclude='.libs' --exclude='dist' --exclude='packaging' \
      -cf - . | tar -C "$BUILD/src" -xf -
  fi
  touch "$BUILD/.copied"
fi

cd "$BUILD/src"

# Inject release version (no .git in build copy)
OC_VERSION="${OC_VERSION:-}"
if [[ -z "$OC_VERSION" && -x "$SRC/version.sh" ]]; then
  # version.sh writes to $1; capture tag from host tree when possible
  if [[ -d "$SRC/.git" ]]; then
    OC_VERSION="$(git -C "$SRC" describe --tags --always 2>/dev/null || true)"
  fi
fi
OC_VERSION="${OC_VERSION:-v9.12-tunnel}"
printf 'const char openconnect_version_str[] = "%s";\n' "$OC_VERSION" > version.c

if [[ ! -x ./configure ]]; then
  NOCONFIGURE=1 ./autogen.sh
fi

# Clean any host-native build leftovers
make distclean >/dev/null 2>&1 || true
# Restore version.c after distclean
printf 'const char openconnect_version_str[] = "%s";\n' "$OC_VERSION" > version.c

# lz4 / stoken often unavailable as MinGW RPMs; build without them.
mingw64-configure \
  --with-vpnc-script=vpnc-script-win.js \
  --without-gnutls-version-check \
  --without-lz4 \
  --without-stoken \
  --disable-dsa-tests \
  --sbindir='${exec_prefix}/bin' \
  CFLAGS='-O2 -g'

# Build binaries first (do not require NSIS for success)
make -j"$(nproc)" V=1 openconnect.exe libopenconnect.la list-system-keys.exe || \
  make -j"$(nproc)" V=1

VERSION="$(cut -f2 -d\" version.c 2>/dev/null || echo unknown)"
SAFE_VER="$(echo "$VERSION" | tr '/' '-')"

# Fetch Wintun + vpnc-script and assemble file list for packaging
make -j"$(nproc)" V=1 file-list.txt vpnc-script-win.js .libs/wintun.dll || true

# Portable zip (primary Windows artifact)
STAGE="$OUT/openconnect-windows-x64-${SAFE_VER}"
rm -rf "$STAGE"
mkdir -p "$STAGE"
if [[ -f file-list.txt ]]; then
  while read -r f; do
    [[ -z "$f" ]] && continue
    [[ -f "$f" ]] || continue
    cp -a "$f" "$STAGE/"
  done < file-list.txt
fi
[[ -f vpnc-script-win.js ]] && cp -a vpnc-script-win.js "$STAGE/"
# Ensure core binaries present even if file-list incomplete
cp -a .libs/openconnect.exe "$STAGE/" 2>/dev/null || true
cp -a .libs/libopenconnect*.dll "$STAGE/" 2>/dev/null || true
cp -a .libs/wintun.dll "$STAGE/" 2>/dev/null || true
cp -a .libs/list-system-keys.exe "$STAGE/" 2>/dev/null || true
cat > "$STAGE/README-WINDOWS.txt" <<EOF
openconnect-tunnel ${VERSION} — Windows x64 portable package

Usage (Administrator Command Prompt recommended):
  openconnect.exe --user=USER https://vpn.example.com/tunnelgroup

Includes Wintun (wintun.dll) and vpnc-script-win.js.
DST domains are exported via CISCO_DYNAMIC_SPLIT_*_DOMAINS; enable library
routing with the DST C API or handle routes in your helper script.
EOF
(
  cd "$OUT"
  rm -f "openconnect-windows-x64-${SAFE_VER}.zip"
  zip -r "openconnect-windows-x64-${SAFE_VER}.zip" "openconnect-windows-x64-${SAFE_VER}"
)
rm -rf "$STAGE"

# Optional NSIS installer
if command -v makensis >/dev/null 2>&1 && [[ -d /usr/share/nsis/Stubs ]]; then
  if make openconnect-installer-MinGW64-GnuTLS.exe V=1; then
    ARTIFACT="$(ls -1 openconnect-installer-MinGW64-GnuTLS-*.exe 2>/dev/null | head -1 || true)"
    [[ -n "$ARTIFACT" ]] && cp -a "$ARTIFACT" "$OUT/"
    [[ -f openconnect-installer-MinGW64-GnuTLS.exe ]] && \
      cp -a openconnect-installer-MinGW64-GnuTLS.exe "$OUT/" || true
  else
    echo "WARNING: NSIS installer build failed; portable zip was still produced." >&2
  fi
fi

echo "Windows artifacts:"
ls -lah "$OUT"
