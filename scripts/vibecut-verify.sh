#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Authoritative VibeCut verification gate. It is runnable locally and from CI.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VIBECUT_BUILD_DIR:-$ROOT/build-vibecut}"
GENERATOR="${VIBECUT_CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${VIBECUT_BUILD_TYPE:-Debug}"
JOBS="${VIBECUT_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

printf '\n== VibeCut verification ==\n'
printf 'source: %s\n' "$ROOT"
printf 'build:  %s\n' "$BUILD_DIR"
printf 'type:   %s\n\n' "$BUILD_TYPE"

# Fail early with a readable dependency report instead of letting CMake fail
# deep in Kdenlive configuration on an incomplete machine.
bash "$ROOT/scripts/vibecut-build-env-check.sh"

cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON \
  "$@"

# For Ninja, keep compiling independent translation units after failures so a
# single verification run exposes the whole current compiler-error batch.
# This significantly shortens repair loops on the full Kdenlive tree.
if [[ "$GENERATOR" == "Ninja" ]]; then
  cmake --build "$BUILD_DIR" --parallel "$JOBS" -- -k 0
else
  cmake --build "$BUILD_DIR" --parallel "$JOBS"
fi

# All VibeCut-focused Catch/ECM tests are named from vibecut*.cpp source files.
ctest --test-dir "$BUILD_DIR" \
  --output-on-failure \
  --no-tests=error \
  -R '^vibecut'

printf '\nVibeCut verification passed.\n'
