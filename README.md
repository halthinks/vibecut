![](data/pics/vibecut-screenshot.png)

# VibeCut — halthinks capability layer

This repository is the **halthinks/VibeCut** fork: a substantial capability expansion of the original VibeCut project, which itself is an AI-scriptable adaptation of Kdenlive.

The lineage matters and is intentionally preserved here:

**Kdenlive → original VibeCut → halthinks/VibeCut**

The work in this fork does not attempt to erase or reframe the work that came before it. Kdenlive remains the professional nonlinear editor, media engine, timeline model, render stack, undo system, effects ecosystem, and application foundation. Original VibeCut introduced the important idea that the editor could be driven and extended from natural language through a live AI chat surface. This fork builds on that idea by turning the assistant into a **state-aware, governed editing agent** with broader native editing access, persistent evidence, multi-step planning, verification, policy, long-running jobs, recovery, and extensibility.

The original VibeCut README layer and Kdenlive README are preserved below this section.

---

## What is different in halthinks/VibeCut?

The original VibeCut demonstrated that an AI assistant could directly operate a real Kdenlive project. halthinks/VibeCut expands that concept from individual AI-triggered operations into a governed editing system designed to reason about, plan against, modify, and verify the state of a professional nonlinear editing project.

The intended experience is closer to having an editing collaborator inside the application than having a collection of AI buttons.

A user should be able to say things such as:

- “Find the dead air in this interview and show me what you would remove.”
- “Clean this dialogue, transcribe it, and find every place we discuss the launch.”
- “Keep the best take of each repeated line, let me review the choices, then apply my selections as one undoable edit.”
- “Move this section, preserve linked audio/video, and tell me exactly what changed.”
- “Find missing media and explain what can be relinked automatically.”
- “Prepare a review render using the presets actually installed on this machine.”
- “Apply this effect stack to these clips, but make the whole operation one undoable edit.”

The assistant is expected to inspect live state first, work from real project/media evidence, produce an explicit plan for consequential changes, execute through native Kdenlive APIs, and verify the result rather than assuming that a requested edit succeeded.

---

## User-facing capability increases

### 1. Project-aware editing agent

**User experience:** the assistant can inspect the current project before acting instead of treating the conversation as the source of truth. It can reason about clips, tracks, selections, subtitles, sequences, media assets, effects, transitions, groups, proxies, and other live editor state.

**Technical basis:** VibeCut exposes Kdenlive state through a governed `VibeCutToolSurface` and native read tools. Tool results are derived from the live project model rather than model memory.

### 2. Multi-step EditPlans instead of uncontrolled tool loops

**User experience:** complex requests can become reviewable plans. You can see what the assistant intends to change before the project is modified.

**Technical basis:** consequential operations are represented as a revision-bound `EditPlan` containing ordered operations, dependencies, expected postconditions, policy metadata, and a captured project revision.

### 3. Review, Auto, and Turbo trust modes

**User experience:** users can choose how much autonomy the editing agent receives without giving up hard safety boundaries.

- **Review** favors explicit approval before edits and external side effects.
- **Auto** can perform governed reversible work while escalating higher-risk actions.
- **Turbo** permits broader automatic execution but still respects confirmation-required and irreversible boundaries.

**Technical basis:** every exposed tool carries a `VibeCutToolPolicy` describing risk, reversibility, project mutation, async behavior, confirmation requirements, and automatic-execution eligibility.

### 4. Stale-plan prevention

**User experience:** if the project changes after the assistant creates a plan, the system can reject the old plan instead of applying edits to a timeline that no longer matches what was inspected.

**Technical basis:** a monotonic project-revision tracker is attached to live undo-stack changes. Plans capture a base revision and must match current project state before execution.

### 5. Transactional native edits, rollback, and Undo

**User experience:** grouped edits are designed to behave like real editor operations rather than a sequence of fragile AI clicks. Failed multi-step edits can roll back, and successful grouped work can remain understandable in Kdenlive’s undo history.

