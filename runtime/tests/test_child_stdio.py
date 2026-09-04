# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

from fake_adapter import FakeAdapter
from halthinks_runtime.policy import TrustMode
from halthinks_runtime.protocol import decode_line


class EditorHostedRuntimeChildTests(unittest.TestCase):
    def test_parent_gpl_host_drives_child_runtime_over_inherited_stdio(self) -> None:
        tests_dir = Path(__file__).resolve().parent
        env = os.environ.copy()
        command = [sys.executable, str(tests_dir / "fake_runtime_child.py")]
        process = subprocess.Popen(
            command,
            cwd=tests_dir,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
        )
        assert process.stdin is not None
        assert process.stdout is not None
        assert process.stderr is not None
        adapter = FakeAdapter(revision=7, value=1)

        def send(message) -> None:
            process.stdin.write(message.encode_line())
            process.stdin.flush()

        send(adapter.hello(TrustMode.OFF))
        try:
            while process.poll() is None:
                raw = process.stdout.readline()
                if raw == b"":
                    break
                request = decode_line(raw)
                self.assertEqual(request.kind, "request")
                response = adapter.exchange(request)
                send(response)
                if request.type == "propose_plan" and response.type != "error":
                    send(adapter.authorize(TrustMode.OFF, human_approved=True))
                if request.type == "invoke" and response.type != "error" and response.payload.get("started") is True:
                    while True:
                        event = adapter.next_event()
                        if event is None:
                            break
                        send(event)
                        job = event.payload.get("job", {})
                        if event.type == "job_update" and job.get("state") in {"succeeded", "failed", "cancelled"}:
                            break
            return_code = process.wait(timeout=5.0)
            stderr = process.stderr.read().decode("utf-8", errors="replace")
            self.assertEqual(return_code, 0, stderr)
            self.assertIn("runtime child success", stderr)
            self.assertEqual(adapter.revision, 9)
            self.assertEqual(adapter.value, 13)
            self.assertTrue(adapter.completed)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2.0)
            for stream in (process.stdin, process.stdout, process.stderr):
                if stream is not None:
                    stream.close()


if __name__ == "__main__":
    unittest.main()
