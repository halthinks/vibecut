#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Repository-local prerequisite diagnostics for the current VibeCut/Kdenlive tree.
set -euo pipefail

missing=0
TMP="$(mktemp -d "${TMPDIR:-/tmp}/vibecut-env-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    printf 'MISSING command: %s\n' "$cmd" >&2
    missing=1
  else
    printf 'ok command: %-14s %s\n' "$cmd" "$(command -v "$cmd")"
  fi
}

require_pkgconfig_version() {
  local pkg="$1" min="$2"
  if ! pkg-config --exists "$pkg" 2>/dev/null; then
    printf 'MISSING pkg-config package: %s >= %s\n' "$pkg" "$min" >&2
    missing=1
    return
  fi
  local version
  version="$(pkg-config --modversion "$pkg")"
  if ! pkg-config --atleast-version="$min" "$pkg" 2>/dev/null; then
    printf 'TOO OLD package: %-22s found %s, need >= %s\n' "$pkg" "$version" "$min" >&2
    missing=1
  else
    printf 'ok package: %-22s %s\n' "$pkg" "$version"
  fi
}

printf '\n== VibeCut build-environment check ==\n'
require_cmd cmake
require_cmd c++
require_cmd pkg-config
if [[ "${VIBECUT_CMAKE_GENERATOR:-Ninja}" == "Ninja" ]]; then
  require_cmd ninja
fi

# Fast, readable version checks for the two version-critical pkg-config families.
require_pkgconfig_version Qt6Core 6.10.0
require_pkgconfig_version Qt6Qml 6.10.0

mlt_pkg=""
for pkg in mlt++-7 mlt-framework-7 mlt++ mlt-framework; do
  if pkg-config --exists "$pkg" 2>/dev/null; then
    mlt_pkg="$pkg"
    break
  fi
done
if [[ -z "$mlt_pkg" ]]; then
  printf 'MISSING MLT development pkg-config metadata (need MLT >= 7.38.0)\n' >&2
  missing=1
else
  require_pkgconfig_version "$mlt_pkg" 7.38.0
fi

# CMake is authoritative for the component-heavy dependency families. This
# probe mirrors the root CMakeLists minimums and required components without
# configuring the whole application tree.
cat >"$TMP/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(VibeCutDependencyProbe LANGUAGES CXX)

set(QT_MIN_VERSION 6.10.0)
set(KF_DEP_VERSION 6.21.0)
set(MLT_MIN_VERSION 7.38.0)

find_package(ECM ${KF_DEP_VERSION} REQUIRED CONFIG)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
  Core Widgets Svg Qml Quick QuickControls2 Concurrent QuickWidgets
  Multimedia MultimediaWidgets Network NetworkAuth SvgWidgets Xml DBus)

find_package(KF6 ${KF_DEP_VERSION} REQUIRED COMPONENTS
  I18n Archive Bookmarks Codecs CoreAddons Config ConfigWidgets KIO
  WidgetsAddons NotifyConfig NewStuff XmlGui Notifications GuiAddons
  TextWidgets IconThemes Solid FileMetaData Purpose DBusAddons)

# Optional in upstream Kdenlive, but required/used by VibeCut when available.
find_package(KF6 ${KF_DEP_VERSION} QUIET COMPONENTS Crash Wallet)

find_package(KDDockWidgets-qt6 2.4.0 REQUIRED)
find_package(Mlt7 ${MLT_MIN_VERSION} REQUIRED COMPONENTS xml)
find_package(FFmpeg REQUIRED COMPONENTS AVFORMAT AVCODEC SWRESAMPLE AVUTIL)
find_package(OpenTimelineIO REQUIRED)
find_package(Imath REQUIRED)
EOF

if command -v cmake >/dev/null 2>&1; then
  if ! cmake -S "$TMP" -B "$TMP/build" -G Ninja >"$TMP/cmake.log" 2>&1; then
    printf '\nCMake dependency probe FAILED:\n' >&2
    cat "$TMP/cmake.log" >&2
    missing=1
  else
    printf 'ok CMake dependency probe: Qt/KF6/KDDockWidgets/MLT/FFmpeg/OTIO/Imath satisfy required components.\n'
  fi
fi

if [[ "$missing" -ne 0 ]]; then
  cat >&2 <<'EOF'

VibeCut build prerequisites are incomplete or too old on this host.
For Debian, install the complete matching stack with:

  bash scripts/vibecut-bootstrap-debian.sh

The current Kdenlive development tree requires Qt >= 6.10, KF/ECM >= 6.21,
MLT >= 7.38, KDDockWidgets >= 2.4, FFmpeg development APIs,
OpenTimelineIO, and Imath. Debian 13/trixie stable is too old for several
of those minimums, so the bootstrap uses a pinned Debian sid dependency set.

Then rerun:

  bash scripts/vibecut-verify.sh

No GitHub Actions/CI are required or used by this gate.
EOF
  exit 2
fi

printf 'VibeCut build prerequisites satisfy the declared dependency contract.\n\n'