**Technical basis:** editing tools use Kdenlive’s native timeline/bin/effect APIs, accumulated undo/redo functions, checkpoints, and verification rather than shell-level project-file rewriting.

### 6. Verified results and project diffs

**User experience:** the assistant can tell you what actually changed, not merely what it attempted to do.

**Technical basis:** plan execution captures project snapshots and evaluates live postconditions. Proposed-versus-observed state can be surfaced as a project diff/evidence result.

### 7. Broad native timeline editing

**User experience:** the agent can perform real nonlinear editing tasks including clip movement, split, trim, ripple trim, deletion, copying, group movement, guide operations, governed bulk operations, and exact governed timeline-range removal.

**Technical basis:** operations are routed through Kdenlive timeline models/functions and preserve stable timeline identifiers, group topology, exact frame positions, and native undo behavior where supported. `timeline_range_remove` uses Kdenlive’s accumulated zone-extraction seam with explicit lift/ripple semantics, locked-track refusal and live postcondition verification.

### 8. Tracks, routing, groups, and selection awareness

**User experience:** the assistant can reason about more than individual clips. It can inspect and modify track structure, track names/order/lock/enabled state, routing, selected items, and grouped media.

**Technical basis:** dedicated track, routing, group, and selection tools expose live `TimelineItemModel` / controller state and validate edits after mutation.

### 9. Effects as inspectable editor state

**User experience:** effects can be listed, inspected, added, removed, grouped, parameterized, keyframed, copied between clips, and applied at bus/track scope where supported.

**Technical basis:** VibeCut wraps Kdenlive’s effect-stack models and native effect APIs rather than inventing an independent AI-only effect representation.

### 10. Transitions, compositions, and same-track mixes

**User experience:** the assistant can work with real installed transitions and compositions and can inspect or modify same-track mixes without pretending unsupported transition names exist.

**Technical basis:** transition and mix tools query the current runtime, operate on actual composition/mix objects, expose A-track/parameters, and verify resulting timeline state. Mix type/parameter breadth remains intentionally constrained to backend seams that Kdenlive exposes safely.

### 11. Titles and title-item inspection

**User experience:** the agent can create title assets, place them on the timeline, inspect title content, and edit supported title items.

**Technical basis:** title operations create and modify Kdenlive-native title/bin artifacts instead of rendering text into opaque external media. Richer shape/image/template/brand-pack work remains sequenced behind safe native element-editing seams and evaluation fixtures.

### 12. Bin and media-management capabilities

**User experience:** users can ask the assistant to inspect project media, create/rename/delete empty folders, move media between folders, import files, inspect source health, insert bin clips, and identify missing media.

**Technical basis:** these capabilities use Kdenlive’s project-bin model and native clip/folder commands with source-path and live-parent verification.

### 13. Proxy and relink intelligence

**User experience:** the assistant can distinguish originals from proxies, identify proxy-only assets, inspect missing media, discover relink candidates, and perform governed relinking instead of silently substituting files.

**Technical basis:** proxy/relink tools expose authoritative bin state, file-backed source metadata, candidate discovery, and explicit mutation paths.

### 14. Persistent media-evidence layer

**User experience:** analysis does not have to disappear when the chat scrolls away. VibeCut can preserve machine-derived observations about media and use them in later editing decisions.

**Technical basis:** a project-local `.vibecutmedia.json` sidecar stores bounded, versioned evidence records keyed by source identity, source fingerprint, extractor identity/version, evidence kind, frame range, confidence, provenance time, and metadata.

A central invariant is that derived evidence remains derived evidence. Model output is not silently promoted into authoritative project state.

### 15. Deterministic media analysis

**User experience:** the assistant can analyze footage for concrete properties before asking a model to guess.

Current deterministic evidence paths include:

- source metadata;
- silence regions;
- loudness context;
- scene/shot boundaries;
- black frames;
- freeze regions;
- blur measurements;
- pairwise MPEG-7 video similarity.

