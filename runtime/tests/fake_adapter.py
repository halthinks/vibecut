# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import copy
import uuid
from dataclasses import dataclass
from typing import Any

from halthinks_runtime.contracts import EditPlan, validate_plan
from halthinks_runtime.policy import ToolPolicy, ToolRisk, TrustMode, plan_requires_confirmation
from halthinks_runtime.protocol import Envelope, PROTOCOL_VERSION


@dataclass
class _PendingJob:
    job_id: str
    operation_id: str
    target_value: int
    stage: int = 0


class FakeAdapter:
    """Protocol-faithful fake editor with no Kdenlive dependency."""

    def __init__(self, revision: int = 7, value: int = 0) -> None:
        self.revision = revision
        self.value = value
        self.plan: EditPlan | None = None
        self.execution_order: tuple[str, ...] = ()
        self.authorization_id: str | None = None
        self.approved_ids: set[str] = set()
        self.completed_ids: set[str] = set()
        self.pending_job: _PendingJob | None = None
        self.aborted = False
        self.completed = False
        self.fail_verification = False
        self.invocation_payloads: list[dict[str, Any]] = []
        self.policies = self._policies()

    def hello(self, trust_mode: TrustMode = TrustMode.OFF) -> Envelope:
        tools = []
        for name in ("project_snapshot", "set_value", "add_value", "async_set"):
            tools.append(
                {
                    "schema": {
                        "name": name,
                        "description": f"fake {name}",
                        "input_schema": {"type": "object"},
                    },
                    "policy": self.policies[name].to_json(),
                }
            )
        return Envelope(
            PROTOCOL_VERSION,
            "hello-1",
            "event",
            "hello",
            {
                "editor_id": "fake-editor",
                "adapter_id": "fake-vibecut-adapter",
                "protocol_versions": [1],
                "project_revision": self.revision,
                "trust_mode": trust_mode.value,
                "tools": tools,
            },
        )

    def authorize(self, mode: TrustMode, *, human_approved: bool | None) -> Envelope:
        if self.plan is None:
            return self._error_event("no_pending_plan", "no plan is pending")
        confirmation = plan_requires_confirmation(
            tuple(operation.tool for operation in self.plan.operations), self.policies, mode
        )
        if confirmation and human_approved is None:
            return self._error_event("confirmation_required", "human decision required")
        if human_approved is False:
            plan_id = self.plan.id
            self._clear()
            return Envelope(
                1,
                "auth-rejected",
                "event",
                "authorize",
                {
                    "plan_id": plan_id,
                    "decision": "rejected",
                    "trust_mode": mode.value,
                    "reason": "human rejected fake plan",
                },
            )
        if self.revision != self.plan.base_revision:
            self._clear()
            return self._error_event("stale_revision", "project changed before authorization")
        self.authorization_id = f"auth-{uuid.uuid4()}"
        self.approved_ids = set(self.execution_order)
        return Envelope(
            1,
            "auth-approved",
            "event",
            "authorize",
            {
                "plan_id": self.plan.id,
                "decision": "approved",
                "trust_mode": mode.value,
                "authorization_id": self.authorization_id,
                "expected_revision": self.revision,
                "approved_operation_ids": list(self.execution_order),
            },
        )

    def exchange(self, message: Envelope) -> Envelope:
        if message.kind != "request":
            return self._error(message, "invalid_envelope", "fake adapter accepts requests only")
        handlers = {
            "inspect": self._inspect,
            "propose_plan": self._propose,
            "invoke": self._invoke,
            "verify": self._verify,
            "complete_plan": self._complete,
            "abort_plan": self._abort,
        }
        handler = handlers.get(message.type)
        if handler is None:
            return self._error(message, "unsupported_request", f"unsupported fake request {message.type}")
        return handler(message)

    def next_event(self) -> Envelope | None:
        job = self.pending_job
        if job is None:
            return None
        if job.stage == 0:
            job.stage = 1
            return Envelope(
                1,
                f"event-{uuid.uuid4()}",
                "event",
                "job_update",
                {
                    "job": {
                        "id": job.job_id,
                        "kind": "fake_async_set",
                        "label": "fake async set",
                        "state": "running",
                        "progress": 50,
                        "message": "running",
                        "cancelable": True,
                    },
                    "project_revision": self.revision,
                },
            )
        self.value = job.target_value
        self.revision += 1
        self.completed_ids.add(job.operation_id)
        self.pending_job = None
        return Envelope(
            1,
            f"event-{uuid.uuid4()}",
            "event",
            "job_update",
            {
                "job": {
                    "id": job.job_id,
                    "kind": "fake_async_set",
                    "label": "fake async set",
                    "state": "succeeded",
                    "progress": 100,
                    "message": "done",
                    "cancelable": True,
                    "result": {"value": self.value},
                },
                "project_revision": self.revision,
            },
        )

    def drift(self) -> None:
        self.revision += 1

    def _propose(self, message: Envelope) -> Envelope:
        if self.plan is not None:
            return self._error(message, "plan_busy", "another fake plan is pending")
        try:
            plan = EditPlan.from_json(copy.deepcopy(message.payload))
        except Exception as exc:
            return self._error(message, "invalid_plan", str(exc))
        if plan.base_revision != self.revision:
            return self._error(message, "stale_revision", "base revision mismatch")
        for operation in plan.operations:
            policy = self.policies.get(operation.tool)
            if policy is None or not policy.enabled:
                return self._error(message, "unknown_or_denied_tool", operation.tool)
        if all(self.policies[operation.tool].risk is ToolRisk.READ_ONLY for operation in plan.operations):
            return self._error(message, "read_only_plan", "read-only work does not require a plan")
        self.plan = EditPlan.from_json(copy.deepcopy(plan.to_json()))
        self.execution_order = validate_plan(self.plan)
        return self._response(
            message,
            {
                "ok": True,
                "plan_id": plan.id,
                "base_revision": plan.base_revision,
                "requires_confirmation_off": True,
                "requires_confirmation_auto": any(
                    self.policies[operation.tool].requires_confirmation(TrustMode.AUTO)
                    for operation in plan.operations
                ),
                "requires_confirmation_turbo": any(
                    self.policies[operation.tool].requires_confirmation(TrustMode.TURBO)
                    for operation in plan.operations
                ),
            },
        )

    def _inspect(self, message: Envelope) -> Envelope:
        operation = message.payload.get("operation")
        if operation != "project_snapshot":
            return self._error(message, "inspect_not_read_only", "fake inspection supports project_snapshot only")
        return self._response(
            message,
            {
                "ok": True,
                "operation": operation,
                "project_revision": self.revision,
                "result": {"ok": True, "value": self.value, "revision": self.revision},
            },
        )

    def _invoke(self, message: Envelope) -> Envelope:
        payload = dict(message.payload)
        self.invocation_payloads.append(copy.deepcopy(payload))
        if "tool" in payload or "input" in payload:
            return self._error(message, "plan_substitution_attempt", "tool/input not accepted after approval")
        if self.plan is None or payload.get("plan_id") != self.plan.id:
            return self._error(message, "invalid_authorization", "wrong plan")
        if not self.authorization_id or payload.get("authorization_id") != self.authorization_id:
            return self._error(message, "invalid_authorization", "wrong authorization")
        operation_id = payload.get("operation_id")
        if operation_id not in self.approved_ids or operation_id in self.completed_ids:
            return self._error(message, "operation_not_approved", "operation unavailable")
        next_id = next((item for item in self.execution_order if item not in self.completed_ids), None)
        if operation_id != next_id:
            return self._error(message, "operation_out_of_order", "operation is not next")
        if payload.get("expected_revision") != self.revision:
            self.authorization_id = None
            return self._error(message, "stale_revision", "expected revision mismatch")
        operation = self.plan.operation(str(operation_id))
        if any(dependency not in self.completed_ids for dependency in operation.depends_on):
            return self._error(message, "dependency_incomplete", "dependency incomplete")
        before = self.revision
        if operation.tool == "set_value":
            self.value = _int_input(operation.input, "value")
            self.revision += 1
            self.completed_ids.add(operation.id)
            result = {"ok": True, "value": self.value}
        elif operation.tool == "add_value":
            self.value += _int_input(operation.input, "delta")
            self.revision += 1
            self.completed_ids.add(operation.id)
            result = {"ok": True, "value": self.value}
        elif operation.tool == "async_set":
            if self.pending_job is not None:
                return self._error(message, "job_pending", "fake job already pending")
            job_id = f"job-{uuid.uuid4()}"
            self.pending_job = _PendingJob(job_id, operation.id, _int_input(operation.input, "value"))
            result = {"ok": True, "started": True, "job_id": job_id}
        else:
            return self._error(message, "unsupported_tool", operation.tool)
        payload_out: dict[str, Any] = {
            "ok": True,
            "plan_id": self.plan.id,
            "operation_id": operation.id,
            "tool": operation.tool,
            "revision_before": before,
            "revision_after": self.revision,
            "result": result,
        }
        if result.get("started"):
            payload_out.update({"started": True, "job_id": result["job_id"], "plan_complete_ready": False})
        else:
            payload_out.update({"completed": True, "plan_complete_ready": self._ready_to_complete()})
        return self._response(message, payload_out)

    def _verify(self, message: Envelope) -> Envelope:
        if self.fail_verification:
            return self._error(message, "verification_failed", "forced fake verification failure")
        payload = message.payload
        if self.plan is None or payload.get("plan_id") != self.plan.id:
            return self._error(message, "invalid_verify", "wrong plan")
        if payload.get("authorization_id") != self.authorization_id:
            return self._error(message, "invalid_verify", "wrong authorization")
        operation_id = payload.get("operation_id")
        if operation_id not in self.completed_ids:
            return self._error(message, "operation_incomplete", "operation not complete")
        if payload.get("expected_revision") != self.revision:
            return self._error(message, "stale_revision", "verification revision mismatch")
        if payload.get("inspection") != "project_snapshot":
            return self._error(message, "invalid_verification_inspection", "unsupported inspection")
        return self._response(
            message,
            {
                "ok": True,
                "plan_id": self.plan.id,
                "operation_id": operation_id,
                "project_revision": self.revision,
                "inspection": "project_snapshot",
                "expected_postconditions": list(payload.get("expected_postconditions", [])),
                "inspection_result": {"ok": True, "value": self.value, "revision": self.revision},
                "verification_semantics": "adapter_inspection_evidence_not_freeform_postcondition_interpretation",
            },
        )

    def _complete(self, message: Envelope) -> Envelope:
        if self.plan is None or message.payload.get("plan_id") != self.plan.id:
            return self._error(message, "invalid_completion", "wrong plan")
        if message.payload.get("authorization_id") != self.authorization_id:
            return self._error(message, "invalid_completion", "wrong authorization")
        if message.payload.get("expected_revision") != self.revision:
            return self._error(message, "stale_revision", "completion revision mismatch")
        if not self._ready_to_complete():
            return self._error(message, "plan_incomplete", "operations remain")
        plan_id = self.plan.id
        revision = self.revision
        self.completed = True
        self._clear()
        return self._response(
            message,
            {
                "ok": True,
                "plan_id": plan_id,
                "completed": True,
                "project_revision": revision,
                "checkpoint_rollback_parity": False,
            },
        )

    def _abort(self, message: Envelope) -> Envelope:
        if self.plan is None or message.payload.get("plan_id") != self.plan.id:
            return self._error(message, "invalid_abort", "wrong plan")
        plan_id = self.plan.id
        self.aborted = True
        self._clear()
        return self._response(
            message,
            {
                "ok": True,
                "plan_id": plan_id,
                "aborted": True,
                "reason": str(message.payload.get("reason", "runtime aborted")),
                "rollback_performed": False,
            },
        )

    def _ready_to_complete(self) -> bool:
        return self.pending_job is None and set(self.execution_order) <= self.completed_ids

    def _clear(self) -> None:
        self.plan = None
        self.execution_order = ()
        self.authorization_id = None
        self.approved_ids.clear()
        self.completed_ids.clear()
        self.pending_job = None

    @staticmethod
    def _policies() -> dict[str, ToolPolicy]:
        return {
            "project_snapshot": ToolPolicy("project_snapshot", ToolRisk.READ_ONLY, False, False, False, False, False, True),
            "set_value": ToolPolicy("set_value", ToolRisk.REVERSIBLE_EDIT, True, True, False, False, False, True),
            "add_value": ToolPolicy("add_value", ToolRisk.REVERSIBLE_EDIT, True, True, False, False, False, True),
            "async_set": ToolPolicy("async_set", ToolRisk.MAJOR_EDIT, True, True, True, True, False, True),
        }

    @staticmethod
    def _response(request: Envelope, payload: dict[str, Any]) -> Envelope:
        return Envelope(1, request.id, "response", request.type, payload)

    @staticmethod
    def _error(request: Envelope, code: str, message: str) -> Envelope:
        return Envelope(1, request.id, "response", "error", {"code": code, "message": message, "retryable": False, "details": {}})

    @staticmethod
    def _error_event(code: str, message: str) -> Envelope:
        return Envelope(1, f"event-{uuid.uuid4()}", "event", "error", {"code": code, "message": message, "retryable": False, "details": {}})


def _int_input(value: dict[str, Any], key: str) -> int:
    raw = value.get(key)
    if isinstance(raw, bool) or not isinstance(raw, int):
        raise ValueError(f"fake tool input {key} must be integer")
    return raw
