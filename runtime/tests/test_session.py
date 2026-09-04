# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import unittest

from fake_adapter import FakeAdapter
from halthinks_runtime.policy import TrustMode
from halthinks_runtime.session import RuntimeSession, SessionError


def plan(revision: int, operations: list[dict]) -> dict:
    return {
        "id": "plan-1",
        "base_revision": revision,
        "objective": "Exercise governed fake editor",
        "operations": operations,
    }


class RuntimeSessionTests(unittest.TestCase):
    def test_two_mutations_use_moving_revision_and_verify_each_step(self) -> None:
        adapter = FakeAdapter(revision=7, value=1)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            plan(
                7,
                [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 10},
                        "depends_on": [],
                        "expected_postconditions": ["value is 10"],
                    },
                    {
                        "id": "op-2",
                        "tool": "add_value",
                        "input": {"delta": 5},
                        "depends_on": ["op-1"],
                        "expected_postconditions": ["value is 15"],
                    },
                ],
            )
        )
        self.assertTrue(session.requires_confirmation(TrustMode.OFF))
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.OFF, human_approved=True))
        result = session.execute_authorized(adapter)

        self.assertEqual(result.final_revision, 9)
        self.assertEqual(adapter.value, 15)
        self.assertTrue(adapter.completed)
        self.assertEqual([item["revision_before"] for item in result.operation_results], [7, 8])
        self.assertEqual([item["revision_after"] for item in result.operation_results], [8, 9])
        self.assertEqual(len(result.verification_results), 2)
        self.assertEqual(result.verification_results[-1]["inspection_result"]["value"], 15)
        self.assertTrue(all("tool" not in payload and "input" not in payload for payload in adapter.invocation_payloads))

    def test_post_approval_runtime_input_mutation_cannot_substitute_adapter_plan(self) -> None:
        adapter = FakeAdapter(revision=3)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            plan(
                3,
                [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 11},
                        "depends_on": [],
                        "expected_postconditions": [],
                    }
                ],
            )
        )
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.OFF, human_approved=True))
        # PlanOperation is frozen, but its JSON input is intentionally a normal
        # mapping. Even if compromised caller code mutates the runtime copy now,
        # invoke sends only operation_id and the adapter executes its stored copy.
        assert session.plan is not None
        session.plan.operations[0].input["value"] = 999
        result = session.execute_authorized(adapter)
        self.assertEqual(result.final_revision, 4)
        self.assertEqual(adapter.value, 11)

    def test_async_operation_waits_for_terminal_job_and_adopts_terminal_revision(self) -> None:
        adapter = FakeAdapter(revision=12, value=1)
        session = RuntimeSession()
        session.accept_hello(adapter.hello(TrustMode.TURBO))
        session.prepare_plan(
            plan(
                12,
                [
                    {
                        "id": "op-1",
                        "tool": "async_set",
                        "input": {"value": 44},
                        "depends_on": [],
                        "expected_postconditions": ["value is 44"],
                    },
                    {
                        "id": "op-2",
                        "tool": "add_value",
                        "input": {"delta": 1},
                        "depends_on": ["op-1"],
                        "expected_postconditions": ["value is 45"],
                    },
                ],
            )
        )
        # async_set has a hard confirmation requirement even in Turbo.
        self.assertTrue(session.requires_confirmation(TrustMode.TURBO))
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.TURBO, human_approved=True))
        result = session.execute_authorized(adapter)
        self.assertEqual(result.final_revision, 14)
        self.assertEqual(adapter.value, 45)

    def test_external_revision_drift_refuses_remaining_operation(self) -> None:
        class DriftAfterFirstInvoke(FakeAdapter):
            def exchange(self, message):  # type: ignore[override]
                response = super().exchange(message)
                if message.type == "verify" and message.payload.get("operation_id") == "op-1" and response.type != "error":
                    self.drift()
                return response

        adapter = DriftAfterFirstInvoke(revision=20)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            plan(
                20,
                [
                    {"id": "op-1", "tool": "set_value", "input": {"value": 2}, "depends_on": [], "expected_postconditions": []},
                    {"id": "op-2", "tool": "add_value", "input": {"delta": 1}, "depends_on": ["op-1"], "expected_postconditions": []},
                ],
            )
        )
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.OFF, human_approved=True))
        with self.assertRaises(SessionError):
            session.execute_authorized(adapter)
        self.assertTrue(adapter.aborted)
        self.assertEqual(adapter.value, 2)

    def test_verification_failure_aborts_and_invalidates_authorization(self) -> None:
        adapter = FakeAdapter(revision=5)
        adapter.fail_verification = True
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            plan(
                5,
                [{"id": "op-1", "tool": "set_value", "input": {"value": 8}, "depends_on": [], "expected_postconditions": ["value is 8"]}],
            )
        )
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.OFF, human_approved=True))
        with self.assertRaises(SessionError):
            session.execute_authorized(adapter)
        self.assertTrue(adapter.aborted)
        self.assertIsNone(session.authorization_id)

    def test_stale_plan_is_refused_before_protocol_submission(self) -> None:
        adapter = FakeAdapter(revision=9)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        with self.assertRaises(SessionError):
            session.prepare_plan(
                plan(
                    8,
                    [{"id": "op-1", "tool": "set_value", "input": {"value": 1}, "depends_on": [], "expected_postconditions": []}],
                )
            )


if __name__ == "__main__":
    unittest.main()
