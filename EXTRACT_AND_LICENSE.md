# Extract and license the halthinks runtime

**Status:** plan of record on `vibecut`  
**Date:** 2026-09-04  
**Repo:** `halthinks/vibecut`  
**This document is the source of truth** for how the halthinks layer can be sold without violating the Kdenlive / original VibeCut GPL lineage.

Do not treat this as optional commentary. If a later commit contradicts this file, update this file in the same commit.

---

## 1. What you can sell, and what you cannot

### You cannot sell as a proprietary product

- The Kdenlive application
- The original VibeCut chat agent
- This combined fork as a closed-source editor
- An in-process "plugin" that links against Kdenlive / MLT / KF6 / this tree and is distributed under a proprietary license

Those are GPL-3.0 (Kdenlive: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL). Combining them into one binary and selling the binary as proprietary is not allowed.

### You can sell

A **separable licensed runtime**: the editor-agnostic planning / policy / evidence / job / provider / protocol layer, after it is cleanly extracted so it no longer contains Kdenlive types, Qt/KDE UI, MLT, or copied GPL implementation.

The Kdenlive-facing code stays in this fork, stays GPL, and is the **free adapter** that owns live editor state and speaks the runtime protocol.

```text
                    YOU MAY LICENSE COMMERCIALLY
                    after extraction (no Kdenlive types)
+------------------------------------------------------------------+
|  halthinks runtime                                                |
|  schema · protocol · EditPlan validation · trust policy           |
|  evidence store · job state machine · revision gate               |
|  model-provider client · orchestration                            |
+------------------------------------------------------------------+
                 versioned JSON / local process IPC
+------------------------------------------------------------------+
|  GPL adapter (stays in this fork, stays free)                     |
|  live VibeCutToolSurface · authorization · checkpoint/Undo        |
|  native timeline / bin / effect / render / subtitle operations    |
|  dock UI · project revision · JobManager                          |
+------------------------------------------------------------------+
|  Kdenlive + MLT + Qt + KF6          GPL                           |
+------------------------------------------------------------------+
```

Rebranding is allowed for **your** extracted runtime and **your** adapter name. It is not allowed for Kdenlive itself, KDE marks, or a claim that the editor is no longer Kdenlive.

---

## 2. Split rule (non-negotiable)

A file belongs in the **runtime** only if all of these are true:

1. It does not `#include` or import Kdenlive, MLT, KF6, Qt/KDE UI, `src/vibecut/`, or editor-private APIs.
2. It does not call Kdenlive models/controllers/undo stacks.
3. It builds and tests with no editor present.
4. Its editor-facing public surface is JSON-serializable contracts over a process/socket protocol.
5. It does not contain substantial copied implementation from original VibeCut or Kdenlive.
6. Its implementation carries the halthinks runtime license, not the GPL/KDE SPDX header copied from editor files.

A file belongs in the **GPL adapter** if it:

- touches `TimelineItemModel`, bin, effects, render, titles, subtitles, `DocUndoStack` / `QUndoStack`, the dock, `VibeCutToolSurface`, or any Kdenlive command;
- exists to translate protocol requests into native editor operations;
- owns live revision, authorization, native checkpoint/Undo, verification, or Kdenlive job state.

Almost all of `src/vibecut/` remains adapter-shaped C++ compiled into the GPL binary. That is why this tree, as a whole, is **not** a proprietary plugin. Extraction is a real process and code boundary, not a rename.

---

## 3. What extracts vs what stays

### Runtime (commercial implementation after clean extraction)

| Concern | GPL source/reference in this fork | Extracted implementation |
|---|---|---|
| Tool policy / risk / trust | `vibecutcontracts.*`, live policy snapshot | `runtime/src/halthinks_runtime/policy.py` |
| EditPlan schema / validation | `VibeCutEditPlan` / `VibeCutPlanOperation` | `contracts.py` + open JSON schema |
| Stale/moving revision gate | `vibecutplangate.*`, integrated runtime behavior | `revision.py` |
| Plan orchestration | `vibecutplanruntime.*` behavior + protocol | `session.py` |
| Job lifecycle | `vibecutjobmanager.*` public shape | `jobs.py` |
| Evidence sidecar format | `vibecutmediaevidence.*` public record shape | `evidence.py` |
| Model-provider request contract | `vibecutmodelprovider.*` public concepts | `providers.py` |
| Process protocol | new GPL adapter seam | `protocol.py`, `stdio.py`, `child_stdio.py` |