**Technical basis:** extractors use FFmpeg/Kdenlive-native analysis paths and persist versioned evidence against source fingerprints so stale results can be detected.

### 16. Searchable transcripts and subtitles

**User experience:** users can generate subtitles, inspect them, search spoken content, edit subtitle text, and delete subtitle entries through stable identifiers.

**Technical basis:** the Whisper pipeline runs as a managed asynchronous job and records transcript provenance against an exact timeline snapshot/range. Subtitle editing operates against Kdenlive subtitle models rather than raw text files alone.

### 17. Dead-air review and governed cleanup

**User experience:** VibeCut can identify silence-backed candidate removals, show the exact timeline ranges involved, and perform lift or conservative ripple cleanup instead of blindly deleting every quiet region.

**Technical basis:** dead-air planning maps persisted source evidence to exact live timeline instances. Execution revalidates the evidence and timeline topology, uses native cuts/deletions, supports linked-group-aware cleanup, and rolls back on failure.

### 18. Repeated-take intelligence and execution

**User experience:** repeated lines or takes can be grouped for editorial review. The system exposes available quality evidence without pretending that an algorithm automatically knows which performance is creatively “best.” After the user explicitly selects the take to keep, VibeCut can now execute the rejected-take removals instead of stopping at a plan.

**Technical basis:** transcript/subtitle similarity produces candidate groups; review tooling adds source/timeline context and measured quality evidence; explicit selection plans preserve the human/editorial choice. `repeated_take_selection_execute` revalidates the current review, requires explicit lift/ripple semantics, rejects overlapping removal ranges, applies removals right-to-left through the governed native range-removal transaction, rolls back on failure, verifies each mutation, and commits the successful batch as one Undo step.

### 19. Take-quality context without invented scores

**User experience:** instead of returning a mysterious “quality = 87” number, the assistant can show what evidence exists: loudness, blur, silence overlap, black/freeze overlap, shot boundaries, and freshness.

**Technical basis:** `take_quality_context` aggregates current evidence for an exact source range while keeping missing or stale evidence explicit.

### 20. Long-running jobs with progress and cancellation

**User experience:** transcription, rendering, and other expensive tasks do not need to block the chat or pretend they finished instantly. They can expose progress, completion, failure, and cancellation state.

**Technical basis:** `VibeCutJobManager` provides stable job IDs, lifecycle states, progress, cancellation requests, and plan-runtime synchronization.

### 21. Native render and preflight workflow

**User experience:** users can inspect the render presets actually installed on the machine, ask VibeCut for deterministic recommendations, check project readiness, and start a managed render.

**Technical basis:** render tools use Kdenlive’s `RenderRequest` / `kdenlive_render` path, project preflight, proxy/original checks, installed preset metadata, output verification, and managed asynchronous execution.

### 22. Multi-sequence awareness

**User experience:** the assistant can understand that a project may contain more than the currently visible timeline and can inspect nested/other sequences explicitly.

**Technical basis:** sequence tools enumerate Kdenlive sequence clips, stable UUIDs, active/open state, durations, usage, and live timeline model information.

### 23. Provider-neutral ML extractor architecture

**User experience:** future OCR, diarization, vision, tagging, or other learned extractors do not need privileged write access to the project or a bespoke integration into the chat loop.

**Technical basis:** `VibeCutExtractorProvider` provides capability discovery and normalized requests while exposing providers only to a constrained evidence sink. Providers receive validated source identity/range information and can persist only through the governed evidence layer.

### 24. Model/provider extension seam

**User experience:** VibeCut is not structurally tied to one model vendor.

**Technical basis:** provider registries normalize model-provider construction and requests behind stable interfaces. Additional adapters and per-task routing are sequenced after evidence/task contracts stabilize so providers do not dictate the architecture.

### 25. Project rules, policy, and memory

**User experience:** a project can carry local instructions and governance that survive beyond a single chat session.

