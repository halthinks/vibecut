#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Local VibeCut verification gate. Intentionally does not use GitHub Actions.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VIBECUT_BUILD_DIR:-$ROOT/build-vibecut}"
GENERATOR="${VIBECUT_CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${VIBECUT_BUILD_TYPE:-Debug}"
JOBS="${VIBECUT_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

printf '\n== VibeCut local verification ==\n'
printf 'source: %s\n' "$ROOT"
printf 'build:  %s\n' "$BUILD_DIR"
printf 'type:   %s\n\n' "$BUILD_TYPE"

cmake -S "$ROOT" -B "$BUILD_DIR" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON \
  "$@"

cmake --build "$BUILD_DIR" --parallel "$JOBS"

# All VibeCut-focused Catch/ECM tests are named from vibecut*.cpp source files.
ctest --test-dir "$BUILD_DIR" \
  --output-on-failure \
  --no-tests=error \
  -R '^vibecut'

printf '\nVibeCut local verification passed.\n'