The runtime implementation is a clean-room implementation of the public protocol/contracts. Do not copy GPL function bodies into it.

### GPL adapter (never proprietary)

| Concern | Current home |
|---|---|
| Dock / chat UI | `vibecutdock.*` |
| Agent/editor loop | `vibecutagent.*` |
| Tool surface and native dispatch | `vibecuttoolsurface.*`, `vibecuttools.*`, editor `*tools.cpp` |
| Tool-policy export / hello snapshot | `vibecutruntimecontract.cpp` |
| Protocol authorization + exact stored plan | `vibecutruntimeprotocoladapter.*` |
| GPL-only protocol execution metadata | `vibecutruntimeprotocolaccess.cpp` |
| NDJSON parent/child transport | `vibecutruntimestdiotransport.*` |
| Kdenlive Undo checkpoint ownership | `vibecutruntimecheckpoint.*` |
| Project revision sourced from editor state | GPL adapter / project revision layer |
| Packaging that installs an editor | `packaging/vibecut/` |

Original VibeCut/Kdenlive files remain GPL and attributed.

---

## 4. Protocol: the legal and technical seam

The proprietary runtime and GPL Kdenlive adapter communicate **only** through the public versioned protocol. No proprietary runtime is linked in-process into the GPL editor.

### Production topology

The first production topology is:

```text
Kdenlive/VibeCut GPL editor process
        |
        | QProcess launches child directly
        v
halthinks proprietary runtime process
```

The GPL editor owns live Kdenlive state. The proprietary runtime child receives `hello` on stdin, sends requests on stdout, receives responses/events on stdin, and writes diagnostics to stderr.

A reverse subprocess client (`runtime` launches an adapter) exists for fake-adapter/OEM tests; it does **not** replace the integrated GPL Kdenlive adapter as owner of live editor state.

### V1 transport rules

1. NDJSON over stdin/stdout.
2. One UTF-8 JSON object per line.
3. Stdout is protocol-only; diagnostics are stderr-only.
4. Exact maximum encoded JSON record: **2 MiB excluding newline** on both sides.
5. No shell invocation for the production child.
6. Malformed/oversized protocol output fails closed and invalidates plan authority.
7. Future Unix socket/named-pipe transport must preserve the same authority semantics.

### V1 message families

- `hello`
- `inspect`
- `propose_plan`
- `authorize`
- `invoke`
- `verify`
- `complete_plan`
- `abort_plan`
- `job_update`
- `revision`
- `evidence_put` / `evidence_get`
- `error`

See `runtime/protocol.md` and `runtime/schema/messages.schema.json` for the exact contract.

### Revision / authorization hardening (non-negotiable)

- `base_revision` is immutable plan provenance.
- `expected_revision` is the moving execution token after authorization.
- Adapter stores the exact accepted plan.
- `authorize` creates an opaque `authorization_id` bound to that exact plan/operation set.
- `invoke` may identify only `plan_id` + `authorization_id` + `operation_id` + `expected_revision`.
- Runtime may **not** supply replacement `tool` or `input` after approval.
- GPL adapter resolves tool/input from the stored approved operation.
- GPL transport performs preflight before opening an Undo checkpoint; adapter repeats all checks immediately before native invocation.
- Effective tool policy is rechecked against the authorization-time policy.
- Kdenlive/editor revision is authoritative and is resynchronized after a checkpoint macro closes.

### Async information containment

Only jobs launched by the active approved protocol operation may cross the process boundary as `job_update`. Unrelated editor/Whisper/render/model jobs remain inside the GPL editor.

### Evidence contract

Evidence remains non-project truth. V1 confidence is exactly:

- `-1` = unknown; or
- `[0,1]` = normalized confidence.

Arbitrary negative values such as `-0.5` are invalid. A bounded frame query does not treat unknown-range evidence (`-1/-1`) as intersecting that range.

---

## 5. Licensing terms for the extracted runtime

### Current editor tree

Files under `src/vibecut/` remain GPL/KDE-licensed according to their SPDX headers. Adding this plan does not relicense them.

### Extracted implementation

`runtime/src/halthinks_runtime/` is being implemented as a separate clean-room work with:

`SPDX-License-Identifier: LicenseRef-halthinks-Proprietary`

