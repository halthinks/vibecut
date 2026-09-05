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
PACKAGE = SRC / "halthinks_runtime"
FORBIDDEN_IMPORT_PREFIXES = (
    "PyQt",
    "PySide",
    "kdenlive",
    "mlt",
    "KF6",
    "vibecut",
)
FORBIDDEN_SOURCE_MARKERS = (
    '#include "core.h"',
    '#include "kdenlivedoc',
    "TimelineItemModel",
    "DocUndoStack",
    "QUndoStack",
    "VibeCutToolSurface",
    "src/vibecut/",
    "GPL-3.0",
    "LicenseRef-KDE-Accepted-GPL",
)
REQUIRED_RUNTIME_FILES = {
    "__init__.py",
    "child_stdio.py",
    "contracts.py",
    "evidence.py",
    "jobs.py",
    "policy.py",
    "protocol.py",
    "providers.py",
    "revision.py",
    "session.py",
    "stdio.py",
    "worker.py",
}
REQUIRED_TEST_FILES = {
    "fake_adapter.py",
    "fake_adapter_stdio.py",
    "fake_runtime_child.py",
    "test_child_stdio.py",
    "test_core.py",
    "test_inspect.py",
    "test_providers.py",
    "test_session.py",
    "test_stdio.py",
    "test_worker.py",
}


def main() -> int:
    failures: list[str] = []
    print("[runtime-verify] root:", ROOT)
    failures.extend(check_required_files())
    failures.extend(check_schemas())
    failures.extend(check_clean_room_boundary())
    failures.extend(check_runtime_entrypoint())
    if not compileall.compile_dir(str(SRC), quiet=1, force=True):
        failures.append("runtime/src failed Python bytecode compilation")
    failures.extend(run_tests())
    if failures:
        print("[runtime-verify] FAILED", file=sys.stderr)
        for failure in failures:
            print("  -", failure, file=sys.stderr)
        return 1
    print("[runtime-verify] PASS: required files, schemas, clean-room boundary, entrypoint, compile, and fake-adapter/process tests")
    return 0


def check_required_files() -> list[str]:
    failures: list[str] = []
    package_files = {path.name for path in PACKAGE.glob("*.py")}
    test_files = {path.name for path in TESTS.glob("*.py")}
    missing_runtime = sorted(REQUIRED_RUNTIME_FILES - package_files)
    missing_tests = sorted(REQUIRED_TEST_FILES - test_files)
    if missing_runtime:
        failures.append("missing required clean-room runtime modules: " + ", ".join(missing_runtime))
    if missing_tests:
        failures.append("missing required standalone runtime tests/fixtures: " + ", ".join(missing_tests))
    return failures


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
    try:
        envelope = json.loads((SCHEMA / "envelope.schema.json").read_text(encoding="utf-8"))
        message_types = envelope["properties"]["type"]["enum"]
        if "plan_handoff" not in message_types:
            failures.append("public envelope schema does not advertise plan_handoff")
        messages_text = (SCHEMA / "messages.schema.json").read_text(encoding="utf-8")
        if '"planHandoff"' not in messages_text or '"plan_handoff"' not in messages_text:
            failures.append("typed messages schema does not define plan_handoff")
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        failures.append(f"could not verify hosted plan_handoff schema coverage: {exc}")
    return failures


def check_clean_room_boundary() -> list[str]:
    failures: list[str] = []
    for path in sorted(SRC.rglob("*.py")):
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if "SPDX-License-Identifier: LicenseRef-halthinks-Proprietary" not in text:
            failures.append(f"runtime implementation file lacks proprietary SPDX marker: {relative}")
        for marker in FORBIDDEN_SOURCE_MARKERS:
            if marker in text:
                failures.append(f"forbidden editor/GPL marker {marker!r} in {relative}")
        try:
            tree = ast.parse(text, filename=str(path))
        except SyntaxError as exc:
            failures.append(f"syntax error in {relative}: {exc}")
            continue
        for node in ast.walk(tree):
            modules: list[str] = []
            if isinstance(node, ast.Import):
                modules.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                modules.append(node.module)
            for module in modules:
                if module.startswith(FORBIDDEN_IMPORT_PREFIXES):
                    failures.append(f"forbidden editor dependency import {module!r} in {relative}")
    try:
        pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    except OSError as exc:
        failures.append(f"could not read runtime/pyproject.toml: {exc}")
        return failures
    for token in ("PyQt", "PySide", "kdenlive", "mlt", "KF6", "GPL-3.0", "LicenseRef-KDE-Accepted-GPL"):
        if token in pyproject:
            failures.append(f"forbidden editor/GPL dependency marker {token!r} in runtime/pyproject.toml")
    if "LicenseRef-halthinks-Proprietary" not in pyproject:
        failures.append("runtime/pyproject.toml does not declare the proprietary runtime license reference")
    return failures


def check_runtime_entrypoint() -> list[str]:
    failures: list[str] = []
    try:
        pyproject = (ROOT / "pyproject.toml").read_text(encoding="utf-8")
    except OSError as exc:
        return [f"could not read runtime/pyproject.toml for entrypoint verification: {exc}"]
    if 'halthinks-vibecut-runtime = "halthinks_runtime.worker:main"' not in pyproject:
        failures.append("runtime/pyproject.toml does not expose the hosted worker entrypoint")
    return failures


def run_tests() -> list[str]:
    env = os.environ.copy()
    python_path = os.pathsep.join([str(SRC), str(TESTS), env.get("PYTHONPATH", "")]).rstrip(os.pathsep)
    env["PYTHONPATH"] = python_path
    try:
        process = subprocess.run(
            [sys.executable, "-m", "unittest", "discover", "-s", str(TESTS), "-p", "test_*.py", "-v"],
            cwd=str(ROOT),
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=120,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        print(output, end="")
        return ["standalone runtime tests exceeded the 120-second verification bound"]
    print(process.stdout, end="")
    return [] if process.returncode == 0 else [f"standalone runtime tests failed with exit code {process.returncode}"]


if __name__ == "__main__":
    raise SystemExit(main())
