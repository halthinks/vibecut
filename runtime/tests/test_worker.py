# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import os
import subprocess
import sys
import unittest

from fake_adapter import FakeAdapter
from halthinks_runtime.policy import TrustMode
from halthinks_runtime.protocol import Envelope, decode_line, request


class HostedPlanWorkerTests(unittest.TestCase):
    def test_gpl_parent_hands_staged_plan_to_runtime_worker(self) -> None:
        env = os.environ.copy()
        process = subprocess.Popen(
            [sys.executable, "-m", "halthinks_runtime.worker"],
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
        plan = {
            "id": "hosted-worker-plan",
            "base_revision": 7,
            "objective": "prove host staged external orchestration",
            "operations": [
                {"id": "op-1", "tool": "set_value", "input": {"value": 10}, "depends_on": [], "expected_postconditions": ["value is 10"]},
                {"id": "op-2", "tool": "add_value", "input": {"delta": 3}, "depends_on": ["op-1"], "expected_postconditions": ["value is 13"]},
            ],
        }
        staged = adapter.exchange(request("propose_plan", plan, "host-stage"))
        self.assertEqual(staged.type, "propose_plan")
        self.assertTrue(staged.payload["ok"])

        def send(message: Envelope) -> None:
            process.stdin.write(message.encode_line())
            process.stdin.flush()

        send(adapter.hello(TrustMode.OFF))
        send(Envelope(1, "handoff-1", "event", "plan_handoff", {"plan": plan, "project_revision": 7}))
        send(adapter.authorize(TrustMode.OFF, human_approved=True))

        try:
            while True:
                raw = process.stdout.readline()
                if raw == b"":
                    break
                message = decode_line(raw)
                self.assertEqual(message.kind, "request")
                response = adapter.exchange(message)
                send(response)
                if message.type == "invoke" and response.type != "error" and response.payload.get("started") is True:
                    while True:
                        event = adapter.next_event()
                        if event is None:
                            break
                        send(event)
                        job = event.payload.get("job", {})
                        if event.type == "job_update" and job.get("state") in {"succeeded", "failed", "cancelled"}:
                            break
                if message.type == "complete_plan" and response.type != "error":
                    process.stdin.close()
                    break

            return_code = process.wait(timeout=5.0)
            stderr = process.stderr.read().decode("utf-8", errors="replace")
            self.assertEqual(return_code, 0, stderr)
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
            for stream in (process.stdout, process.stderr):
                if stream is not None:
                    stream.close()


if __name__ == "__main__":
    unittest.main()