**Technical basis:** `.vibecutrules`, `.vibecutpolicy.json`, and `.vibecutmemory.json` provide bounded project-local rules, policy overrides, and durable assistant/project memory.

### 26. Controlled scripting surface

**User experience:** advanced automation can be extended without turning the AI assistant into an unrestricted shell.

**Technical basis:** VibeScript provides a constrained scripting path designed to remain inside VibeCut’s authority and tool boundaries.

### 27. Context compaction for long editing sessions

**User experience:** longer sessions can remain usable without corrupting the relationship between tool calls, tool results, and the current project state.

**Technical basis:** conversation compaction preserves structured tool protocol and essential project context rather than naively truncating message history.

---

## Next capability sequence

The remaining capability work is deliberately dependency-ordered rather than a flat wishlist:

1. **Golden editing fixtures and quantitative verified-success / Undo-fidelity evaluation** across the existing mutation surface.
2. **Evidence depth:** diarization + user speaker naming, OCR/on-screen text, richer noise/room-tone/audio-event analysis, and visual subject/object/action evidence.
3. **Retrieval:** text/visual embeddings, cross-modal semantic search, and stronger duplicate/near-duplicate detection.
4. **Editorial synthesis:** rough cuts, highlights/shorts, B-roll planning, pacing analysis, and narrative analysis.
5. **Presentation/audio breadth:** richer title shapes/images/templates/brand packs and mixer gain/pan/solo plus mix type/parameter edits only where Kdenlive exposes a safe backend seam.
6. **Provider scale:** additional model/provider adapters and per-task routing after task/evidence contracts are stable.

`TODO.md` is the dependency-ordered full roadmap; `VIBECUT_ROADMAP_STATUS.md` is the concise live-state ledger.

---

## What users should expect

halthinks/VibeCut is being built around several behavioral expectations:

1. **Inspect before editing.** The agent should use live project/media state where possible.
2. **Plan consequential changes.** Large edits should be reviewable and revision-bound.
3. **Use native Kdenlive mechanisms.** AI should operate the editor, not replace the editor with an incompatible shadow model.
4. **Verify after editing.** A successful API call is not automatically treated as proof that the intended edit exists.
5. **Preserve Undo and recovery.** Reversible work should remain reversible.
6. **Keep observations, predictions, and preferences distinct.** Measured evidence is different from model judgment, and editorial taste ultimately belongs to the user.
7. **Expose uncertainty and missing evidence.** The system should say when it does not know.
8. **Respect project changes made by the human.** Stale plans must not silently execute against a changed timeline.

---

## Architecture in one view

```text
User intent
   ↓
Live project inspection + media evidence retrieval
   ↓
EditPlan bound to project revision
   ↓
Validation + policy + Review / Auto / Turbo authorization
   ↓
Native Kdenlive operations
   ↓
JobManager boundary for long-running work
   ↓
Live-state verification + evidence/diff
   ↓
Undo / continue / revise
```

The important distinction is that the language model is not the project database. Kdenlive remains authoritative for project state; persisted evidence remains provenance-bearing evidence; the runtime governs how proposed actions become actual edits.

For deeper implementation details see:

- `VIBECUT_ARCHITECTURE.md`
- `DESIGN_SPECS.md`
- `VIBECUT_ROADMAP_STATUS.md`
- `BUILDING_VIBECUT.md`
- `TODO.md`

---

## Building and verification

The current development branch tracks a recent Kdenlive development stack and therefore requires matching Qt/KF6/MLT dependencies.

On a supported Debian build host:

```bash
bash scripts/vibecut-bootstrap-debian.sh
```

The repository verification gate is:

```bash
bash scripts/vibecut-verify.sh
```

That repository-local gate is authoritative: it validates the dependency contract, configures CMake, compiles the tree, and runs the `vibecut*` CTest suite. Branch GitHub Actions may supplement the halthinks hardening cycle, but they do not replace the repository-local gate or the hands-on editor smoke matrix.

## Installation packaging

