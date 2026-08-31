# VibeCut Agent Architecture

**Status:** integration architecture on `agent/vibecut-architecture-slices`  
**Host:** Kdenlive / Qt / KDE / MLT  
**Design goal:** a state-aware, semantically aware, transactionally safe editing agent — not a collection of AI buttons.

## 1. Core invariants

1. **Kdenlive is authoritative state.** Model text is never accepted as evidence that an edit happened.
2. **Read first, mutate second.** Stable clip/subtitle/tool ids come from live project inspection.
3. **No ungoverned mutation.** Project changes and external side effects pass through the plan runtime.
4. **No stale plan execution.** Every plan captures a project revision and is rejected when that revision is no longer current.
5. **No `ok:true` without evidence.** Mutating tools verify the relevant live Kdenlive state before success.
6. **Long work is asynchronous.** Render, transcription, setup, downloads, indexing, generation, etc. return a JobManager id instead of blocking the GUI.
7. **Project mutation and external side effect are different concepts.** Only real Kdenlive project mutations participate in undo macros/project diffs.
8. **Human authority is explicit.** Review/Auto/Turbo is policy, not a suggestion to the model.
9. **Native mode is not a raw shell.** Broad editor capability is exposed through governed Kdenlive operations. General code execution belongs behind VibeScript-style sandboxing.
10. **Extensions plug into seams, not the chat loop.** Providers, tools, context, jobs, and media extractors have dedicated contracts.

## 2. Runtime topology

```text
VibeCutDock
   │
   ├── Trust mode: Review / Auto / Turbo
   ├── Plan review: Approve / Cancel
   └── Chat / evidence / job status
          │
          ▼
VibeCutAgent
   ├── ConversationContext compaction
   ├── Project rules injection
   ├── ModelProvider
   ├── VibeCutHooks context/event bus
   └── VibeCutToolSurface
          │
          ├── read-only inspection/retrieval ───────────────┐
          │                                                │
          └── edit_plan_propose                             │
                  │                                        │
                  ▼                                        │
          VibeCutPlanRuntime                               │
          ├── schema/dependency validation                  │
          ├── project revision gate                         │
          ├── trust/confirmation policy                     │
          ├── Kdenlive undo checkpoints                     │
          ├── async JobManager synchronization              │
          └── before/after project evidence                 │
                  │                                        │
                  ▼                                        │
          VibeCutToolSurface ◄──────────────────────────────┘
                  │
                  ▼
          Kdenlive native models/controllers
```

## 3. Source map

### Agent and UI

- `src/vibecut/vibecutagent.*` — model/tool loop, plan interception, provider use, hook publication.
- `src/vibecut/vibecutdock.*` — chat UI, plan review controls, trust selector, deterministic evidence and next actions.
- `src/vibecut/sseparser.h` — existing streaming-event parser.

### Governance and planning

- `vibecutcontracts.*` — `VibeCutToolPolicy`, risk classes, `VibeCutEditPlan`, operations, validation.
- `vibecutplangate.*` — stale revision, dependencies, policy, confirmation decision.
- `vibecutplanruntime.*` — pending plan state, approval, deterministic dependency order, checkpoints, async waits, final evidence.
- `vibecutplantools.*` — `edit_plan_propose` schema/handler.
- `vibecutprojectrevision.*` / `vibecutprojectstate.cpp` — monotonic live project revision token.
- `vibecutprojectsnapshot.*` — project-level before/after counts/diff evidence.

### Capability surface

- `vibecuttools.*` — original native tools retained as a compatibility/native core.
- `vibecuttoolsurface.*` — canonical merged schemas/policies/dispatch, extension registration, native-tool decorators.
- `vibecuttoolpolicies.cpp` — governance metadata for legacy native tools.
- `vibecutedittools.*` — verified clip move/split/trim/ripple/delete.
- `vibecutmarkertools.*` — project guides and range guides.
- `vibecutsubtitletools.*` — subtitle search + scope-safe subtitle generation decorator.
- `vibecutsubtitleedittools.*` — subtitle edit/delete.
- `vibecuttransitiontools.*` — installed transition discovery + verified native insertion.
- `vibecuttitletools.*` — Kdenlive title-document/bin creation + timeline insertion.
- `vibecutrendertools.*` — render preset discovery + async verified native render/export.

