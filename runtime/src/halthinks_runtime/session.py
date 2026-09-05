# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Any, Mapping

from .contracts import EditPlan, validate_plan
from .policy import ToolPolicy, ToolRisk, TrustMode, plan_requires_confirmation
from .protocol import AdapterClient, Envelope, request
from .revision import RevisionGate


class SessionError(RuntimeError):
    pass


@dataclass(frozen=True)
class SessionHello:
    editor_id: str
    adapter_id: str
    project_revision: int
    trust_mode: TrustMode
    tool_schemas: dict[str, dict[str, Any]]
    policies: dict[str, ToolPolicy]


@dataclass(frozen=True)
class ExecutionResult:
    plan_id: str
    final_revision: int
    operation_results: tuple[dict[str, Any], ...]
    verification_results: tuple[dict[str, Any], ...]
    completion: dict[str, Any]


class RuntimeSession:
    """Govern an EditPlan over a transport-neutral GPL adapter.

    This class never calls editor APIs. It knows only public protocol envelopes,
    tool policies, opaque revision tokens, and adapter-returned evidence.
    """

    def __init__(self) -> None:
        self.hello: SessionHello | None = None
        self.plan: EditPlan | None = None
        self.execution_order: tuple[str, ...] = ()
        self.authorization_id: str | None = None
        self.approved_operation_ids: frozenset[str] = frozenset()
        self.gate: RevisionGate | None = None

    def accept_hello(self, envelope: Envelope | Mapping[str, Any]) -> SessionHello:
        message = envelope if isinstance(envelope, Envelope) else Envelope.from_json(envelope)
        if message.type != "hello" or message.kind not in {"event", "response"}:
            raise SessionError("session initialization requires a hello event/response")
        payload = message.payload
        versions = payload.get("protocol_versions")
        if not isinstance(versions, list) or 1 not in versions:
            raise SessionError("adapter does not advertise protocol v1")
        editor_id = _bounded_string(payload.get("editor_id"), "editor_id")
        adapter_id = _bounded_string(payload.get("adapter_id"), "adapter_id")
        revision = _revision(payload.get("project_revision"), "project_revision")
        try:
            trust_mode = TrustMode(payload.get("trust_mode"))
        except (TypeError, ValueError) as exc:
            raise SessionError("hello contains unsupported trust_mode") from exc
        raw_tools = payload.get("tools")
        if not isinstance(raw_tools, list):
            raise SessionError("hello tools must be an array")
        schemas: dict[str, dict[str, Any]] = {}
        policies: dict[str, ToolPolicy] = {}
        for raw in raw_tools:
            if not isinstance(raw, dict) or not isinstance(raw.get("schema"), dict) or not isinstance(raw.get("policy"), dict):
                raise SessionError("hello tool entries require schema and policy objects")
            schema = dict(raw["schema"])
            name = _bounded_string(schema.get("name"), "tool schema name")
            policy = ToolPolicy.from_json(raw["policy"])
            if policy.name != name:
                raise SessionError(f"tool schema/policy name mismatch: {name} vs {policy.name}")
            if name in schemas:
                raise SessionError(f"duplicate advertised tool: {name}")
            schemas[name] = schema
            policies[name] = policy
        self.hello = SessionHello(editor_id, adapter_id, revision, trust_mode, schemas, policies)
        return self.hello

    def inspect(
        self,
        adapter: AdapterClient,
        operation: str,
        input: Mapping[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Run one advertised read-only adapter inspection and refresh revision.

        Inspection is deliberately disabled while an authorization is active.
        If current editor revision changed since hello, any merely prepared plan
        is discarded because its immutable base_revision is no longer current.
        """
        hello = self._require_hello()
        if self.authorization_id is not None or (self.gate is not None and self.gate.authorized):
            raise SessionError("cannot refresh inspection state while a plan authorization is active")
        name = _bounded_string(operation, "inspection operation")
        policy = hello.policies.get(name)
        if policy is None or not policy.enabled:
            raise SessionError(f"inspection tool is unavailable: {name}")
        if policy.risk is not ToolRisk.READ_ONLY or policy.mutates_project:
            raise SessionError(f"inspect may use advertised read-only tools only: {name}")
        payload_input = dict(input or {})
        response = self._exchange(
            adapter,
            request("inspect", {"operation": name, "input": payload_input}),
        )
        payload = self._success_payload(response, "inspect")
        if payload.get("operation") != name:
            raise SessionError("adapter inspection response names a different operation")
        revision = _revision(payload.get("project_revision"), "project_revision")
        result = payload.get("result")
        if not isinstance(result, dict):
            raise SessionError("inspection response requires structured adapter state evidence")
        if revision != hello.project_revision:
            self.hello = replace(hello, project_revision=revision)
            if self.plan is not None and self.plan.base_revision != revision:
                self.plan = None
                self.execution_order = ()
                self.gate = None
                self.approved_operation_ids = frozenset()
        return dict(payload)

    def prepare_plan(self, value: EditPlan | Mapping[str, Any]) -> EditPlan:
        hello = self._require_hello()
        plan = value if isinstance(value, EditPlan) else EditPlan.from_json(value)
        order = validate_plan(plan)
        if plan.base_revision != hello.project_revision:
            raise SessionError(
                f"plan base_revision {plan.base_revision} does not match hello revision {hello.project_revision}"
            )
        has_effect = False
        for operation in plan.operations:
            policy = hello.policies.get(operation.tool)
            if policy is None or not policy.enabled:
                raise SessionError(f"plan references unavailable tool: {operation.tool}")
            if policy.risk is not ToolRisk.READ_ONLY:
                has_effect = True
        if not has_effect:
            raise SessionError("read-only inspection does not require a governed EditPlan")
        self.plan = plan
        self.execution_order = order
        self.authorization_id = None
        self.approved_operation_ids = frozenset()
        self.gate = RevisionGate(plan.base_revision)
        return plan

    def requires_confirmation(self, mode: TrustMode | None = None) -> bool:
        hello = self._require_hello()
        plan = self._require_plan()
        selected_mode = mode or hello.trust_mode
        return plan_requires_confirmation(
            tuple(operation.tool for operation in plan.operations), hello.policies, selected_mode
        )

    def submit_plan(self, adapter: AdapterClient) -> dict[str, Any]:
        plan = self._require_plan()
        response = self._exchange(adapter, request("propose_plan", plan.to_json()))
        payload = self._success_payload(response, "propose_plan")
        if payload.get("plan_id") != plan.id:
            raise SessionError("adapter acknowledged a different plan id")
        if payload.get("base_revision") != plan.base_revision:
            raise SessionError("adapter acknowledged a different plan base revision")
        return payload

    def accept_authorization(self, envelope: Envelope | Mapping[str, Any]) -> dict[str, Any]:
        plan = self._require_plan()
        message = envelope if isinstance(envelope, Envelope) else Envelope.from_json(envelope)
        if message.type == "error":
            raise SessionError(_error_message(message))
        if message.type != "authorize" or message.kind not in {"event", "response"}:
            raise SessionError("expected authorize event/response")
        payload = message.payload
        if payload.get("plan_id") != plan.id:
            raise SessionError("authorization targets a different plan")
        decision = payload.get("decision")
        if decision == "rejected":
            self._clear_authorization()
            raise SessionError(f"plan authorization rejected: {payload.get('reason', '')}")
        if decision != "approved":
            raise SessionError("authorize decision must be approved or rejected")
        authorization_id = _bounded_string(payload.get("authorization_id"), "authorization_id")
        expected_revision = _revision(payload.get("expected_revision"), "expected_revision")
        approved = payload.get("approved_operation_ids")
        if not isinstance(approved, list) or not approved:
            raise SessionError("approved authorization requires operation ids")
        approved_ids = [_bounded_string(raw, "approved operation id") for raw in approved]
        if len(set(approved_ids)) != len(approved_ids):
            raise SessionError("approved operation ids must be unique")
        expected_ids = set(self.execution_order)
        if set(approved_ids) != expected_ids:
            raise SessionError("protocol v1 authorization must cover the exact validated plan operations")
        gate = RevisionGate(plan.base_revision)
        gate.authorize(expected_revision)
        self.gate = gate
        self.authorization_id = authorization_id
        self.approved_operation_ids = frozenset(approved_ids)
        return dict(payload)

    def execute_authorized(
        self,
        adapter: AdapterClient,
        *,
        verification_inspection: str = "project_snapshot",
        verification_input: Mapping[str, Any] | None = None,
    ) -> ExecutionResult:
        hello = self._require_hello()
        plan = self._require_plan()
        authorization_id = self.authorization_id
        gate = self.gate
        if not authorization_id or gate is None or not gate.authorized:
            raise SessionError("plan has not been authorized")
        operation_results: list[dict[str, Any]] = []
        verification_results: list[dict[str, Any]] = []
        try:
            for operation_id in self.execution_order:
                operation = plan.operation(operation_id)
                if operation_id not in self.approved_operation_ids:
                    raise SessionError(f"operation is not authorized: {operation_id}")
                policy = hello.policies.get(operation.tool)
                if policy is None or not policy.enabled:
                    raise SessionError(f"authorized tool disappeared from runtime snapshot: {operation.tool}")
                expected_revision = gate.expected_revision
                if expected_revision is None:
                    raise SessionError("moving revision gate is not active")
                invoke_response = self._exchange(
                    adapter,
                    request(
                        "invoke",
                        {
                            "plan_id": plan.id,
                            "authorization_id": authorization_id,
                            "operation_id": operation_id,
                            "expected_revision": expected_revision,
                        },
                    ),
                )
                invoke_payload = self._success_payload(invoke_response, "invoke")
                if invoke_payload.get("plan_id") != plan.id or invoke_payload.get("operation_id") != operation_id:
                    raise SessionError("adapter invoke response does not match requested operation")
                before = _revision(invoke_payload.get("revision_before"), "revision_before")
                after = _revision(invoke_payload.get("revision_after"), "revision_after")
                gate.advance(before, after)
                if not isinstance(invoke_payload.get("result"), dict):
                    raise SessionError("invoke response requires structured result evidence")
                operation_results.append(dict(invoke_payload))

                if invoke_payload.get("started") is True:
                    job_id = _bounded_string(invoke_payload.get("job_id"), "job_id")
                    self._wait_for_job(adapter, job_id, gate, mutates_project=policy.mutates_project)

                expected_revision = gate.expected_revision
                if expected_revision is None:
                    raise SessionError("moving revision gate unexpectedly closed")
                verify_response = self._exchange(
                    adapter,
                    request(
                        "verify",
                        {
                            "plan_id": plan.id,
                            "authorization_id": authorization_id,
                            "operation_id": operation_id,
                            "expected_revision": expected_revision,
                            "expected_postconditions": list(operation.expected_postconditions),
                            "inspection": verification_inspection,
                            "inspection_input": dict(verification_input or {}),
                        },
                    ),
                )
                verify_payload = self._success_payload(verify_response, "verify")
                if verify_payload.get("plan_id") != plan.id or verify_payload.get("operation_id") != operation_id:
                    raise SessionError("verification response does not match requested operation")
                verified_revision = _revision(verify_payload.get("project_revision"), "project_revision")
                gate.require_current(verified_revision)
                if not isinstance(verify_payload.get("inspection_result"), dict):
                    raise SessionError("verification must contain adapter inspection evidence")
                verification_results.append(dict(verify_payload))

            expected_revision = gate.expected_revision
            if expected_revision is None:
                raise SessionError("moving revision gate unexpectedly closed")
            complete_response = self._exchange(
                adapter,
                request(
                    "complete_plan",
                    {
                        "plan_id": plan.id,
                        "authorization_id": authorization_id,
                        "expected_revision": expected_revision,
                    },
                ),
            )
            completion = self._success_payload(complete_response, "complete_plan")
            final_revision = _revision(completion.get("project_revision"), "project_revision")

            # Closing a Kdenlive Undo macro is adapter-owned state transition. A
            # synchronous mutating plan may therefore publish a newer revision at
            # complete_plan even though every operation/verification correctly ran
            # against the prior moving token. Accept only an explicitly declared
            # committed checkpoint and only a monotonic advance; every other
            # completion revision mismatch remains stale/fail-closed.
            current_expected = gate.expected_revision
            if current_expected is None:
                raise SessionError("moving revision gate unexpectedly closed at completion")
            if final_revision != current_expected:
                if (
                    completion.get("checkpoint_committed") is True
                    and completion.get("checkpoint_rollback_parity") is True
                    and final_revision > current_expected
                ):
                    gate.advance(current_expected, final_revision)
                else:
                    gate.require_current(final_revision)
            else:
                gate.require_current(final_revision)

            gate.close()
            self.authorization_id = None
            self.approved_operation_ids = frozenset()
            return ExecutionResult(
                plan_id=plan.id,
                final_revision=final_revision,
                operation_results=tuple(operation_results),
                verification_results=tuple(verification_results),
                completion=dict(completion),
            )
        except Exception as exc:
            self._abort_best_effort(adapter, str(exc))
            raise

    def _wait_for_job(
        self,
        adapter: AdapterClient,
        job_id: str,
        gate: RevisionGate,
        *,
        mutates_project: bool,
    ) -> None:
        for _ in range(100_000):
            event = adapter.next_event()
            if event is None:
                raise SessionError(f"adapter supplied no terminal event for job {job_id}")
            if event.type == "error":
                raise SessionError(_error_message(event))
            if event.type == "revision":
                revision = _revision(event.payload.get("project_revision"), "project_revision")
                gate.require_current(revision)
                continue
            if event.type != "job_update":
                continue
            job = event.payload.get("job")
            if not isinstance(job, dict) or job.get("id") != job_id:
                continue
            revision = _revision(event.payload.get("project_revision"), "project_revision")
            expected = gate.expected_revision
            if expected is None:
                raise SessionError("moving revision gate unexpectedly closed")
            if revision != expected:
                if mutates_project and revision > expected:
                    gate.advance(expected, revision)
                else:
                    gate.require_current(revision)
            state = job.get("state")
            if state == "succeeded":
                return
            if state in {"failed", "cancelled"}:
                raise SessionError(f"background job {job_id} ended as {state}: {job.get('message', '')}")
        raise SessionError(f"job event bound exceeded for {job_id}")

    def _abort_best_effort(self, adapter: AdapterClient, reason: str) -> None:
        plan = self.plan
        if plan is None:
            return
        base_payload: dict[str, Any] = {
            "plan_id": plan.id,
            "reason": reason[:16_384] or "runtime aborted",
        }
        attempts: list[dict[str, Any]] = []
        if self.authorization_id:
            with_auth = dict(base_payload)
            with_auth["authorization_id"] = self.authorization_id
            attempts.append(with_auth)
        attempts.append(base_payload)
        for payload in attempts:
            try:
                response = adapter.exchange(request("abort_plan", payload))
                if isinstance(response, Envelope) and response.type != "error":
                    break
            except Exception:
                continue
        self._clear_authorization()

    def _clear_authorization(self) -> None:
        if self.gate is not None:
            self.gate.close()
        self.authorization_id = None
        self.approved_operation_ids = frozenset()

    @staticmethod
    def _exchange(adapter: AdapterClient, message: Envelope) -> Envelope:
        response = adapter.exchange(message)
        if not isinstance(response, Envelope):
            raise SessionError("adapter client must return an Envelope")
        if response.id != message.id:
            raise SessionError("adapter response correlation id mismatch")
        return response

    @staticmethod
    def _success_payload(response: Envelope, expected_type: str) -> dict[str, Any]:
        if response.type == "error":
            raise SessionError(_error_message(response))
        if response.kind != "response" or response.type != expected_type:
            raise SessionError(f"expected {expected_type} response")
        if response.payload.get("ok") is not True:
            raise SessionError(
                str(response.payload.get("error") or response.payload.get("message") or f"{expected_type} failed")
            )
        return dict(response.payload)

    def _require_hello(self) -> SessionHello:
        if self.hello is None:
            raise SessionError("adapter hello has not been accepted")
        return self.hello

    def _require_plan(self) -> EditPlan:
        if self.plan is None:
            raise SessionError("no prepared plan")
        return self.plan


def _error_message(message: Envelope) -> str:
    code = message.payload.get("code", "adapter_error")
    detail = message.payload.get("message", "adapter returned an error")
    return f"{code}: {detail}"


def _bounded_string(raw: Any, label: str) -> str:
    if not isinstance(raw, str) or not raw.strip() or len(raw) > 16_384:
        raise SessionError(f"{label} must be a non-empty bounded string")
    return raw.strip()


def _revision(raw: Any, label: str) -> int:
    if isinstance(raw, bool) or not isinstance(raw, int) or raw < 0 or raw > 9_007_199_254_740_991:
        raise SessionError(f"{label} must be an exact non-negative JSON integer")
    return raw
