# VibeCut Runtime Protocol v1

SPDX-License-Identifier: Apache-2.0

This document defines the editor-agnostic protocol seam described by `EXTRACT_AND_LICENSE.md`.

The protocol is intentionally usable without linking the runtime implementation into Kdenlive, MLT, KF6, or the GPL VibeCut adapter. Kdenlive remains authoritative editor/project state. The runtime may inspect, reason, propose, govern, and orchestrate; it does not become a second editor database.

## 1. Transport

Version 1 transport is newline-delimited JSON (NDJSON) over stdin/stdout.

- one UTF-8 JSON object per line;
- no multi-line JSON objects;
- stdout is protocol-only;
- diagnostics go to stderr;
- implementations must impose bounded message sizes;
- local Unix sockets / named pipes may carry the same envelopes later without changing the message model.

## 2. Envelope

Every protocol message uses this envelope:

```json
{
  "v": 1,
  "id": "msg-...",
  "kind": "request",
  "type": "hello",
  "payload": {}
}
```

Fields:

- `v` — protocol version. Version 1 is required by this document.
- `id` — caller-generated message/correlation id. Responses reuse the request id.
- `kind` — `request`, `response`, or `event`.
- `type` — message type from the catalog below.
- `payload` — type-specific JSON object.

Unknown protocol versions fail closed. Unknown required message types fail with a structured `error` response.

## 3. Authority model

Protocol authority is deliberately split:

1. **Adapter / Kdenlive authority** — current project state, current revision, native tool availability, native mutation result, Undo/Redo behavior, render/job state.
2. **Runtime authority** — contract validation, planning, orchestration, trust-policy evaluation, evidence bookkeeping, provider-neutral model requests.
3. **Human authority** — authorization when required by tool policy/trust mode.
4. **Evidence authority** — evidence records are observations, measurements, predictions, representations, or derived candidates according to their own provenance. Repetition or confidence never promotes evidence into editor truth.

A runtime request cannot self-elevate from proposal authority into mutation authority. Consequential editor changes happen only through adapter-exposed native tools after authorization.

## 4. Revision and authorization model

Version 1 distinguishes two revision values:

- `base_revision` — immutable provenance captured when the plan is proposed. It binds the plan to the state it was reasoned from.
- `expected_revision` — moving execution token. It starts at the adapter revision when authorization is granted and is replaced by each successful adapter response/event that legitimately advances project state.

This distinction mirrors the current integrated `VibeCutPlanRuntime`: an approved plan is stale if the project changes before execution, but operation 1 may legitimately advance the revision before operation 2.

Rules:

1. Adapter stores the exact accepted `propose_plan` object immutably for the pending authorization.
2. `authorize` binds a fresh opaque `authorization_id` to that stored plan and its approved operation ids.
3. `invoke` references only `plan_id`, `authorization_id`, `operation_id`, and `expected_revision`.
4. The adapter resolves the tool name/input from the stored approved plan. The runtime may **not** substitute a new tool or new JSON input after approval.
5. Before each invoke, adapter requires current project revision == `expected_revision`.
6. A successful response returns `revision_before` and `revision_after`; runtime uses `revision_after` as the next `expected_revision`.
7. If an external-only asynchronous job is running and project state changes before remaining operations, the remaining plan is stale and must stop.
8. `complete_plan` explicitly releases a successful authorization after all approved operations have completed and the expected revision still matches.
9. `abort_plan` explicitly stops a pending/authorized plan. Initial shim behavior invalidates authorization; Step 4 adds plan-wide checkpoint rollback parity.
10. A rejected/stale/completed/aborted authorization id cannot be reused.

This prevents stale-plan execution, post-approval plan substitution, and ambiguous authorization lifetime.

## 5. Required message types

### `hello`

Direction: adapter → runtime event/request during session initialization.

Minimum payload:

```json
{
  "editor_id": "kdenlive",
  "adapter_id": "halthinks-vibecut-adapter",
  "protocol_versions": [1],
  "project_revision": 42,
  "trust_mode": "off",
  "tools": [
    {
      "schema": {"name": "clip_move", "description": "...", "input_schema": {}},
      "policy": {"name": "clip_move", "risk": "reversible_edit", "reversible": true, "mutates_project": true, "async": false, "confirmation_required": false, "auto_allowed": false, "enabled": true}
    }
  ]
}
```

The tool table is authoritative for that adapter session. The runtime must not invent unavailable tool names.

### `inspect`

Direction: runtime → adapter request.

Payload names a read-only advertised tool and JSON input. The adapter returns current editor-derived state plus the revision token used for the inspection.

```json
{"operation":"project_snapshot","input":{}}
```

### `propose_plan`

Direction: runtime → adapter request/event for review.

Payload contains one object conforming to `schema/editplan.schema.json`. `base_revision` binds the plan to inspected state. The adapter stores the accepted object immutably for the pending authorization and refuses a conflicting reuse of the same plan id.

### `authorize`

Direction: adapter/human → runtime response/event.

Approved response payload:

```json
{
  "plan_id": "plan-...",
  "decision": "approved",
  "trust_mode": "off",
  "authorization_id": "auth-...",
  "expected_revision": 42,
  "approved_operation_ids": ["op-1"]
}
```

Rejected response omits `authorization_id` and contains a reason. Version 1 never infers approval from silence where policy requires a human decision.

### `invoke`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "operation_id": "op-1",
  "expected_revision": 42
}
```

The adapter resolves `tool` and `input` from the exact stored approved plan; neither is caller-overridable at invoke time.

Successful synchronous response includes:

```json
{
  "plan_id": "plan-...",
  "operation_id": "op-1",
  "revision_before": 42,
  "revision_after": 43,
  "result": {"ok": true}
}
```

For asynchronous work, response additionally exposes `started: true` and a trackable `job_id`. Adapter rechecks authorization, moving expected revision, effective policy, dependencies, and native preconditions at execution time.

### `verify`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "operation_id": "op-1",
  "expected_revision": 43,
  "expected_postconditions": ["..."],
  "inspection": "project_snapshot",
  "inspection_input": {}
}
```

