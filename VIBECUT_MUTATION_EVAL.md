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

`VibeCutProjectSnapshot::captureMutationStateV1()` is the live-state capture seam used by executable mutation fixtures. It deliberately excludes the project revision counter: Undo or Redo may advance bookkeeping revisions while restoring the same editable project state, so revision equality is not an Undo-fidelity requirement.

Schema `vibecut_mutation_state_v1` captures, in deterministic timeline order:

- timeline duration and group data;
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

## Initial deterministic fixture corpus

`tests/dataset/vibecut/golden_mutation_cases.json` establishes contract fixtures for:

- successful ripple `timeline_range_remove` with exact Undo/Redo round-trip;
- stale-plan refusal with no mutation;
- repeated-take overlap refusal with no mutation;
- locked-track refusal with no mutation;
- transaction rollback after partial failure with restored canonical state.

The fixtures are consumed by `tests/vibecutevaltest.cpp` and test the scoring/refusal contract itself.

## Live fixture binding

The JSON fixtures are the deterministic contract baseline, not a substitute for hands-on editor verification. Executable Kdenlive fixture runs should bind the same fixture IDs and thresholds to live VibeCut operations using this sequence:

1. Load the tiny golden project and call `captureMutationStateV1()` for `preEditState`.
2. Execute or deliberately refuse the governed mutation.
3. Capture `postEditState` and evaluate the requested postcondition.
4. For an applied mutation, execute exactly one Undo, capture `undoState`, then Redo and capture `redoState`.
5. Feed all four observations into `VibeCutEvaluator::evaluateMutation`.
6. Keep the raw states and score object as reproducible test evidence when a fixture fails.

The release gate remains unchanged: compile/link, `vibecut*` tests, package smoke and hands-on editor smoke must pass before the branch can be described as release-ready.