The executable `runtime/verify.py` is designed to fail if the proprietary runtime implementation imports/references Kdenlive/MLT/KF6/Qt/KDE editor implementation, `src/vibecut/`, or GPL/KDE implementation markers.

Recommended split:

| Artifact | License |
|---|---|
| Kdenlive fork / GPL adapter / editor integration | GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL |
| Public protocol schemas | Apache-2.0 or CC0-1.0 |
| Extracted runtime implementation | proprietary commercial license owned by halthinks |
| GPL adapter source | GPL/free |

Schemas stay open so adapters can interoperate. The runtime implementation, commercial updates/support and optional policy/provider packs are the paid SKU.

### Commercial deliverables must make clear

- Kdenlive and original VibeCut remain GPL and are not the paid proprietary SKU;
- GPL adapter source remains available under its GPL terms;
- the protocol version is identified;
- Kdenlive remains authoritative editor state;
- Kdenlive is not rebranded as a proprietary original NLE.

This is not legal advice. Before the first paid invoice, have counsel experienced with GPL-adjacent commercial products review this plan, source boundaries and SPDX/license texts.

---

## 6. Rebrand rules

Allowed:

- brand the extracted runtime;
- brand the GPL adapter/capability layer appropriately;
- sell runtime licenses/support.

Not allowed:

- present Kdenlive itself as a newly proprietary original editor;
- remove required Kdenlive/VibeCut copyright/license attribution;
- tell customers GPL editor code became proprietary.

---

## 7. Extraction steps and current status

### Step 0 — freeze the contract — SOURCE-LANDED

- [x] `EXTRACT_AND_LICENSE.md`
- [x] `runtime/protocol.md`
- [x] `runtime/LICENSE.md`
- [x] open public schemas
- [x] live/effective tool+policy export via `VibeCutToolSurface::runtimeContractSnapshot()`

### Step 1 — export public schemas — SOURCE-LANDED

- [x] EditPlan / operation
- [x] tool policy
- [x] job
- [x] evidence
- [x] envelope
- [x] type-specific messages including complete/abort

Schema shape is source-landed; release compatibility still requires the verification gates below.

### Step 2 — GPL process adapter seam — SOURCE-LANDED; KDENLIVE HOST VERIFICATION OPEN

Implemented in the GPL tree:

- [x] integrated GPL protocol adapter over live `VibeCutToolSurface`
- [x] generated hello/tool-policy snapshot
- [x] exact plan storage + authorization id
- [x] no post-approval tool/input substitution
- [x] moving expected revision
- [x] read-only inspect / verify
- [x] complete / abort lifecycle
- [x] bounded NDJSON `QProcess` transport
- [x] production topology: GPL editor parent launches proprietary runtime child
- [x] protocol-owned async job filtering
- [x] disconnect invalidates active plan authority
- [x] C++ fake-surface/source regressions registered
- [ ] full Kdenlive compile/link/test execution
- [ ] hands-on production child process smoke inside Kdenlive

No Kdenlive native handler was moved out of GPL.

### Step 3 — clean-room runtime — SUBSTANTIAL SOURCE IMPLEMENTATION LANDED; EXACT GATE OPEN

Implemented under `runtime/src/halthinks_runtime/`:

- [x] plan validator / deterministic dependency ordering
- [x] trust-mode/tool-policy engine
- [x] immutable base + moving revision gate
- [x] bounded job state machine
- [x] atomic provenance-scoped evidence store with no project mutation API
- [x] provider-neutral model client
- [x] governed runtime session
- [x] read-only inspect-driven revision refresh
- [x] fake/OEM adapter subprocess client
- [x] production child inherited-stdio client (synchronous/thread-free)
- [x] synchronous + async fake-adapter/process tests in source
- [x] executable clean-room verifier (`python3 runtime/verify.py`)
- [x] secure provider endpoint policy: remote HTTPS, loopback-only HTTP
- [ ] run `python3 runtime/verify.py` against the **exact current branch checkout** and record the result
- [ ] package/install smoke from a clean environment

Prior reconstructed local test runs found and drove real fixes, including the child-stdio shutdown race. They are useful development evidence but do **not** substitute for the exact-tree verifier above.

### Step 4 — out-of-process execution parity — SOURCE-LANDED; LIVE UNDO VERIFICATION OPEN

Current source mirrors the existing integrated `VibeCutPlanRuntime` checkpoint scope:

