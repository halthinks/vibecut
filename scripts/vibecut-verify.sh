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

# The clean-room runtime is a separately licensed/process-isolated component,
# but its public schemas, boundary rules, hosted-worker entrypoint, and full
# standalone regression suite are required for the external-runtime path to be
# valid. Run its stdlib-only verifier before the expensive C++ build so the
# authoritative local/CI gate cannot silently pass with a broken runtime.
if ! command -v python3 >/dev/null 2>&1; then
  printf 'ERROR: python3 is required for runtime/verify.py\n' >&2
  exit 1
fi
printf '\n== Clean-room runtime verification ==\n'
python3 "$ROOT/runtime/verify.py"

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