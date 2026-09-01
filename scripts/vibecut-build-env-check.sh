#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Repository-local prerequisite diagnostics for VibeCut/Kdenlive builds.
set -euo pipefail

missing=0

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    printf 'MISSING command: %s\n' "$cmd" >&2
    missing=1
  else
    printf 'ok command: %-12s %s\n' "$cmd" "$(command -v "$cmd")"
  fi
}

require_pkgconfig() {
  local pkg="$1"
  if ! pkg-config --exists "$pkg" 2>/dev/null; then
    printf 'MISSING pkg-config package: %s\n' "$pkg" >&2
    missing=1
  else
    printf 'ok package: %-18s %s\n' "$pkg" "$(pkg-config --modversion "$pkg")"
  fi
}

printf '\n== VibeCut build-environment check ==\n'
require_cmd cmake
require_cmd c++
require_cmd pkg-config

if [[ "${VIBECUT_CMAKE_GENERATOR:-Ninja}" == "Ninja" ]]; then
  require_cmd ninja
fi

# These are the non-negotiable base families used by the VibeCut/Kdenlive tree.
# CMake performs the authoritative component/version checks afterwards; this
# script exists to fail early with a readable explanation on incomplete hosts.
require_pkgconfig Qt6Core
require_pkgconfig Qt6Qml

mlt_ok=0
for pkg in mlt++-7 mlt-framework-7 mlt++ mlt-framework; do
  if pkg-config --exists "$pkg" 2>/dev/null; then
    printf 'ok package: %-18s %s\n' "$pkg" "$(pkg-config --modversion "$pkg")"
    mlt_ok=1
  fi
done
if [[ "$mlt_ok" -eq 0 ]]; then
  printf 'MISSING MLT development pkg-config metadata (tried mlt++-7/mlt-framework-7 and unversioned names)\n' >&2
  missing=1
fi

# KF6 is primarily discovered through CMake package config files rather than
# a stable pkg-config umbrella, so probe the common config locations.
kf6_config="$(find /usr/lib /usr/local/lib /opt -type f -name 'KF6Config.cmake' -print -quit 2>/dev/null || true)"
if [[ -z "$kf6_config" ]]; then
  printf 'MISSING KDE Frameworks 6 CMake development packages (KF6Config.cmake not found)\n' >&2
  missing=1
else
  printf 'ok KF6 config: %s\n' "$kf6_config"
fi

if [[ "$missing" -ne 0 ]]; then
  cat >&2 <<'EOF'

VibeCut build prerequisites are incomplete on this host.
Install the Kdenlive development dependency set (Qt 6 including Qml,
KDE Frameworks 6, MLT/MLT++, FFmpeg development dependencies, and the
other packages required by this repository), then rerun:

  bash scripts/vibecut-verify.sh

No GitHub Actions/CI are required or used by this gate.
EOF
  exit 2
fi

printf 'Base VibeCut build prerequisites look present; continuing to CMake for authoritative checks.\n\n'
