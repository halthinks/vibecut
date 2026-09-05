# VibeCut Runtime Protocol v1

SPDX-License-Identifier: Apache-2.0

This document defines the editor-agnostic process seam described by `EXTRACT_AND_LICENSE.md`.

Kdenlive remains authoritative editor/project state. The proprietary runtime may inspect, reason, propose, govern and orchestrate, but it does not link into Kdenlive and does not become a second editor database.

## 1. Production topology and transport

The first production topology is:

```text
GPL Kdenlive / VibeCut adapter (parent process)
        │
        │ launches
        ▼
proprietary halthinks runtime (child process)
```

The GPL editor owns live Kdenlive state, `VibeCutToolSurface`, native commands, `DocUndoStack`, job state and human authorization UI. The proprietary child owns editor-agnostic planning/policy/protocol orchestration only.

Version 1 uses newline-delimited JSON (NDJSON) over inherited stdin/stdout:

- GPL parent → runtime child stdin: `hello`, responses and authorized adapter events;
- runtime child → GPL parent: protocol requests only;
- runtime diagnostics go to stderr, never stdout;
- one UTF-8 JSON object per line;
- no multi-line JSON objects;
- **maximum encoded JSON record size is 2 MiB**, excluding the terminating newline, on both sides;
- malformed/oversized protocol output fails closed and invalidates active plan authority;
- the editor launches the runtime directly; no shell is used.

`StdioAdapterClient` also supports the reverse topology (runtime launches a fake/OEM adapter process) for testing and non-Kdenlive hosts. A standalone helper process is **not** the production owner of live Kdenlive state.

Local Unix sockets / named pipes may carry the same envelopes later without changing v1 authority semantics.

## 2. Envelope

Every message uses:

```json
{
  "v": 1,
  "id": "msg-...",
  "kind": "request",
  "type": "inspect",
  "payload": {}
}
```

- `v` — protocol version; v1 only here.
- `id` — bounded correlation id. Responses reuse the request id.
- `kind` — `request`, `response`, or `event`.
- `type` — message type below.
- `payload` — type-specific object.

Unknown versions/types fail closed. Public schemas live under `runtime/schema/`.

## 3. Authority model

1. **GPL adapter / Kdenlive authority** — live project state, current revision, available tools/policies, native mutation result, Undo/Redo checkpoint behavior, jobs and editor verification.
2. **Runtime authority** — public-contract validation, planning, orchestration, moving revision bookkeeping, evidence bookkeeping and provider-neutral model requests.
3. **Human authority** — authorization where effective policy/trust mode requires it.
4. **Evidence authority** — provenance-bound observation/measurement/prediction/representation/derived evidence only. Evidence persistence never mutates Kdenlive project truth.

Runtime proposal authority can never self-elevate into mutation authority.

## 4. Revision, authorization and exact-plan binding

V1 distinguishes:

- `base_revision` — immutable plan provenance captured from inspected state;
- `expected_revision` — moving execution token after authorization.

Rules:

1. GPL adapter stores the exact accepted `propose_plan` object.
2. Authorization binds a fresh opaque `authorization_id` to that exact plan and exact operation set.
3. `invoke` carries only `plan_id`, `authorization_id`, `operation_id`, `expected_revision`.
4. `invoke` **must not contain `tool` or `input`**. The GPL adapter resolves both from the stored approved plan.
5. Before opening a Kdenlive Undo checkpoint, the GPL transport performs adapter-side preflight: authorization, moving revision, dependency order, duplicate/completed state and authorization-time/current policy equality.
6. The actual adapter repeats the full validation immediately before native invocation.
7. A successful operation returns `revision_before` / `revision_after`; runtime advances its moving token.
8. When the GPL checkpoint layer closes an Undo macro and Kdenlive publishes the resulting revision, the adapter resynchronizes `expected_revision` from editor-authoritative state.
9. External-only async work cannot absorb unrelated project drift. If project state changes while such a job is active and plan work remains, the plan becomes stale.
10. Rejected, stale, completed or aborted authorizations are not reusable.

This prevents stale execution, post-approval substitution and ambiguous authorization lifetime.

## 5. Adapter-side Undo/checkpoint semantics

Out-of-process execution intentionally mirrors the existing integrated `VibeCutPlanRuntime` scope; it does **not** claim stronger all-plan atomic rollback.

- consecutive synchronous `mutates_project=true` operations share one open Kdenlive Undo macro;
- read-only verification may occur while that macro remains open;
- the macro is committed before an asynchronous operation begins;
- later synchronous mutations after async open a new checkpoint;
- a failed synchronous native mutation rolls back the **currently open synchronous macro only**;
- `abort_plan` rolls back the currently open synchronous macro if one exists;
- already committed checkpoints (for example those closed before async) are not retroactively claimed as rolled back;
- successful `complete_plan` commits any currently open macro;
- post-commit revision is re-read from Kdenlive before it is returned as authoritative completion state.

Source support exists in the GPL adapter/transport (`VibeCutRuntimeCheckpoint` + stdio transport), but release-quality parity remains gated on the real Kdenlive compile/runtime/Undo smoke suite.

## 6. Message catalog

### `hello`

