#!/usr/bin/env python3
# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import ast
import compileall
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
TESTS = ROOT / "tests"
SCHEMA = ROOT / "schema"
FORBIDDEN_IMPORT_PREFIXES = (
    "PyQt",
    "PySide",
    "kdenlive",
    "mlt",
    "KF6",
)
FORBIDDEN_SOURCE_MARKERS = (
    '#include "core.h"',
    '#include "kdenlivedoc',
    "TimelineItemModel",
    "DocUndoStack",
    "QUndoStack",
    "src/vibecut/",
)


def main() -> int:
    failures: list[str] = []
    print("[runtime-verify] root:", ROOT)
    failures.extend(check_schemas())
    failures.extend(check_clean_room_boundary())
    if not compileall.compile_dir(str(SRC), quiet=1, force=True):
        failures.append("runtime/src failed Python bytecode compilation")
    failures.extend(run_tests())
    if failures:
        print("[runtime-verify] FAILED", file=sys.stderr)
        for failure in failures:
            print("  -", failure, file=sys.stderr)
        return 1
    print("[runtime-verify] PASS: schemas, clean-room boundary, compile, and fake-adapter tests")
    return 0


def check_schemas() -> list[str]:
    failures: list[str] = []
    required = {
        "editplan.schema.json",
        "toolpolicy.schema.json",
        "evidence.schema.json",
        "job.schema.json",
        "envelope.schema.json",
        "messages.schema.json",
    }
    present = {path.name for path in SCHEMA.glob("*.json")}
    missing = sorted(required - present)
    if missing:
        failures.append("missing required public schemas: " + ", ".join(missing))
    for path in sorted(SCHEMA.glob("*.json")):
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            failures.append(f"invalid JSON schema {path.name}: {exc}")
            continue
        if not isinstance(value, dict) or "$schema" not in value:
            failures.append(f"schema {path.name} is not a JSON Schema object")
        comment = value.get("$comment", "") if isinstance(value, dict) else ""
        if "Apache-2.0" not in str(comment) and "CC0" not in str(comment):
            failures.append(f"schema {path.name} lacks an open SPDX marker")
    return failures


def check_clean_room_boundary() -> list[str]:
    failures: list[str] = []
    for path in sorted(SRC.rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        if "SPDX-License-Identifier: LicenseRef-halthinks-Proprietary" not in text:
            failures.append(f"runtime implementation file lacks proprietary SPDX marker: {path.relative_to(ROOT)}")
        for marker in FORBIDDEN_SOURCE_MARKERS:
            if marker in text:
                failures.append(f"forbidden editor/GPL marker {marker!r} in {path.relative_to(ROOT)}")
        try:
            tree = ast.parse(text, filename=str(path))
        except SyntaxError as exc:
            failures.append(f"syntax error in {path.relative_to(ROOT)}: {exc}")
            continue
        for node in ast.walk(tree):
            modules: list[str] = []
            if isinstance(node, ast.Import):
                modules.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                modules.append(node.module)
            for module in modules:
                if module.startswith(FORBIDDEN_IMPORT_PREFIXES):
                    failures.append(f"forbidden editor dependency import {module!r} in {path.relative_to(ROOT)}")
    pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    for token in ("PyQt", "PySide", "kdenlive", "mlt", "KF6"):
        if token in pyproject:
            failures.append(f"forbidden editor dependency {token!r} in runtime/pyproject.toml")
    return failures


def run_tests() -> list[str]:
    env = os.environ.copy()
    python_path = os.pathsep.join([str(SRC), str(TESTS), env.get("PYTHONPATH", "")]).rstrip(os.pathsep)
    env["PYTHONPATH"] = python_path
    process = subprocess.run(
        [sys.executable, "-m", "unittest", "discover", "-s", str(TESTS), "-p", "test_*.py", "-v"],
        cwd=str(ROOT),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    print(process.stdout, end="")
    return [] if process.returncode == 0 else [f"standalone runtime tests failed with exit code {process.returncode}"]


if __name__ == "__main__":
    raise SystemExit(main())
