#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Bootstrap a Debian host for building the current VibeCut/Kdenlive development tree.
#
# The current tree requires Qt >= 6.10, KF >= 6.21 and MLT >= 7.38. Debian 13
# (trixie) is too old for those minimums, so this script installs the build
# stack explicitly from Debian sid while leaving sid at low APT priority for
# unrelated packages.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APT_SUITE="${VIBECUT_APT_SUITE:-sid}"
SKIP_VERIFY="${VIBECUT_BOOTSTRAP_SKIP_VERIFY:-0}"

if [[ ! -r /etc/os-release ]]; then
  echo "Unsupported host: /etc/os-release is unavailable." >&2
  exit 2
fi
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "debian" ]]; then
  echo "This bootstrap currently supports Debian only (detected ${ID:-unknown})." >&2
  exit 2
fi

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
  SUDO=()
elif command -v sudo >/dev/null 2>&1; then
  SUDO=(sudo)
else
  echo "Root privileges or sudo are required to install build dependencies." >&2
  exit 2
fi

printf '\n== VibeCut Debian build bootstrap ==\n'
printf 'Debian: %s (%s)\n' "${VERSION_ID:-unknown}" "${VERSION_CODENAME:-unknown}"
printf 'APT suite for development stack: %s\n\n' "$APT_SUITE"

# Kdenlive 26.11 development currently needs versions newer than Debian 13.
# Keep sid pinned low globally, then request it explicitly for this dependency
# transaction. This avoids silently making every future package install prefer
# unstable.
if [[ "$APT_SUITE" == "sid" ]]; then
  SID_SOURCE=/etc/apt/sources.list.d/vibecut-sid.sources
  SID_PREF=/etc/apt/preferences.d/vibecut-sid
  if [[ ! -f "$SID_SOURCE" ]]; then
    "${SUDO[@]}" tee "$SID_SOURCE" >/dev/null <<'EOF'
Types: deb
URIs: https://deb.debian.org/debian
Suites: sid
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF
  fi
  "${SUDO[@]}" tee "$SID_PREF" >/dev/null <<'EOF'
Package: *
Pin: release a=unstable
Pin-Priority: 100

Package: *
Pin: release n=sid
Pin-Priority: 100
EOF
fi

"${SUDO[@]}" apt-get update

# Keep this list explicit and source-controlled. It mirrors the required
# find_package() families in the repository's top-level CMakeLists.txt, plus
# the normal compiler/build utilities needed for Kdenlive tests.
packages=(
  build-essential
  cmake
  ninja-build
  pkgconf
  git
  gettext
  extra-cmake-modules

  # Qt 6: Core/Widgets/Svg/Qml/Quick/QuickControls2/Concurrent/QuickWidgets,
  # Multimedia/MultimediaWidgets/Network/NetworkAuth/SvgWidgets/Xml/DBus.
  qt6-base-dev
  qt6-base-dev-tools
  qt6-base-private-dev
  qt6-declarative-dev
  qt6-declarative-private-dev
  qt6-svg-dev
  qt6-multimedia-dev
  qt6-networkauth-dev
  qt6-tools-dev
  qt6-tools-dev-tools

  # KDE Frameworks required by the root CMake configuration.
  libkf6i18n-dev
  libkf6archive-dev
  libkf6bookmarks-dev
  libkf6codecs-dev
  libkf6coreaddons-dev
  libkf6config-dev
  libkf6configwidgets-dev
  libkf6kio-dev
  libkf6widgetsaddons-dev
  libkf6notifyconfig-dev
  libkf6newstuff-dev
  libkf6xmlgui-dev
  libkf6notifications-dev
  libkf6guiaddons-dev
  libkf6textwidgets-dev
  libkf6iconthemes-dev
  libkf6solid-dev
  libkf6filemetadata-dev
  libkf6purpose-dev
  libkf6dbusaddons-dev
  # Optional-but-used VibeCut/Kdenlive integrations.
  libkf6crash-dev
  libkf6wallet-dev

  # Docking UI.
  libkddockwidgets-qt6-dev

  # MLT / rendering.
  libmlt-dev
  libmlt++-dev
  melt

  # LibAV/FFmpeg development APIs.
  ffmpeg
  libavformat-dev
  libavcodec-dev
  libswresample-dev
  libavutil-dev

  # Timeline interchange and Imath workaround required by CMake.
  libopentimelineio-dev
  libimath-dev

  # Common Kdenlive build/runtime assets used by local smoke testing.
  mediainfo
  frei0r-plugins
)

apt_args=(install --yes --no-install-recommends)
if [[ -n "$APT_SUITE" ]]; then
  apt_args+=(--target-release "$APT_SUITE")
fi
"${SUDO[@]}" apt-get "${apt_args[@]}" "${packages[@]}"

printf '\nInstalled requested build dependency set. Running authoritative checks...\n'
bash "$ROOT/scripts/vibecut-build-env-check.sh"

if [[ "$SKIP_VERIFY" != "1" ]]; then
  exec bash "$ROOT/scripts/vibecut-verify.sh"
fi

printf '\nBootstrap completed; verification was skipped by VIBECUT_BOOTSTRAP_SKIP_VERIFY=1.\n'