Adapter → runtime initialization event.

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
      "policy": {
        "name": "clip_move",
        "risk": "reversible_edit",
        "reversible": true,
        "mutates_project": true,
        "async": false,
        "confirmation_required": false,
        "auto_allowed": false,
        "enabled": true
      }
    }
  ]
}
```

The table comes from the live effective `VibeCutToolSurface` snapshot. Runtime must not invent absent tools.

### `inspect`

Runtime → adapter read-only request:

```json
{"operation":"project_snapshot","input":{}}
```

`operation` must be an advertised effective `read_only` tool. Response includes the editor revision measured with that inspection. The clean-room runtime can refresh its planning revision from this result; inspection is refused during active authorization.

### `propose_plan`

Runtime → adapter request containing `schema/editplan.schema.json`.

The adapter requires current revision == `base_revision`, validates tools/dependencies, stores the accepted plan immutably and refuses a second simultaneous protocol plan.

### `authorize`

Adapter/human → runtime event/response.

Approved payload:

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

Human approval is never inferred when effective policy requires confirmation. Code-defined hard confirmation remains non-waivable even in Turbo.

### `invoke`

Runtime → adapter:

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "operation_id": "op-1",
  "expected_revision": 42
}
```

No runtime-supplied tool/input is accepted. Adapter resolves exact operation content.

A synchronous successful response includes structured native result plus revisions. Async start additionally includes `started: true` + `job_id`. For a mutating operation the GPL transport owns checkpoint begin/commit/rollback; runtime never controls that metadata.

### `verify`

Runtime → adapter:

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

`inspection` must be an advertised read-only tool. Returned editor state is verification evidence; free-form postcondition strings are not automatically converted into truth.

### `complete_plan`

Runtime → adapter after every approved operation is terminal-successful and no tracked job remains:

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "expected_revision": 43
}
```

Production GPL transport commits any open synchronous checkpoint, returns editor-authoritative post-commit revision and invalidates plan authorization.

### `abort_plan`

Runtime → adapter:

```json
{
  "plan_id": "plan-...",
  "authorization_id": "auth-...",
  "reason": "failed verification"
}
```

A pending unapproved plan may omit `authorization_id`. Production transport requests cancellation of tracked cancellable jobs and rolls back only the currently open synchronous checkpoint, matching integrated runtime scope.

### `job_update`

Adapter → runtime event for a job **owned by the active protocol plan only**.

Unrelated Kdenlive/Whisper/render/model/editor jobs are filtered at the process boundary and are not exported to the proprietary child. Payload mirrors `schema/job.schema.json` plus current project revision.

### `revision`

Adapter → runtime event:

```json
{"project_revision":43,"reason":"undo_stack_changed"}
```

A generic revision event is not automatically attributable to an active job; unrelated drift fails the moving revision gate.

### `evidence_put` / `evidence_get`

Evidence records follow `schema/evidence.schema.json`.

V1 confidence semantics are exact:

- `-1` = unknown;
- otherwise value is in `[0,1]`;
- values such as `-0.5` are invalid.

Frame range is `-1/-1` when unavailable, otherwise non-negative and `end_frame >= start_frame`. A bounded frame query does not claim an unknown-range record intersects the requested range.

Evidence persistence is not editor mutation and cannot become project truth by repetition/confidence.

### `error`

Structured failure:

```json
{
  "code": "stale_revision",
  "message": "expected_revision no longer matches current project revision.",
  "retryable": false,
  "details": {}
}
```

Errors never masquerade as success.

## 7. EditPlan contract

Serialized names remain exactly:

- plan: `id`, `base_revision`, `objective`, `operations`;
- operation: `id`, `tool`, `input`, `depends_on`, `expected_postconditions`.

Semantic validation requires non-empty unique ids/tool names, known dependencies, no self-dependency/cycles, advertised effective tools, and correct revision authority.

## 8. Tool policy / trust contract

Public policy fields:

- `name`
- `risk`: `read_only | reversible_edit | major_edit | external_side_effect | irreversible`
- `reversible`
- `mutates_project`
- `async`
- `confirmation_required`
- `auto_allowed`
- `enabled`

Trust modes:

- C++ `Off` → protocol `off` (Review)
- `Auto` → `auto`
- `Turbo` → `turbo`

Hard confirmation and irreversible confirmation remain adapter-enforced lower bounds.

## 9. Job contract

States:

`queued → running → {succeeded | failed | cancelled}`

with `cancel_requested` as explicit intermediate state. Successful structured results remain bounded. Failed/cancelled jobs do not retain a claimed successful result.

## 10. Model-provider transport boundary

The clean-room runtime provider client is editor-independent. Default remote provider requests require HTTPS; cleartext HTTP is permitted only for loopback/local development endpoints. URL-embedded credentials and URL fragments are rejected before transport. Provider events/request bodies are independently bounded.

## 11. Compatibility/versioning

- Additive optional fields require old peers to safely ignore them.
- Rename/removal/semantic changes require a protocol version bump.
- `hello` advertises supported versions.
- Security/authority semantics are never silently downgraded for compatibility.

## 12. Current acceptance gate

The runtime is not commercially release-qualified until all of these are true:

1. `python3 runtime/verify.py` passes from the exact source tree;
2. runtime source passes the executable clean-room boundary scan and has no Kdenlive/MLT/KF6/Qt/GPL implementation dependency;
3. fake/OEM subprocess tests pass;
4. production-direction test passes with GPL-style parent and proprietary runtime child over inherited stdio;
5. hello → read-only inspect → plan → authorize → invoke → verify → complete passes;
6. stale base/moving revision and post-approval substitution fail before native mutation;
7. async job events are exported only for protocol-owned jobs;
8. evidence-store parity and non-project-truth semantics pass;
9. GPL adapter compiles/links in Kdenlive;
10. live Kdenlive checkpoint/Undo/rollback parity passes, including synchronous failure, async boundary and runtime disconnect;
11. `bash scripts/vibecut-verify.sh` passes from a clean Kdenlive build tree;
12. licensing/SPDX review and pre-sale legal review are complete.

Source implementation for much of this gate now exists. That is not the same as verified commercial release authority.