`inspection` must name an advertised read-only adapter tool. The adapter returns its measured/native state together with the expected-postcondition strings. Generic strings are not magically interpreted as truth by the adapter. `ok: true` without supporting state/evidence is not sufficient verification.

### `complete_plan`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "expected_revision": 43
}
```

The adapter accepts completion only when every approved operation is terminal-successful, no tracked background operation remains, and current revision equals `expected_revision`. It then invalidates the authorization. Step 4 uses this lifecycle boundary to close a plan-wide Undo checkpoint/macro.

### `abort_plan`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "reason": "runtime stopped after failed verification"
}
```

A pending plan may omit `authorization_id`. Abort invalidates pending/authorization state. Initial Step-2 shim does not claim plan-wide rollback parity; Step 4 adds adapter-side checkpoint rollback at this boundary.

### `job_update`

Direction: adapter → runtime event.

Payload mirrors the public job schema and includes current `project_revision` when revision-sensitive plan continuation is possible. Successful terminal events may carry bounded structured result data.

### `revision`

Direction: adapter → runtime event.

```json
{"project_revision":43,"reason":"undo_stack_changed"}
```

Revision tokens are opaque monotonic adapter state. Runtime-owned successful operations advance the moving expected token; unrelated changes invalidate remaining work.

### `evidence_put`

Direction: runtime/adapter → evidence-store owner request.

Payload contains one or more records conforming to `schema/evidence.schema.json`. Version 1 adapter persistence may require all records in one put to share the same source id/fingerprint/extractor id/version so replacement remains one canonical evidence slice. Evidence persistence never mutates Kdenlive project truth by itself.

### `evidence_get`

Direction: runtime → evidence-store owner request.

Payload contains bounded filters such as source id/fingerprint, extractor id/version, kind, and frame range. Results preserve original provenance.

### `error`

Direction: either side → response/event.

Minimum payload:

```json
{
  "code": "stale_revision",
  "message": "expected_revision no longer matches current project revision.",
  "retryable": false,
  "details": {}
}
```

Errors never masquerade as successful responses.

## 6. Plan contract

The canonical serialized plan field names mirror current `VibeCutEditPlan::toJson()` / `VibeCutPlanOperation::toJson()`:

- plan: `id`, `base_revision`, `objective`, `operations`;
- operation: `id`, `tool`, `input`, `depends_on`, `expected_postconditions`.

JSON Schema validates shape. Semantic validation additionally requires:

- non-empty plan id/objective;
- at least one operation;
- unique non-empty operation ids;
- non-empty tool names;
- every dependency references a known operation;
- no self-dependency;
- dependency graph is acyclic;
- every tool is advertised and governed by the active adapter snapshot;
- plan `base_revision` equals adapter revision when authorization is granted;
- after authorization, moving `expected_revision` — not immutable `base_revision` — guards each operation.

## 7. Trust/policy contract

The public policy serialization mirrors `VibeCutToolPolicy::toJson()` exactly:

- `name`
- `risk`: `read_only | reversible_edit | major_edit | external_side_effect | irreversible`
- `reversible`
- `mutates_project`
- `async`
- `confirmation_required`
- `auto_allowed`
- `enabled`

Trust modes map current C++ names as:

- C++ `Off` → protocol `off` (Review behavior)
- C++ `Auto` → protocol `auto`
- C++ `Turbo` → protocol `turbo`

Code/adapter-defined hard confirmation remains a lower bound. Runtime or project policy must not waive `confirmation_required=true` or irreversible confirmation.

## 8. Job contract

Job states mirror the current JobManager state machine:

`queued → running → {succeeded | failed | cancelled}`

with `cancel_requested` as an explicit intermediate state for cancelable jobs.

Fields are defined in `schema/job.schema.json`. Structured successful results remain bounded. Failed/cancelled jobs do not claim a successful result.

## 9. Evidence contract

The public record shape mirrors `VibeCutMediaEvidenceRecord::toJson()` exactly. See `schema/evidence.schema.json`.

Important invariants:

- source identity and source fingerprint are distinct and both required;
- extractor id/version are required;
- confidence is `-1` for unknown or within `[0,1]`;
- frame ranges use `-1` for unavailable, otherwise non-negative coordinates with end ≥ start;
- evidence is provenance-bound and does not become project truth automatically.

## 10. Compatibility/versioning

- Additive optional fields may be introduced only when old peers can safely ignore them.
- Renaming/removing fields or changing semantics requires a new protocol/schema version.
- Adapter `hello` advertises supported versions.
- No side may silently downgrade security/authority semantics to achieve compatibility.

## 11. Initial extraction acceptance

The first extracted runtime is protocol-valid only when a fake adapter can demonstrate:

1. hello/tool discovery;
2. inspect current revision;
3. propose and validate an EditPlan;
4. require authorization according to policy;
5. reject stale `base_revision` before authorization;
6. issue an authorization id bound to the exact stored plan;
7. reject post-approval tool/input substitution by resolving operation content adapter-side;
8. reject unexpected revision changes while accepting revision changes caused by prior approved operations;
9. invoke only advertised and approved tools;
10. verify postconditions from adapter state;
11. complete an all-success plan and invalidate its authorization;
12. abort a pending/authorized plan and invalidate its authorization;
13. receive job lifecycle events and stop remaining work when an external-only job observes project drift;
14. persist/retrieve evidence without converting it into editor truth.