### Long-running work

- `vibecutjobmanager.*` — stable id, state, progress, cancellation request, terminal state.
- `vibecutjobtools.*` — job inspection + legacy Whisper setup job bridge.
- `vibecutsubtitlepipeline.*` — non-blocking audio export / Whisper / import chain with stale-result guard.

### Context, providers, intelligence

- `vibecutconversationcontext.*` — bounded complete-exchange conversation history.
- `vibecutprojectrules.*` — project-local `.vibecutrules` loading.
- `vibecutmodelprovider.*` — provider factory/request seam.
- `vibecuthooks.*` — lifecycle event bus and named structured context providers.
- `vibecutmediaindex.*` — provider-neutral time-ranged project knowledge index.
- `vibecutmediatools.*` — `media_search` retrieval tool.
- `vibecuteval.*` — basic planning/safety eval scoring.

## 4. Tool policy model

Each exposed tool has governance metadata separate from model-provider JSON:

```text
VibeCutToolPolicy
  name
  risk
  reversible
  mutatesProject
  asynchronous
  confirmationRequired
```

Current risk classes:

- **ReadOnly** — inspect/search/status; immediate.
- **ReversibleEdit** — normal Kdenlive undoable mutations.
- **MajorEdit** — broader/destructive-looking but undoable project changes such as delete/ripple/subtitle generation.
- **ExternalSideEffect** — writes/downloads/renders/publishes outside project state.
- **Irreversible** — must always require confirmation.

`mutatesProject` is intentionally independent from risk. A render is an external side effect but not a project mutation; an effect application is a project mutation. Undo/project-diff logic keys off `mutatesProject`, not merely risk.

## 5. Trust modes

### Review

Every non-read side effect requires explicit plan approval.

### Auto

Reversible edits may execute after governance/validation without a review click. Major edits and external side effects still require approval.

### Turbo

Governed work can auto-execute except tools explicitly marked `confirmationRequired` or `Irreversible`.

Trust mode never changes tool schemas or bypasses validation/revision checks.

## 6. Project revision and stale plans

`VibeCutProjectRevisionTracker` observes the current Kdenlive undo stack and emits a monotonic token. It deliberately does not use a raw undo index as the revision because undo followed by a new edit can reuse index values.

An `EditPlan` stores `baseRevision`. Approval rechecks it immediately. Async mutating tools must also protect their own captured base state before committing results. The subtitle pipeline follows this rule before importing transcription output.

## 7. Transaction/checkpoint behavior

Contiguous synchronous `mutatesProject=true` operations execute inside a Kdenlive `QUndoStack` macro. If one reports failure, the macro is closed and immediately undone.

Async operations close the current synchronous checkpoint before starting. The runtime waits on the returned JobManager id. When a mutating async job succeeds, the new project revision becomes the next checkpoint. When an external-only async job succeeds, a user project edit during that job only invalidates *remaining* plan operations; it does not retroactively declare an already-produced render invalid.

## 8. Verification evidence

Tool-specific verification examples:

- move — live track id + position must match;
- split — both clip sides must exist at the expected cut boundary and originate from the same bin asset;
- trim — live duration must equal the applied duration;
- delete — clip/subtitle id must no longer exist;
- guide — marker/range must exist with requested content/duration;
- transition — live composition id/track/position must match;
- title — both bin title asset and inserted timeline clip must match;
- subtitles — generated SRT must import only if project state is still valid;
- render — native renderer must exit normally and final output files must exist with nonzero size.

Project-mutating plans additionally capture a coarse project snapshot and append a final diff (clips, subtitles, effects, duration, etc.).

## 9. JobManager contract

Every expensive/long operation should use the shared manager:

```text
Queued
  ↓
Running ───→ CancelRequested
  │                │
  ├──→ Succeeded   └──→ Cancelled
  └──→ Failed
```

Async plan operations must return:

```json
{
  "ok": true,
  "started": true,
  "job_id": "stable-id"
}
```

The plan runtime does not guess when an async operation has completed.

