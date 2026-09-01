# VibeCut Release Smoke Checklist

This is the hands-on gate that follows a green compile/test/package run. A release candidate is not ready merely because it builds.

## 1. Installer / launch

- Install the generated `vibecut-halthinks` `.deb` on a clean Debian-compatible machine.
- Confirm it coexists with a normal Kdenlive install.
- Launch from the desktop entry and from `vibecut-halthinks`.
- Open an existing `.kdenlive` project and create/save a new project.
- Uninstall and confirm the normal Kdenlive installation remains intact.

## 2. Governed edit lifecycle

For each representative edit family, verify:

1. assistant inspects live state;
2. mutation becomes an EditPlan;
3. plan carries current project revision;
4. approval behavior matches trust mode;
5. native Kdenlive edit executes;
6. live-state postconditions verify;
7. project diff reflects the actual change;
8. Undo restores the previous project state;
9. Redo restores the verified edited state where applicable;
10. stale plans are refused after an intervening edit.

Representative families:

- clip move, split, trim, ripple and delete;
- `timeline_range_remove` in `lift` and `ripple` modes;
- bulk edit;
- group edit;
- track create/move/rename/lock/enable/delete;
- effect add/parameter/keyframe/remove;
- transition/composition and same-track mix;
- title create/edit;
- bin import/folder/source replacement;
- relink/proxy actions.

### `timeline_range_remove` safety cases

- Lift an exact range spanning representative audio/video material; verify selected-track content is gone only inside the requested range and Undo restores the canonical pre-edit state.
- Ripple-remove an exact range; verify downstream material shifts by exactly the removed width and Undo/Redo reproduce the two canonical states.
- Include a locked track in the target set; verify the tool fails closed before partial mutation.
- Supply an invalid/out-of-bounds range; verify no mutation and no success claim.
- Verify the tool remains classified as reversible `MajorEdit` and project-mutating in the live policy surface.

## 3. Trust / policy

- Review mode confirms mutations and external side effects.
- Auto mode automatically runs only policy-eligible reversible work.
- Turbo mode still respects explicit confirmation/irreversible restrictions.
- `.vibecutpolicy.json` deny/always-confirm/auto-allow behavior is reflected in schemas, policies and invocation.
- `.vibecutrules` is loaded for the project.
- `.vibecutmemory.json` survives restart where expected.

## 4. Long-running jobs

- Start Whisper subtitle generation and observe JobManager progress.
- Cancel a running Whisper job and confirm terminal cancellation state.
- Run deterministic media analysis and confirm evidence persistence.
- Start a render, observe progress, cancel once, then complete a second render.
- Verify successful render output exists and is non-empty.

## 5. Evidence / media intelligence

- Generate and inspect `.vibecutmedia.json`.
- Confirm source fingerprint and extractor-version freshness behavior.
- Verify silence, loudness, shots, black, freeze and blur evidence on known fixtures.
- Verify Whisper transcript segments carry timeline/source provenance.
- Verify video similarity comparison returns persisted pair evidence.
- Modify/replace a source and confirm stale evidence is surfaced rather than silently reused.

## 6. Editorial intelligence

- Dead-air plan is reviewable before mutation.
- Lift cleanup produces the proposed removals and Undo restores them.
- Ripple cleanup verifies topology constraints before editing.
- Linked/group-aware dead-air cleanup preserves linked relationships where required.
- Repeated-take candidate grouping matches transcript evidence.
- Repeated-take review exposes quality evidence without inventing a winner.
- Selection planning only follows the user's explicit keep choice.
- `repeated_take_selection_execute` re-runs/revalidates the current review immediately before mutation.
- Repeated-take execution requires explicit `lift` or `ripple` semantics and removes only rejected ranges.
- Multiple rejected ranges execute right-to-left so earlier absolute coordinates remain valid under ripple semantics.
- Overlapping rejected ranges are refused before mutation.
- A failure in any rejected range rolls the whole batch back; no partial success is reported.
- A successful repeated-take batch is one native Undo step; Undo restores the canonical pre-selection timeline and Redo restores the verified selected-take result.
- Verify `repeated_take_selection_execute` remains reversible `MajorEdit` and project-mutating in the live policy surface.

## 7. Project resilience

- Missing original media is distinguished from proxy-only media.
- Single and batch relink verify the resulting live source state.
- Directory discovery does not silently select ambiguous candidates.
- Project preflight blocks inappropriate long jobs.
- Multi-sequence inspection reports active/open sequences correctly.

## 8. Conversation / provider robustness

- Long conversation compaction preserves valid tool-call/tool-result ordering.
- Provider credentials are read through the configured secret store and are not written to project files.
- Provider hot reload does not corrupt the current project.
- A provider/extractor failure is reported as failure and does not manufacture evidence or project success.

## 9. Quantitative release evidence

Until the full golden-fixture suite is landed, record the applicable representative checks in a reproducible release note. As Phase 6 matures, these become machine-comparable release metrics:

- verified-success rate: requested postcondition vs observed live state;
- Undo fidelity: canonical pre-edit state vs state restored after Undo;
- Redo fidelity: canonical verified edited state vs state restored after Redo;
- false-success count must be zero for destructive edit cases;
- stale-plan execution count must be zero;
- partial-mutation-on-failure count must be zero.

## Release decision

A candidate may be merged to `vibecut` only when:

- CI compile/link/tests are green;
- Debian package creation and install/remove smoke are green;
- the applicable hands-on checks above pass, including the new timeline-range and repeated-take execution paths;
- no known failure can result in a false success, silent destructive edit, stale-plan execution or fabricated evidence.

An upstream PR to original VibeCut is a separate decision. The halthinks/VibeCut fork remains a complete product line whether or not upstream chooses to merge any subset of these capabilities.
