#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${VIBECUT_PACKAGE_BUILD_DIR:-$ROOT/build-vibecut-package}"
OUTPUT_DIR="${VIBECUT_PACKAGE_OUTPUT_DIR:-$ROOT/packages}"
JOBS="${VIBECUT_JOBS:-4}"
BUILD_TYPE="${VIBECUT_BUILD_TYPE:-Release}"
PREFIX="/opt/vibecut-halthinks"
PACKAGE_NAME="vibecut-halthinks"

command -v cmake >/dev/null || { echo "cmake is required" >&2; exit 2; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb is required" >&2; exit 2; }
command -v dpkg >/dev/null || { echo "dpkg is required" >&2; exit 2; }

bash "$ROOT/scripts/vibecut-build-env-check.sh"

if [[ -n "${VIBECUT_PACKAGE_VERSION:-}" ]]; then
  VERSION="$VIBECUT_PACKAGE_VERSION"
else
  VERSION="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo 0.0.0)"
fi
VERSION="${VERSION#v}"
VERSION="$(printf '%s' "$VERSION" | sed -E 's/[^0-9A-Za-z.+:~\-]/./g')"
[[ -n "$VERSION" ]] || VERSION="0.0.0"
ARCH="$(dpkg --print-architecture)"

rm -rf "$BUILD_DIR"
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"

cmake --build "$BUILD_DIR" --parallel "$JOBS" -- -k 0

if [[ "${VIBECUT_PACKAGE_SKIP_TESTS:-0}" != "1" ]]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure -R '^vibecut'
fi

PKG_ROOT="$BUILD_DIR/package-root"
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT"
DESTDIR="$PKG_ROOT" cmake --install "$BUILD_DIR"

mkdir -p "$PKG_ROOT/usr/bin" "$PKG_ROOT/usr/share/applications" "$PKG_ROOT/DEBIAN"
cat > "$PKG_ROOT/usr/bin/vibecut-halthinks" <<'EOF'
#!/usr/bin/env bash
exec /opt/vibecut-halthinks/bin/kdenlive "$@"
EOF
chmod 0755 "$PKG_ROOT/usr/bin/vibecut-halthinks"

cat > "$PKG_ROOT/usr/share/applications/org.halthinks.vibecut.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=VibeCut (halthinks)
Comment=Governed agentic nonlinear video editor built on VibeCut and Kdenlive
Exec=vibecut-halthinks %U
Icon=kdenlive
Terminal=false
Categories=AudioVideo;AudioVideoEditing;Qt;KDE;
MimeType=application/x-kdenlive;
StartupNotify=true
EOF

DEPENDS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
  mapfile -t ELF_FILES < <(find "$PKG_ROOT$PREFIX" -type f -print0 | xargs -0 -r file 2>/dev/null | awk -F: '/ELF .* (executable|shared object)/ {print $1}')
  if (( ${#ELF_FILES[@]} > 0 )); then
    set +e
    SHLIB_OUTPUT="$(cd "$ROOT" && dpkg-shlibdeps -O "${ELF_FILES[@]}" 2>/dev/null)"
    SHLIB_STATUS=$?
    set -e
    if [[ $SHLIB_STATUS -eq 0 ]]; then
      DEPENDS="${SHLIB_OUTPUT#shlibs:Depends=}"
    else
      echo "warning: dpkg-shlibdeps could not derive runtime dependencies; package will omit an automatic Depends field" >&2
    fi
  fi
fi

{
  echo "Package: $PACKAGE_NAME"
  echo "Version: $VERSION"
  echo "Section: video"
  echo "Priority: optional"
  echo "Architecture: $ARCH"
  echo "Maintainer: halthinks/VibeCut contributors"
  [[ -z "$DEPENDS" ]] || echo "Depends: $DEPENDS"
  echo "Homepage: https://github.com/halthinks/vibecut"
  echo "Description: halthinks capability-expanded VibeCut fork"
  echo " VibeCut is a governed agentic nonlinear video editor built on the original"
  echo " VibeCut project and Kdenlive. This package installs the halthinks fork under"
  echo " /opt/vibecut-halthinks so it can coexist with a distribution Kdenlive install."
} > "$PKG_ROOT/DEBIAN/control"

INSTALLED_SIZE="$(du -sk "$PKG_ROOT" | awk '{print $1}')"
echo "Installed-Size: $INSTALLED_SIZE" >> "$PKG_ROOT/DEBIAN/control"

cat > "$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postinst"

cat > "$PKG_ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$PKG_ROOT/DEBIAN/postrm"

mkdir -p "$OUTPUT_DIR"
OUTPUT="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
rm -f "$OUTPUT"
dpkg-deb --build --root-owner-group "$PKG_ROOT" "$OUTPUT"

echo
echo "Built VibeCut installer:"
echo "  $OUTPUT"
echo
echo "Install with:"
echo "  sudo apt install ./$OUTPUT"