- [x] stale-plan rejection
- [x] moving expected revision across approved operations
- [x] exact approved-operation binding / no substitution
- [x] Review / Auto / Turbo policy semantics
- [x] consecutive synchronous mutating operations share one adapter-side Kdenlive Undo macro
- [x] checkpoint commits before async work
- [x] failed synchronous mutation rolls back the current open synchronous checkpoint
- [x] abort rolls back the current open synchronous checkpoint
- [x] already committed pre-async checkpoints are **not** falsely claimed as rolled back
- [x] post-checkpoint editor revision resync
- [x] job wait + external-only drift handling
- [x] read-only postcondition inspection
- [x] editor-independent checkpoint state tests registered
- [ ] real Kdenlive Undo/Redo/checkpoint smoke for the process path
- [ ] authoritative compile/test gate

Do not claim stronger all-plan atomic rollback than the integrated runtime actually provides.

### Step 5 — license and ship — NOT OPEN

Do not open paid distribution until Section 10 passes.

### Step 6 — do not fork-and-close

Never treat this whole GPL editor repository as the proprietary SKU.

---

## 8. Studio / OEM packaging frame

| SKU | Paid deliverable | Not included |
|---|---|---|
| Studio | runtime binary/service + protocol compatibility + updates for N seats | relicensing Kdenlive/editor source |
| OEM | runtime library/service + protocol integration support | right to close a linked Kdenlive binary |
| Adapter | GPL adapter source/binary under GPL | optional paid support is separate |

Pricing is a business decision. The license boundary is not.

---

## 9. Current extracted-tree map

```text
runtime/
  LICENSE.md
  protocol.md
  verify.py
  pyproject.toml
  schema/
    editplan.schema.json
    toolpolicy.schema.json
    evidence.schema.json
    job.schema.json
    envelope.schema.json
    messages.schema.json
  src/halthinks_runtime/
    __init__.py
    contracts.py
    policy.py
    revision.py
    jobs.py
    evidence.py
    protocol.py
    providers.py
    session.py
    stdio.py
    child_stdio.py
  tests/
    fake_adapter.py
    fake_adapter_stdio.py
    fake_runtime_child.py
    test_core.py
    test_session.py
    test_inspect.py
    test_providers.py
    test_stdio.py
    test_child_stdio.py
```

GPL adapter remains under `src/vibecut/`.

---

## 10. Acceptance test for "extracted / licensable"

Extraction is complete only when **all** of these pass:

1. `python3 runtime/verify.py` passes from the exact source tree.
2. The clean-room verifier confirms no Kdenlive/MLT/KF6/Qt/KDE/GPL implementation dependency in `runtime/src`.
3. Public schema/implementation seam tests pass, including exact 2 MiB NDJSON bound and exact evidence-confidence semantics.
4. Fake adapter proves inspect → propose → authorize → invoke → verify → complete.
5. Stale `base_revision` is rejected before mutation.
6. Moving expected revision accepts adapter-owned successful mutation revisions and rejects unrelated drift.
7. Approval binds the exact plan; post-approval tool/input substitution is impossible.
8. Review mode never mutates before required authorization; hard confirmation remains non-waivable.
9. Async job events exported to the runtime are limited to active protocol-owned jobs.
10. Evidence writes remain non-project truth and provenance/range semantics match the GPL adapter.
11. GPL adapter compiles/links and all registered `vibecut*` tests pass.
12. Live Kdenlive process-path checkpoint smoke proves synchronous rollback, async boundary, disconnect handling and user Undo/Redo behavior.
13. `bash scripts/vibecut-verify.sh` passes from a clean Kdenlive build tree.
14. Packaging/install/uninstall/coexistence smoke passes.
15. README/user-facing material continues to state Kdenlive is authoritative editor state and preserves lineage/licenses.
16. A lawyer experienced with GPL-adjacent commercial products reviews the final boundary before the first paid invoice.

**Until every required technical/licensing gate above is satisfied, there is not yet a release-qualified licensed runtime SKU.** There is now a substantial clean-room runtime and GPL process seam in source, but source-landed is not the same as commercially verified.

---

## 11. Related docs

- `README.md`
- `VIBECUT_ARCHITECTURE.md`
- `DESIGN_SPECS.md`
- `TODO.md`
- `VIBECUT_ROADMAP_STATUS.md`
- `runtime/protocol.md`
- `runtime/LICENSE.md`
