# VibeCut Mutation Evaluation Contract

This document defines the first quantitative golden-evaluation layer for governed VibeCut editing.

The purpose is not to prove that a tool call returned success. The purpose is to measure whether a consequential edit produced the requested live project state and whether the editor's transaction semantics remain reversible.

## Metrics

Every applied golden mutation is evaluated with three normalized scores in `[0, 1]`:

- **Verified success** — requested postcondition leaves matched in the observed post-edit live state. Unspecified live state is ignored so a fixture can assert only the state it owns.
- **Undo fidelity** — complete canonical pre-edit state matched after one Undo. Unexpected or missing canonical structure counts against the score.
- **Redo fidelity** — complete canonical post-edit state matched after Redo. This is intentionally measured against the state that was actually committed; verified-success independently catches a wrong committed result.

Release-quality mutation fixtures default to a threshold of `1.0` for all three metrics. A fixture may lower a threshold only when the metric itself is intentionally approximate and the reason is documented.

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

## Next hardening step

These JSON fixtures are the deterministic contract baseline, not a substitute for hands-on editor verification. The next evaluation slice must bind the same fixture IDs and score thresholds to tiny executable Kdenlive project fixtures and live VibeCut tool invocations. Those runs should capture canonical state before edit, after edit, after Undo and after Redo, then feed those observations into `VibeCutEvaluator::evaluateMutation`.

The release gate remains unchanged: compile/link, `vibecut*` tests, package smoke and hands-on editor smoke must pass before the branch can be described as release-ready.
