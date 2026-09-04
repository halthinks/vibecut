# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

from halthinks_runtime.policy import TrustMode
from halthinks_runtime.session import RuntimeSession
from halthinks_runtime.stdio import StdioAdapterClient, StdioClientError


class StdioProtocolTests(unittest.TestCase):
    def test_fake_adapter_process_executes_governed_plan_over_ndjson(self) -> None:
        tests_dir = Path(__file__).resolve().parent
        command = [sys.executable, str(tests_dir / "fake_adapter_stdio.py")]
        with StdioAdapterClient(command, cwd=tests_dir, response_timeout=5.0, event_timeout=5.0) as client:
            session = RuntimeSession()
            session.accept_hello(client.read_hello())
            session.prepare_plan(
                {
                    "id": "stdio-plan",
                    "base_revision": 7,
                    "objective": "exercise real stdio seam",
                    "operations": [
                        {
                            "id": "op-1",
                            "tool": "set_value",
                            "input": {"value": 9},
                            "depends_on": [],
                            "expected_postconditions": ["value is 9"],
                        },
                        {
                            "id": "op-2",
                            "tool": "add_value",
                            "input": {"delta": 4},
                            "depends_on": ["op-1"],
                            "expected_postconditions": ["value is 13"],
                        },
                    ],
                }
            )
            session.submit_plan(client)
            authorization = client.next_event()
            self.assertIsNotNone(authorization)
            session.accept_authorization(authorization)
            result = session.execute_authorized(client)
            self.assertEqual(result.final_revision, 9)
            self.assertEqual(result.verification_results[-1]["inspection_result"]["value"], 13)
            self.assertEqual(client.diagnostics(), ())

    def test_fake_adapter_process_propagates_async_job_events(self) -> None:
        tests_dir = Path(__file__).resolve().parent
        command = [sys.executable, str(tests_dir / "fake_adapter_stdio.py")]
        with StdioAdapterClient(command, cwd=tests_dir, response_timeout=5.0, event_timeout=5.0) as client:
            session = RuntimeSession()
            session.accept_hello(client.read_hello())
            session.prepare_plan(
                {
                    "id": "stdio-async-plan",
                    "base_revision": 7,
                    "objective": "exercise async stdio seam",
                    "operations": [
                        {
                            "id": "op-1",
                            "tool": "async_set",
                            "input": {"value": 40},
                            "depends_on": [],
                            "expected_postconditions": ["value is 40"],
                        },
                        {
                            "id": "op-2",
                            "tool": "add_value",
                            "input": {"delta": 2},
                            "depends_on": ["op-1"],
                            "expected_postconditions": ["value is 42"],
                        },
                    ],
                }
            )
            session.submit_plan(client)
            session.accept_authorization(client.next_event())
            result = session.execute_authorized(client)
            self.assertEqual(result.final_revision, 9)
            self.assertEqual(result.verification_results[-1]["inspection_result"]["value"], 42)

    def test_stdout_noise_is_rejected_as_protocol_not_treated_as_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "bad_adapter.py"
            script.write_text(
                "import sys\nsys.stdout.write('not-json\\n'); sys.stdout.flush()\n",
                encoding="utf-8",
            )
            with StdioAdapterClient([sys.executable, str(script)], response_timeout=2.0, event_timeout=2.0) as client:
                with self.assertRaises(StdioClientError):
                    client.read_hello()

    def test_stderr_is_bounded_diagnostic_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "stderr_adapter.py"
            script.write_text(
                textwrap.dedent(
                    """
                    import sys, time
                    print('diagnostic-only', file=sys.stderr, flush=True)
                    time.sleep(0.2)
                    """
                ),
                encoding="utf-8",
            )
            client = StdioAdapterClient([sys.executable, str(script)], response_timeout=1.0, event_timeout=1.0)
            client.start()
            try:
                with self.assertRaises(StdioClientError):
                    client.read_hello()
                self.assertTrue(any("diagnostic-only" in line for line in client.diagnostics()))
            finally:
                client.close()


if __name__ == "__main__":
    unittest.main()