halthinks/VibeCut carries its own packaging layer in addition to the packaging inherited from Kdenlive/VibeCut. The fork-specific package path is intended to install alongside a normal Kdenlive installation rather than impersonating or replacing the distribution’s Kdenlive package.

See `packaging/vibecut/README.md` for the halthinks/VibeCut package contract and package-generation command.

## Development status

The integration branch now includes source implementation for governed timeline-range removal and final repeated-take selection execution. Those capabilities—and the broader integration branch—are not considered release-ready until the complete build, VibeCut test suite, package checks, and hands-on editing/Undo smoke matrix are green. Source implementation is not treated as proof of runtime correctness.

No claim in this README should be interpreted as replacing Kdenlive’s own feature set or authorship. The purpose of this layer is to describe the additional agentic/editor-governance capability introduced by the halthinks fork.

---

# Original VibeCut README layer — preserved

The following is the original VibeCut project’s README layer, retained to preserve project lineage and credit.

# vibecut

vibecut is an AI-scriptable fork of [Kdenlive](https://kdenlive.org) — a chat panel that can drive and extend the editor live, from natural language, instead of just triggering pre-built features through menus. Ask it to remove background noise, generate subtitles, or apply an effect, and it does it directly on your live project — the screenshot above is a real run: chat-driven AI noise removal (DeepFilterNet) and a full-project GPU-Whisper subtitle transcription, both landed on the timeline by asking for them. Video editing's answer to [vibecad](https://github.com/10-X-eng/vibecad).

Everything below this point is Kdenlive's own upstream documentation, unmodified.

---

![](data/pics/kdenlive-logo.png)

# Kdenlive

Kdenlive is a powerful, free and open-source video editor that brings professional-grade video editing capabilities to everyone. Whether you're creating a simple family video or working on a complex project, Kdenlive provides the tools you need to bring your vision to life.

For more information about Kdenlive's features, tutorials, and community, please visit our [official website](https://kdenlive.org).

There you can also find downloads for both stable releases and experimental daily builds for Kdenlive.

## Contributing to Kdenlive

Kdenlive is a community-driven project, and we welcome contributions from everyone! There are many ways to contribute beyond coding:

- Help translate Kdenlive into your language
- Report and triage bugs
- Write documentation
- Create tutorials
- Help other users on forums and bug trackers

Visit [kdenlive.org](https://kdenlive.org) to learn more about non-code contributions.

## Developer Information

### Technology Stack

Kdenlive is written in C++ and is using these technologies and frameworks:

- **Core Framework**: MLT for video editing functionality
- **GUI Framework**: Qt and KDE Frameworks 6
- **Additional Libraries**: frei0r (video effects), LADSPA (audio effects)

### Getting Started

1. Check out our [build instructions](dev-docs/build.md) to set up your development environment
2. Familiarize yourself with the [architecture](dev-docs/architecture.md) and [coding guidelines](dev-docs/coding.md)
4. If the MLT library is new to you check out [MLT Introduction](dev-docs/mlt-intro.md)
3. Join our Matrix channel `#kdenlive-dev:kde.org` for developer discussions and support

### Contributing Code

Kdenlive's primary development happens on [KDE Invent](https://invent.kde.org/multimedia/kdenlive). While we maintain a GitHub mirror, all code contributions should be submitted through KDE's GitLab instance. For more information about KDE's development infrastructure, visit the [KDE GitLab documentation](https://community.kde.org/Infrastructure/GitLab).

### Finding Things to Work On

- Browse open issues on [KDE Invent](https://invent.kde.org/multimedia/kdenlive/-/issues), for example those labeled with [First Task](https://invent.kde.org/multimedia/kdenlive/-/issues?label_name%5B%5D=First%20Task)
- Check the [KDE Bug Tracker](https://bugs.kde.org) for reported issues
- Look for issues tagged with "good first issue" or "help wanted"

Need help getting started? Join our Matrix channel `#kdenlive-dev:kde.org` - our community is friendly and always ready to help new contributors!
