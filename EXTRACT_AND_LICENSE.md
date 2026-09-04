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

A **separable licensed runtime**: the editor-agnostic planning / policy / evidence / job / protocol layer, after it is extracted so it no longer contains Kdenlive types, Qt-KDE widgets, MLT, or copy-pasted GPL implementation.

The Kdenlive-facing code stays in this fork, stays GPL, and becomes a **free adapter** that speaks the runtime protocol.

                    YOU MAY LICENSE COMMERCIALLY
                    after extraction (no Kdenlive types)
+------------------------------------------------------------------+
|  halthinks runtime                                                |
|  schema · protocol · EditPlan validation · trust policy           |
|  evidence store format · job state machine · revision gate        |
|  model-provider request contract                                  |
+------------------------------------------------------------------+
                 JSON / stdio / socket / local IPC
+------------------------------------------------------------------+
|  GPL adapter (stays in this fork, stays free)                     |
|  VibeCutToolSurface Kdenlive bindings                             |
|  native timeline / bin / effect / render / undo ops               |
|  dock UI · project revision tracker wired to QUndoStack           |
+------------------------------------------------------------------+
|  Kdenlive + MLT + Qt + KF6          GPL                           |
+------------------------------------------------------------------+

Rebranding is allowed for **your** extracted runtime and **your** adapter name. It is not allowed for Kdenlive itself, KDE marks, or a claim that the editor is no longer Kdenlive.

---

## 2. Split rule (non-negotiable)

A file belongs in the **runtime** only if all of these are true:

1. It does not `#include` Kdenlive, MLT, KF6, or application UI headers.
2. It does not call Kdenlive models / controllers / undo stacks.
3. It can be built and tested with no editor present.
4. Its public surface is JSON-serializable contracts and a process/socket protocol.
5. It does not contain substantial copied implementation from original VibeCut or Kdenlive.

A file belongs in the **GPL adapter** if it:

- touches `TimelineItemModel`, bin, effects, render, titles, subtitles, QUndoStack, the dock, or any Kdenlive command
- exists only to map protocol messages onto native editor operations

Today almost all of `src/vibecut/` is adapter-shaped C++ compiled into the GPL binary. That is why this tree, as it sits, is **not** a sellable proprietary plugin. Extraction is a real split, not a rename.

---

## 3. What extracts vs what stays

### Runtime (commercial after clean extraction)

| Concern | Current home in this fork | Extracted form |
|---|---|---|
| Tool policy / risk / trust | `vibecutcontracts.*`, `vibecuttoolpolicies.cpp` | policy engine + JSON policy table |
| EditPlan schema / validation | `VibeCutEditPlan` in `vibecutcontracts.*` | `schema/editplan.schema.json` + validator |
| Stale-plan / revision gate | `vibecutplangate.*`, `vibecutprojectrevision.*` | revision token protocol + gate |
| Plan execution orchestration | `vibecutplanruntime.*` | runtime that calls adapter ops over protocol |
| Job lifecycle | `vibecutjobmanager.*` | job state machine + job ids |
| Evidence sidecar format | `vibecutmediaevidence.*` | `.vibecutmedia.json` schema + store |
| Rules / policy / memory files | `vibecutprojectrules.*`, `vibecutpolicyoverrides.*`, `vibecutprojectmemory.*` | file formats + loaders |
| Model provider request contract | `vibecutmodelprovider.*` | provider-neutral request/stream events |
| Conversation compaction rules | `vibecutconversationcontext.*` | bounded history policy |
| Extractor provider contract | `vibecutextractorprovider.*`, `vibecutextractorrequest.*` | capability + sink protocol |

### GPL adapter (never proprietary)

| Concern | Current home |
|---|---|
| Dock / chat UI | `vibecutdock.*` |
| Agent loop wired to Kdenlive | `vibecutagent.*` |
| Tool surface dispatch into Kdenlive | `vibecuttoolsurface.*`, `vibecuttools.*` |
| Every `*tools.cpp` that mutates or reads live editor state | timeline, bin, effects, titles, render, subtitles, groups, tracks, … |
| Undo checkpoints / macros | plan runtime pieces that call `QUndoStack` |
| Project revision sourced from the undo stack | `vibecutprojectrevision.*` host wiring |
| Packaging that installs an editor | `packaging/vibecut/` |

Original VibeCut files that pre-existed this fork stay GPL and stay attributed.

---

## 4. Protocol (the legal and technical seam)

The runtime and the adapter must communicate **only** through a versioned protocol. No in-process linking of a proprietary runtime into the GPL editor.

Transport, in order of implementation:

1. newline-delimited JSON on stdio (first extract target)
2. local Unix socket / named pipe
3. optional TCP localhost with explicit bind policy

Every message is one JSON object with:

{
  "v": 1,
  "id": "msg-…",
  "kind": "request | response | event",
  "type": "hello | inspect | propose_plan | authorize | invoke | verify | job_update | revision | evidence_put | evidence_get | error",
  "payload": {}
}

