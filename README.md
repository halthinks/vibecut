![](data/pics/vibecut-screenshot.png)

# VibeCut

**VibeCut is an agentic nonlinear video editor built on Kdenlive.** It adds a state-aware assistant that can inspect the live project, search media/transcript evidence, formulate multi-step edit plans, execute approved changes through Kdenlive's native APIs, verify the resulting state, and preserve human control through revision guards, trust policy, jobs, and undo checkpoints.

The goal is not a collection of AI buttons. The goal is an editing agent with the same kind of broad, composable access that coding agents have inside an IDE — while treating the timeline as authoritative state instead of letting model text become truth.

## What works on the current VibeCut branch

VibeCut can currently:

- inspect timeline clips and selection;
- apply verified audio cleanup effects;
- set up GPU-capable Whisper and generate subtitles asynchronously;
- search subtitle/transcript text and a project media index;
- edit/delete subtitles by stable subtitle id;
- move, split, trim, ripple-trim, and delete clips through Kdenlive's undoable timeline APIs;
- list/add/remove point and range guides for review, B-roll, candidate cuts, and semantic annotations;
- list the transitions installed in the current Kdenlive runtime and insert verified compositions;
- create real Kdenlive title-bin assets and insert them on the timeline;
- list installed render presets and start verified asynchronous renders through Kdenlive's native `RenderRequest` / `kdenlive_render` path;
- track long-running work through a shared JobManager;
- load per-project `.vibecutrules`;
- compact long conversations without breaking tool-use/tool-result protocol;
- expose provider, lifecycle-event, context-provider, and media-index extension seams.

## Governed editing model

Read-only investigation can run immediately. Project changes and external side effects become an `EditPlan` first and are executed by a deterministic runtime rather than by a free-form model loop.

```text
User intent
   ↓
Project inspection / media retrieval
   ↓
EditPlan + base project revision
   ↓
Validation + trust policy
   ↓
Review / Auto / Turbo authorization
   ↓
Checkpointed native Kdenlive operations
   ↓
Async JobManager boundaries where needed
   ↓
Live-state verification + project diff
```

The default **Review** mode asks before side effects. **Auto** may auto-run governed reversible edits while major/external work still requires review. **Turbo** can auto-run governed work but does not override tools explicitly marked confirmation-required or irreversible.

A plan is rejected if its captured project revision is stale. Contiguous synchronous project edits are grouped in Kdenlive's undo stack; a failed checkpoint is rolled back rather than reported as success. Async tools must return a stable job id and the plan pauses until the job reaches a terminal state.

## Extension hooks

VibeCut now has stable seams instead of requiring every feature to modify the chat loop:

- `VibeCutToolSurface` — schemas, governance metadata, dispatch, and tool decorators;
- `VibeCutModelProviderRegistry` — model-provider factories and provider request normalization seam;
- `VibeCutHooks` — lifecycle events plus named structured context providers;
- `VibeCutMediaIndex` — shared retrieval contract for transcript, clips, and future scene/OCR/audio/embedding extractors;
- `VibeCutJobManager` — progress, failure, cancellation state, and async-plan synchronization;
- `VibeCutPlanRuntime` — revision-aware authorization, dependency order, checkpoints, verification, and diffs.

See `VIBECUT_ARCHITECTURE.md`, `DESIGN_SPECS.md`, and `TODO.md` for the implementation contract and remaining roadmap.

## Local verification — no GitHub Actions required

VibeCut development does not require GitHub CI. From a machine with Kdenlive build dependencies installed:

```bash
bash scripts/vibecut-verify.sh
```

That configures a local build, builds the project, and runs the `vibecut*` test set with `ctest`. Override `VIBECUT_BUILD_DIR`, `VIBECUT_BUILD_TYPE`, `VIBECUT_JOBS`, or `VIBECUT_CMAKE_GENERATOR` as needed.

## Status

VibeCut is under active development. The agent/runtime architecture is substantially ahead of the original single-tool prototype, but the integration branch should still receive a full local Kdenlive compile/test pass and hands-on editing test before an upstream pull request is proposed.

Everything below this point is Kdenlive's own upstream documentation, intentionally retained for the fork.

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
3. If the MLT library is new to you check out [MLT Introduction](dev-docs/mlt-intro.md)
4. Join our Matrix channel `#kdenlive-dev:kde.org` for developer discussions and support

### Contributing Code

Kdenlive's primary development happens on [KDE Invent](https://invent.kde.org/multimedia/kdenlive). While we maintain a GitHub mirror, all code contributions should be submitted through KDE's GitLab instance. For more information about KDE's development infrastructure, visit the [KDE GitLab documentation](https://community.kde.org/Infrastructure/GitLab).

### Finding Things to Work On

- Browse open issues on KDE Invent, for example those labeled First Task
- Check the KDE Bug Tracker for reported issues
- Look for issues tagged "good first issue" or "help wanted"

Need help getting started? Join the Kdenlive Matrix development channel.
