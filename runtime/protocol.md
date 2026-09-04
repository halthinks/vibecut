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

## 4. Required message types

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

Payload names an inspection operation and JSON input. The adapter returns current editor-derived state plus the revision token used for the inspection.

```json
{"operation":"project_snapshot","input":{}}
```

### `propose_plan`

Direction: runtime → adapter request/event for review.

Payload contains one object conforming to `schema/editplan.schema.json`. `base_revision` binds the plan to inspected state.

### `authorize`

Direction: adapter/human → runtime response/event.

Minimum payload:

```json
{
  "plan_id": "plan-...",
  "decision": "approved",
  "trust_mode": "off",
  "approved_operation_ids": ["op-1"]
}
```

`decision` is `approved` or `rejected`. A later protocol version may add scoped approval, but v1 must never infer approval from silence.

### `invoke`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "operation_id": "op-1",
  "base_revision": 42,
  "tool": "clip_move",
  "input": {}
}
```

The adapter rechecks current revision, tool availability, policy, authorization, and native preconditions at execution time. Runtime validation is not a substitute for adapter validation.

### `verify`

Direction: runtime → adapter request.

```json
{
  "plan_id": "plan-...",
  "operation_id": "op-1",
  "expected_postconditions": ["..."],
  "inspection": "project_snapshot"
}
```

The adapter returns measured/native postcondition evidence. `ok: true` without supporting state/evidence is not sufficient verification.

### `job_update`

Direction: adapter → runtime event.

Payload mirrors the public job schema and may be emitted for queued/running/cancel-requested/terminal state changes.

### `revision`

Direction: adapter → runtime event.

```json
{"project_revision":43,"reason":"undo_stack_changed"}
```

Revision tokens are opaque monotonic adapter state. A stale `base_revision` must be refused before mutation.

### `evidence_put`

Direction: runtime/adapter → evidence-store owner request.

Payload contains one or more records conforming to `schema/evidence.schema.json`. Evidence persistence never mutates Kdenlive project truth by itself.

### `evidence_get`

Direction: runtime → evidence-store owner request.

Payload contains bounded filters such as source id/fingerprint, extractor id/version, kind, and frame range. Results preserve original provenance.

### `error`

Direction: either side → response/event.

Minimum payload:

```json
{
  "code": "stale_revision",
  "message": "Plan base_revision no longer matches current project revision.",
  "retryable": false,
  "details": {}
}
```

Errors never masquerade as successful responses.

## 5. Plan contract

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
- plan `base_revision` equals current adapter revision at authorization/execution time.

## 6. Trust/policy contract

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

## 7. Job contract

Job states mirror the current JobManager state machine:

`queued → running → {succeeded | failed | cancelled}`

with `cancel_requested` as an explicit intermediate state for cancelable jobs.

Fields are defined in `schema/job.schema.json`. Structured successful results remain bounded. Failed/cancelled jobs do not claim a successful result.

## 8. Evidence contract

The public record shape mirrors `VibeCutMediaEvidenceRecord::toJson()` exactly. See `schema/evidence.schema.json`.

Important invariants:

- source identity and source fingerprint are distinct and both required;
- extractor id/version are required;
- confidence is `-1` for unknown or within `[0,1]`;
- frame ranges use `-1` for unavailable, otherwise non-negative coordinates with end ≥ start;
- evidence is provenance-bound and does not become project truth automatically.

## 9. Compatibility/versioning

- Additive optional fields may be introduced only when old peers can safely ignore them.
- Renaming/removing fields or changing semantics requires a new protocol/schema version.
- Adapter `hello` advertises supported versions.
- No side may silently downgrade security/authority semantics to achieve compatibility.

## 10. Initial extraction acceptance

The first extracted runtime is protocol-valid only when a fake adapter can demonstrate:

1. hello/tool discovery;
2. inspect current revision;
3. propose and validate an EditPlan;
4. require authorization according to policy;
5. reject stale revision before invoke;
6. invoke only advertised tools;
7. verify postconditions from adapter state;
8. receive job lifecycle events;
9. persist/retrieve evidence without converting it into editor truth.