### Minimum message types

- `hello` — adapter announces editor id, protocol version, available tools + policies
- `inspect` — runtime asks adapter for live state (clips, selection, revision, …)
- `propose_plan` — runtime emits an `EditPlan` bound to immutable `base_revision`; adapter stores the accepted plan object for authorization
- `authorize` — adapter/human returns Review / Auto / Turbo decision; approval creates an opaque `authorization_id` and starting `expected_revision`
- `invoke` — runtime references only an approved `operation_id`; the adapter resolves the stored approved tool/input and refuses post-approval substitution
- `verify` — runtime asks adapter for postconditions against live state using the current `expected_revision`
- `job_update` — adapter pushes job lifecycle and current revision where continuation is revision-sensitive
- `revision` — adapter pushes current project revision token
- `evidence_put` / `evidence_get` — sidecar records, never treated as project truth
- `error` — structured failure, never `ok: true` without evidence

### Revision / authorization hardening (non-negotiable)

`base_revision` and execution revision are **not the same thing**:

- `base_revision` is immutable plan provenance: the revision the plan was reasoned from.
- `expected_revision` is the moving execution token. It starts when authorization is granted and advances only from successful adapter-reported operations/events.

The adapter must store the exact approved plan. After approval the runtime is not allowed to send a replacement tool name or replacement JSON input. `invoke` identifies the stored approved operation by `plan_id` + `authorization_id` + `operation_id` + `expected_revision`.

Before each operation the adapter checks current revision equals `expected_revision`. A legitimate approved mutation may advance the revision; the adapter returns `revision_after`, which becomes the next expected token. An unrelated user/project change makes the remaining plan stale.

This mirrors the current integrated `VibeCutPlanRuntime`, which tracks a moving expected revision while allowing its own approved operations to advance the undo-stack revision.

Kdenlive remains authoritative state. The runtime is not a second project database.

The v1 public contract lives at:

- `runtime/protocol.md`
- `runtime/schema/editplan.schema.json`
- `runtime/schema/toolpolicy.schema.json`
- `runtime/schema/evidence.schema.json`
- `runtime/schema/job.schema.json`
- `runtime/schema/envelope.schema.json`
- `runtime/schema/messages.schema.json`

---

## 5. Licensing terms for the extracted runtime

### Current tree (this fork)

Every file under `src/vibecut/` is currently marked:

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

That header controls **this** copy. You do not get to sell this copy as proprietary by adding a second file that says otherwise.

### After extraction

The extracted runtime is a **new work** in a new tree (`runtime/` here, later its own repo) that:

- reimplements contracts and protocol from this plan and the public schemas
- does not copy GPL function bodies from Kdenlive or original VibeCut
- carries a halthinks commercial license **and** an optional evaluation / source-available license if you want one

Recommended split licenses:

| Artifact | License |
|---|---|
| This fork, adapter, editor integration | GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL |
| Protocol schemas (`runtime/schema/*`) | CC0-1.0 or Apache-2.0 (keep them public so adapters can exist) |
| Extracted runtime implementation | Proprietary commercial license owned by halthinks |
| Adapter in this repo | GPL, free, so any editor build can hook the runtime |

Schemas should stay open. The implementation of the planner, policy engine, evidence store, and job runtime is what you sell.

### What a customer actually buys

1. **Studio license** — use the runtime binary / service against one or more GPL adapters they already have (this Kdenlive adapter, later others).
2. **OEM license** — embed the runtime in their own product, provided they do not statically link it into a GPL editor binary they then close.
3. **Support / updates** — protocol version guarantees, policy packs, extractor packs.

They do **not** buy the right to close Kdenlive.

### What you must put on every commercial deliverable

- Kdenlive and original VibeCut remain GPL and are not part of the paid SKU
- the adapter source stays available under GPL
- the protocol version the SKU speaks
- that Kdenlive is the authoritative editor and is not rebranded as your editor

This is not legal advice. Before the first paid invoice, have a lawyer who has shipped GPL-adjacent products read this file and the SPDX headers.

---

## 6. Rebrand rules

Allowed:

- name the runtime whatever you want (halthinks, VibeCut Runtime, something else)
- name the adapter package as a VibeCut / halthinks capability layer
- sell the runtime under that brand

Not allowed:

- ship Kdenlive renamed as if it were your original NLE
- remove Kdenlive / VibeCut copyright and license files from the editor tree
- tell customers the editor itself is proprietary

Keep Kdenlive as authoritative state in every user-facing doc.

---

## 7. Extraction steps (do these in order)

### Step 0 — freeze the contract — LANDED IN SOURCE

