# VibeCut Mutation Evaluation Contract

This document defines the quantitative golden-evaluation layer for governed VibeCut editing.

The purpose is not to prove that a tool call returned success. The purpose is to measure whether a consequential edit produced the requested live project state and whether the editor's transaction semantics remain reversible.

## Metrics

Every applied golden mutation is evaluated with three normalized scores in `[0, 1]`:

- **Verified success** — requested postcondition leaves matched in the observed post-edit live state. Unspecified live state is ignored so a fixture can assert only the state it owns.
- **Undo fidelity** — complete canonical pre-edit mutation state matched after one Undo. Unexpected or missing canonical structure counts against the score.
- **Redo fidelity** — complete canonical post-edit mutation state matched after Redo. This is intentionally measured against the state that was actually committed; verified-success independently catches a wrong committed result.

Release-quality mutation fixtures default to a threshold of `1.0` for all three metrics. A fixture may lower a threshold only when the metric itself is intentionally approximate and the reason is documented.

## Canonical live mutation state

`VibeCutProjectSnapshot::captureMutationStateV1()` is the active-editor capture seam and `VibeCutProjectSnapshot::mutationStateV1(model)` is the model-bound form used by headless Kdenlive fixtures. Both use the same canonical implementation. The state deliberately excludes the project revision counter: Undo or Redo may advance bookkeeping revisions while restoring the same editable project state, so revision equality is not an Undo-fidelity requirement.

Schema `vibecut_mutation_state_v1` captures, in deterministic timeline order:

- timeline duration, aggregate item counts and group data;
- master effect stack state;
- track identity/order/name/type, lock/active/hidden/mute state, same-track mix count and track effect stack;
- clip identity, bin identity/name, timeline position/duration, source in/out, speed and serialized effect stack;
- composition identity, timing, asset id, active state and serialized parameter JSON;
- subtitle identity/layer/timing/text/style/name/margins/effects/dialogue state plus subtitle lock/disable state.

Effect stacks are serialized through Kdenlive's existing backend XML representation so parameter/keyframe changes are visible to fidelity comparisons rather than reduced to a count. This state is for mutation verification, not a user-facing project interchange format; schema changes must therefore be versioned instead of silently changing the meaning of old golden results.

## Mutation outcomes

The evaluator has three explicit outcomes:

1. `Applied` — the mutation committed and must satisfy its requested postcondition plus required Undo/Redo observations.
2. `Refused` — policy or live-state preconditions rejected the mutation. Refusal is successful only if canonical project state is unchanged; an expected refusal reason can also be required.
3. `RolledBack` — execution began but the transaction failed. Success requires canonical state to be restored to the pre-edit state and rollback verification to be explicit.

This prevents a refusal or rollback from being counted as successful merely because an error string was returned.

## Deterministic contract corpus

`tests/dataset/vibecut/golden_mutation_cases.json` establishes evaluator-contract fixtures for:

- successful ripple `timeline_range_remove` with exact Undo/Redo round-trip;
- stale-plan refusal with no mutation;
- repeated-take overlap refusal with no mutation;
- locked-track refusal with no mutation;
- transaction rollback after partial failure with restored canonical state.

The fixtures are consumed by `tests/vibecutevaltest.cpp` and validate the scoring/refusal contract itself.

## Executable live/headless fixture baseline

The same contract is now bound to real Kdenlive model operations in source:

- `tests/vibecutmutationlivetest.cpp`
  - executes native accumulated ripple range removal on a real headless `TimelineItemModel`;
  - measures requested postcondition plus exact pre → Undo and post → Redo state fidelity;
  - proves locked-track refusal leaves canonical state unchanged.
- `tests/vibecutmutationrollbacktest.cpp`
  - executes a real timeline move inside `VibeCutPlanRuntime`, deliberately reports failure after mutation, and requires the checkpoint macro rollback to restore exact pre-edit state.
- `tests/vibecutmutationstalelivetest.cpp`
  - proposes a revision-bound mutating plan, performs an independent real editor mutation, then requires approval to refuse the now-stale plan before its handler is invoked and without another Undo-stack or project-state change.
- `tests/vibecutrepeatedtakemutationtest.cpp`
  - binds overlap refusal to unchanged canonical state;
  - executes the same model-bound destructive core used by production repeated-take selection;
  - requires a successful multi-range batch to add exactly one Undo-stack command;
  - requires one real `DocUndoStack::undo()` to restore the exact canonical pre-state and one real `redo()` to restore the exact committed post-state.

Production `repeated_take_selection_execute` still performs fresh review and explicit keep-choice revalidation immediately before it enters the model-bound destructive core. Extracting that core for headless testing does not bypass or weaken the production governance path.

## Verification status

The source fixture baseline is implemented, but it is not equivalent to a passing release gate. These tests must still be compiled and executed on a host with the required Kdenlive/Qt/MLT development stack through repository-local verification. Raw failures should retain the canonical states and score output so state mismatches are diagnosable rather than converted into a boolean-only failure.

The release gate remains unchanged: compile/link, all `vibecut*` tests, package smoke and hands-on editor smoke must pass before the branch can be described as release-ready.