## 10. Media Intelligence contract

`VibeCutMediaIndex` is the common retrieval layer. Current extractors populate:

- timeline clip names and frame ranges;
- subtitle/transcript text, ids, layers and frame ranges.

Future extractors should add `VibeCutMediaDocument` records instead of inventing isolated retrieval systems:

- speakers/diarization;
- scene/shot boundaries;
- silence and audio events;
- OCR/on-screen text;
- faces/subjects where appropriate;
- visual/CLIP embeddings;
- semantic clip/transcript embeddings;
- generated analysis annotations.

The retrieval API remains time-ranged evidence regardless of extractor.

## 11. Hook contract

`VibeCutHooks` publishes lifecycle events such as:

- `conversation.request`, `conversation.reset`;
- `model.request`;
- `tool.invoked`, `tool.completed`, `tool.failed`, `tool.blocked_direct_mutation`;
- `plan.proposed`, `plan.approved`, `plan.progress`, `plan.finished`;
- `job.added`, `job.changed`, `job.progress_message`;
- `trust.changed`;
- `agent.error`.

External modules can also register named context providers returning bounded `QJsonObject` data. Collected context is injected under a dedicated extension-context section instead of allowing arbitrary prompt replacement.

## 12. Provider contract

The model-provider registry owns provider factories. Provider configuration is selected through `VIBECUT_PROVIDER`; Anthropic remains the built-in provider and `VIBECUT_MODEL` can override its model name.

The provider seam owns request construction and exposes stream-event normalization. Core planning/tool/governance code must not depend on a provider-specific REST schema.

## 13. Current native editing vocabulary

### Read/search

- `timeline_list_clips`
- `timeline_get_selection`
- `subtitles_search`
- `media_search`
- `guides_list`
- `transitions_list`
- `render_presets_list`
- `speech_status`
- `jobs_list`
- `job_status`

### Project mutation

- `effect_apply`
- `clip_move`
- `clip_split`
- `clip_trim`
- `clip_ripple_trim`
- `clip_delete`
- `guide_add`
- `guide_range_add`
- `guide_remove`
- `subtitle_edit`
- `subtitle_delete`
- `generate_subtitles`
- `transition_add`
- `title_create`

### External side effects

- `speech_setup`
- `render_start`

The next editing breadth should continue through native Kdenlive APIs: richer effect parameter editing, composition editing/removal, title edits, bin/media insertion, grouped multi-clip operations, audio/track controls, and publishing adapters.

## 14. VibeScript boundary

Native mode intentionally does not expose raw shell execution. The future VibeScript subsystem should be a `QJSEngine` sandbox that can inspect explicitly provided structured project context and produce a validated plan/artifact. Any result that changes the real project still passes through the same plan/tool/governance runtime.

It must not become an alternate ungoverned path around `VibeCutToolSurface`.

## 15. Secrets

Environment variables remain the current provider bootstrap mechanism. The intended desktop UX is a secure KDE keyring/KWallet-backed secret store plus settings UI. That should be implemented as a secret-provider seam so headless/test environments can continue using environment variables without embedding credentials in project files or source.

## 16. Verification without GitHub CI

No GitHub Actions are required for VibeCut development. Use:

```bash
bash scripts/vibecut-verify.sh
```

The script configures/builds locally and runs `ctest -R '^vibecut' --output-on-failure`.

Before upstream PR:

1. clean local configure/build;
2. all `vibecut*` tests pass;
3. hands-on smoke project: inspect → plan → approve → edit → undo;
4. long Whisper job while interacting with UI;
5. render job with verified output and cancellation test;
6. stale-plan test: propose, manually edit timeline, attempt approval;
7. Auto/Turbo trust-policy tests;
8. compound setup → subtitles plan;
9. title/transition/guide/razor smoke tests;
10. review branch diff for accidental upstream/unrelated changes.

## 17. Upstream strategy

Keep VibeCut work isolated from unrelated Kdenlive changes. Once the integration branch passes the local gate, split the upstream contribution into reviewable conceptual PRs where practical (core dock/tool infrastructure, governed plan runtime, async subtitle improvements, etc.) while preserving the fork branch as the full product integration line.