- [x] This file
- [x] `runtime/protocol.md`
- [x] `runtime/schema/editplan.schema.json`
- [x] `runtime/LICENSE.md`
- [x] Tool policy table exported as JSON from `VibeCutToolSurface::runtimeContractSnapshot()` (generated from the live/effective schemas + policies; do not hand-edit a long-term policy dump)

### Step 1 — export schemas from the live C++ types — LANDED IN SOURCE

Mirror, as JSON Schema:

- [x] `VibeCutToolPolicy` → `runtime/schema/toolpolicy.schema.json`
- [x] `VibeCutEditPlan` / `VibeCutPlanOperation` shape → `runtime/schema/editplan.schema.json`; graph/revision rules remain semantic validation
- [x] job record → `runtime/schema/job.schema.json`
- [x] evidence record → `runtime/schema/evidence.schema.json`
- [x] common envelope → `runtime/schema/envelope.schema.json`
- [x] hello / inspect / propose / authorize / invoke / verify / job / revision / evidence / error payloads → `runtime/schema/messages.schema.json`

Keep field names stable. If C++ names change, version the protocol.

### Step 2 — write a protocol adapter shim inside this GPL tree — NEXT

Add a small GPL adapter transport in this repo that:

- speaks stdio JSON to an out-of-process runtime
- calls existing `VibeCutToolSurface` handlers
- exports `runtimeContractSnapshot()` as the authoritative hello/tool-policy table
- stores the exact proposed plan before authorization
- binds authorization to an opaque `authorization_id`
- resolves invoke tool/input from the stored approved operation rather than trusting replacement input from the runtime
- enforces moving `expected_revision` around native calls
- does **not** move Kdenlive handlers out of GPL

This shim is the proof that the runtime can be out-of-process.

### Step 3 — reimplement the runtime outside Kdenlive types

New tree (`runtime/src` or a future `halthinks/runtime` repo):

- plan validator
- trust-mode policy engine
- revision gate
- job state machine
- evidence store
- model-provider client

Tests must pass with a fake adapter. No Kdenlive linked.

### Step 4 — stop in-process plan execution from being the only path

`VibeCutPlanRuntime` in the editor keeps working for the integrated product. The commercial SKU uses the out-of-process runtime + GPL shim. Do not delete the integrated path until the protocol path has parity on:

- stale-plan rejection
- moving expected-revision handling across multiple approved operations
- no post-approval operation substitution
- Review / Auto / Turbo
- undo checkpoint + rollback behavior (adapter-side)
- job wait
- postcondition verify

### Step 5 — license and ship

- commercial license text on the runtime repo
- public schemas
- GPL adapter remains in `halthinks/vibecut`
- invoice SKUs: Studio / OEM / support

### Step 6 — do not fork-and-close

Never take this whole repository private-as-proprietary. The editor half cannot follow.

---

## 8. Studio / OEM pricing frame (not a quote)

Use this as the internal packaging, not as published legal terms.

| SKU | What is delivered | What is not |
|---|---|---|
| Studio | runtime binary + protocol docs + updates for N seats | editor source relicensed |
| OEM | runtime library / service + support to speak protocol from their host | right to statically link into a closed Kdenlive |
| Adapter | this fork, GPL, no fee required | support is optional and separate |

Price is a business choice. License shape is not.

---

## 9. File map for the extracted tree

runtime/
  LICENSE.md                 commercial terms + GPL boundary
  protocol.md                message catalog + revision/authorization semantics
  schema/
    editplan.schema.json
    toolpolicy.schema.json
    evidence.schema.json
    job.schema.json
    envelope.schema.json
    messages.schema.json
  src/                       editor-agnostic implementation (future)
  tests/                     fake-adapter tests (future)

The GPL adapter stays at `src/vibecut/`.

---

## 10. Acceptance test for "extracted"

Extraction is done only when all of these pass:

1. `runtime/` builds with no Kdenlive, MLT, KF6, or `src/` editor headers.
2. A fake adapter can propose → authorize → invoke → verify a plan.
3. A stale `base_revision` is rejected without calling mutate.
4. Review mode never mutates before `authorize`.
5. Approval binds the exact stored plan; the runtime cannot substitute tool/input after authorization.
6. A legitimate approved mutation may advance `expected_revision`; unrelated revision drift stops remaining operations.
7. Evidence writes cannot become project truth.
8. SPDX / license text on runtime files is not GPL-3.0-only copied from Kdenlive files.
9. This fork still builds and `bash scripts/vibecut-verify.sh` still passes.
10. README still says Kdenlive is authoritative state.

Until then, there is no licensed runtime. There is only this plan, public protocol/schema work, and the GPL layer in `src/vibecut/`.

---

## 11. Related docs in this repo

- `README.md` — product framing and lineage
- `VIBECUT_ARCHITECTURE.md` — current in-process architecture
- `DESIGN_SPECS.md`
- `TODO.md`
- `VIBECUT_ROADMAP_STATUS.md`
- `runtime/protocol.md`
- `runtime/LICENSE.md`
