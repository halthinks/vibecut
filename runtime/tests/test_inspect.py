# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import unittest

from fake_adapter import FakeAdapter
from halthinks_runtime.policy import TrustMode
from halthinks_runtime.session import RuntimeSession, SessionError


class RuntimeInspectionTests(unittest.TestCase):
    def test_read_only_inspection_refreshes_revision_before_planning(self) -> None:
        adapter = FakeAdapter(revision=7, value=3)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        adapter.drift()
        inspected = session.inspect(adapter, "project_snapshot")
        self.assertEqual(inspected["project_revision"], 8)
        self.assertEqual(inspected["result"]["value"], 3)
        assert session.hello is not None
        self.assertEqual(session.hello.project_revision, 8)
        session.prepare_plan(
            {
                "id": "fresh-after-inspect",
                "base_revision": 8,
                "objective": "plan from inspected state",
                "operations": [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 4},
                        "depends_on": [],
                        "expected_postconditions": [],
                    }
                ],
            }
        )
        self.assertEqual(session.plan.base_revision, 8)

    def test_inspection_after_drift_discards_unapproved_stale_plan(self) -> None:
        adapter = FakeAdapter(revision=5)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            {
                "id": "will-go-stale",
                "base_revision": 5,
                "objective": "be invalidated",
                "operations": [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 9},
                        "depends_on": [],
                        "expected_postconditions": [],
                    }
                ],
            }
        )
        adapter.drift()
        session.inspect(adapter, "project_snapshot")
        self.assertIsNone(session.plan)
        self.assertEqual(session.execution_order, ())
        with self.assertRaises(SessionError):
            session.submit_plan(adapter)

    def test_inspect_refuses_mutating_tool(self) -> None:
        adapter = FakeAdapter(revision=2)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        with self.assertRaises(SessionError):
            session.inspect(adapter, "set_value", {"value": 3})
        self.assertEqual(adapter.value, 0)
        self.assertEqual(adapter.revision, 2)

    def test_inspect_refuses_while_authorization_is_active(self) -> None:
        adapter = FakeAdapter(revision=4)
        session = RuntimeSession()
        session.accept_hello(adapter.hello())
        session.prepare_plan(
            {
                "id": "authorized",
                "base_revision": 4,
                "objective": "lock inspection state",
                "operations": [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 1},
                        "depends_on": [],
                        "expected_postconditions": [],
                    }
                ],
            }
        )
        session.submit_plan(adapter)
        session.accept_authorization(adapter.authorize(TrustMode.OFF, human_approved=True))
        with self.assertRaises(SessionError):
            session.inspect(adapter, "project_snapshot")


if __name__ == "__main__":
    unittest.main()
