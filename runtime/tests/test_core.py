# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from halthinks_runtime.contracts import EditPlan, PlanValidationError, validate_plan
from halthinks_runtime.evidence import EvidenceError, EvidenceRecord, EvidenceStore
from halthinks_runtime.jobs import JobError, JobManager, JobState, MAX_RESULT_BYTES
from halthinks_runtime.policy import ToolPolicy, ToolRisk, TrustMode
from halthinks_runtime.protocol import Envelope, ProtocolError, decode_line, request
from halthinks_runtime.revision import RevisionGate, StaleRevisionError


class ContractTests(unittest.TestCase):
    def test_dependency_order_is_deterministic(self) -> None:
        plan = EditPlan.from_json(
            {
                "id": "p",
                "base_revision": 1,
                "objective": "test",
                "operations": [
                    {"id": "b", "tool": "t", "input": {}, "depends_on": ["a"], "expected_postconditions": []},
                    {"id": "a", "tool": "t", "input": {}, "depends_on": [], "expected_postconditions": []},
                    {"id": "c", "tool": "t", "input": {}, "depends_on": ["a"], "expected_postconditions": []},
                ],
            }
        )
        self.assertEqual(validate_plan(plan), ("a", "b", "c"))

    def test_cycle_fails_closed(self) -> None:
        with self.assertRaises(PlanValidationError):
            EditPlan.from_json(
                {
                    "id": "p",
                    "base_revision": 1,
                    "objective": "cycle",
                    "operations": [
                        {"id": "a", "tool": "t", "input": {}, "depends_on": ["b"], "expected_postconditions": []},
                        {"id": "b", "tool": "t", "input": {}, "depends_on": ["a"], "expected_postconditions": []},
                    ],
                }
            )


class PolicyTests(unittest.TestCase):
    def test_hard_confirmation_survives_turbo(self) -> None:
        policy = ToolPolicy("danger", ToolRisk.MAJOR_EDIT, True, True, False, True, True, True)
        self.assertTrue(policy.requires_confirmation(TrustMode.TURBO))

    def test_auto_allows_reversible_but_not_major_by_default(self) -> None:
        reversible = ToolPolicy("r", ToolRisk.REVERSIBLE_EDIT, True, True, False, False, False, True)
        major = ToolPolicy("m", ToolRisk.MAJOR_EDIT, True, True, False, False, False, True)
        self.assertFalse(reversible.requires_confirmation(TrustMode.AUTO))
        self.assertTrue(major.requires_confirmation(TrustMode.AUTO))


class RevisionTests(unittest.TestCase):
    def test_base_and_expected_revision_are_distinct(self) -> None:
        gate = RevisionGate(7)
        gate.authorize(7)
        gate.advance(7, 8)
        gate.advance(8, 9)
        self.assertEqual(gate.base_revision, 7)
        self.assertEqual(gate.expected_revision, 9)

    def test_unrelated_drift_raises_stale_subtype(self) -> None:
        gate = RevisionGate(2)
        gate.authorize(2)
        with self.assertRaises(StaleRevisionError):
            gate.require_current(3)


class JobTests(unittest.TestCase):
    def test_job_lifecycle_and_result(self) -> None:
        jobs = JobManager()
        job = jobs.create("render", "render fake", cancelable=True)
        jobs.mark_running(job.id)
        jobs.set_progress(job.id, 50)
        jobs.set_result(job.id, {"artifact": "x"})
        jobs.mark_succeeded(job.id)
        result = jobs.get(job.id)
        self.assertEqual(result.state, JobState.SUCCEEDED)
        self.assertEqual(result.progress, 100)
        self.assertEqual(result.result, {"artifact": "x"})

    def test_cancel_and_terminal_transition_rules(self) -> None:
        jobs = JobManager()
        job = jobs.create("x", "x", cancelable=True)
        jobs.mark_running(job.id)
        jobs.request_cancel(job.id)
        jobs.mark_cancelled(job.id)
        with self.assertRaises(JobError):
            jobs.mark_succeeded(job.id)

    def test_result_size_is_bounded(self) -> None:
        jobs = JobManager()
        job = jobs.create("x", "x")
        with self.assertRaises(JobError):
            jobs.set_result(job.id, {"payload": "x" * (MAX_RESULT_BYTES + 1)})


class EvidenceTests(unittest.TestCase):
    @staticmethod
    def record(*, text: str, fingerprint: str = "fp", extractor: str = "e") -> EvidenceRecord:
        return EvidenceRecord.from_json(
            {
                "source_id": "bin:1",
                "source_fingerprint": fingerprint,
                "extractor_id": extractor,
                "extractor_version": "1",
                "kind": "transcript_segment",
                "start_frame": 0,
                "end_frame": 10,
                "text": text,
                "confidence": 1.0,
                "metadata": {"authority": "observation"},
            }
        )

    def test_slice_replacement_is_atomic_and_provenance_scoped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".vibecutmedia.json"
            store = EvidenceStore(path)
            store.replace_slice([self.record(text="old")])
            store.replace_slice([self.record(text="new")])
            self.assertEqual([record.text for record in store.records()], ["new"])
            reloaded = EvidenceStore(path)
            self.assertEqual([record.text for record in reloaded.records()], ["new"])
            root = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(root["version"], 1)

    def test_different_fingerprint_is_not_same_slice(self) -> None:
        store = EvidenceStore()
        store.replace_slice([self.record(text="a", fingerprint="fp-a")])
        store.replace_slice([self.record(text="b", fingerprint="fp-b")])
        self.assertEqual(len(store.records()), 2)

    def test_mixed_slice_write_fails_closed(self) -> None:
        store = EvidenceStore()
        with self.assertRaises(EvidenceError):
            store.replace_slice([self.record(text="a"), self.record(text="b", extractor="other")])

    def test_store_exposes_no_project_truth_mutator(self) -> None:
        store = EvidenceStore()
        self.assertFalse(hasattr(store, "set_project_state"))
        self.assertFalse(hasattr(store, "mutate_project"))


class ProtocolTests(unittest.TestCase):
    def test_envelope_round_trip(self) -> None:
        message = request("inspect", {"operation": "project_snapshot", "input": {}}, "m1")
        self.assertEqual(decode_line(message.encode_line()), message)

    def test_unknown_protocol_type_fails_closed(self) -> None:
        with self.assertRaises(ProtocolError):
            Envelope.from_json({"v": 1, "id": "x", "kind": "request", "type": "invented", "payload": {}})


if __name__ == "__main__":
    unittest.main()
