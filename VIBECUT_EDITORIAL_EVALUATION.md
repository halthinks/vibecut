# VibeCut Editorial Evaluation Protocol

**Protocol version:** 1  
**Scope:** rough-cut, highlight/short, and B-roll proposal evaluation  
**Authority:** evaluation and human-review evidence only  
**Execution authority:** none

This document defines how VibeCut measures editorial proposal behavior without turning model scores, reference agreement, or subjective human ratings into facts or automatic edit authority.

## Core invariant

**Editorial evaluation may inform whether an execution translator should be designed, but evaluation output never authorizes timeline mutation.**

The governed path remains:

`current evidence/context → proposal → evaluation/review → explicit approval → governed EditPlan translation → normal authorization → native mutation → verification → Undo/Redo evaluation`

The `approved proposal → EditPlan translation` stage is intentionally not implemented yet.

## 1. Three separate evidence classes

VibeCut keeps these measurements separate because they answer different questions.

### 1.1 Structural/reference agreement

`editorial_selection_evaluate` compares an actual candidate-ID selection/order with an **explicit** reference supplied by the evaluator.

Metrics:
- precision;
- recall;
- F1;
- exact set agreement;
- exact order agreement;
- pairwise relative-order agreement among candidates common to both sequences;
- explicit missed and unexpected candidate IDs.

These metrics answer: **“How closely did this proposal match this declared reference?”**

They do **not** answer: “Is this objectively a good edit?”

The deterministic starter corpus is `tests/dataset/vibecut/editorial_selection_cases.json`. Those cases verify metric semantics and regression behavior only; they are not a representative editorial benchmark.

### 1.2 Descriptive proposal analysis

Pacing and narrative tools describe proposal/source structure without normative thresholds.

Examples:
- segment/shot duration distributions;
- duration variability;
- silence coverage;
- transcript gaps/density;
- chronology/reordering;
- semantic/lexical adjacency continuity;
- relative section-boundary candidates;
- relative repetition candidates.

These answer: **“What measurable structure does this proposal have?”**

They do not label a pace, section boundary, or narrative as intrinsically good/bad.

### 1.3 Blinded human review

`VibeCutEditorialReview-v1` records subjective human judgment under one fixed rubric:
1. `objective_relevance`;
2. `narrative_coherence`;
3. `pacing_fit`;
4. `source_fidelity`;
5. `overall_preference`.

Each criterion is an integer 1–5.

Every v1 review must:
- declare `blind=true`;
- identify `case_id`, opaque `candidate_id`, `reviewer_id`, and `task_type`;
- bind to the exact 64-hex `context_sha256`;
- bind to the exact 64-hex `proposal_id` actually reviewed;
- use only the fixed rubric fields;
- remain `quality_ground_truth=false` and `mutation_authority=none`.

Aggregates require unique reviewer IDs and the exact same case/candidate/task/context/proposal. They report mean, standard deviation, minimum and maximum per criterion. They do not produce a pass/fail or auto-execution result.

## 2. Blinding protocol

For comparative human evaluation:

1. Freeze the input media/project evidence and produce one exact `context_sha256`.
2. Produce candidate proposals and preserve each proposal's cryptographic `proposal_id`.
3. Build a case manifest with opaque display labels that do not disclose provider/model/version or internal ranking.
4. Randomize presentation order outside the proposal-generation system.
5. Reviewers see the source/reference material needed for the task and opaque candidate labels only.
6. Store one `VibeCutEditorialReview-v1` record per reviewer/candidate.
7. Do not reveal aggregate scores or model/provider identity to a reviewer before that review is submitted.
8. Compare versions/providers only on the same frozen cases and equivalent evidence budget.

A review collected for one proposal hash may not be reused after that proposal or context changes.

## 3. Reference construction

A structural selection reference must be explicit and provenance-bearing. VibeCut does not manufacture its own reference answer.

Permitted reference sources include:
- `golden`: a deliberately curated reference fixture;
- `human_consensus`: a separately constructed human reference from a documented process.

Reference disagreement should be preserved rather than hidden. Where multiple plausible edits exist, retain multiple references or human-review distributions instead of forcing one sequence to masquerade as ground truth.

## 4. Dataset discipline

Representative evaluation cases should span materially different workloads, for example:
- single-speaker interview;
- multi-speaker interview/podcast;
- tutorial/explainer;
- product/demo footage;
- mixed A-roll/B-roll source pools;
- short-form/highlight objectives;
- noisy or incomplete transcript/evidence conditions;
- projects with repeated takes and near-duplicates.

Case manifests must bind to exact context/proposal identities. Model/provider/version and evidence-runtime provenance should be stored in evaluation metadata outside blinded display labels.

No minimum number of cases/reviewers is hard-coded by this protocol yet. Sample-size and acceptance decisions must be justified from observed variability and intended product risk, not invented in advance.

## 5. Comparing proposal versions/providers

A valid comparison should report, separately:
- structural/reference agreement metrics;
- human-review criterion distributions and reviewer count;
- pacing/narrative descriptive measurements where relevant;
- retrieval/evidence coverage and freshness;
- latency/cost/resource measurements where relevant;
- failure/refusal rate;
- exact model/provider/version/configuration provenance.

Do not collapse these into a single opaque “quality score.”

## 6. Execution gate

The synthesis execution gate remains **CLOSED**.

Designing an approved-proposal → governed `EditPlan` translator requires, at minimum:
- representative evaluation cases rather than only structural unit fixtures;
- real blinded reviews bound to exact proposal/context hashes;
- retrieval/evidence runtime verification and calibration on representative media;
- documented failure modes and disagreement;
- authoritative Kdenlive compile/test/smoke success;
- an explicit translation design showing every proposed edit resolves to existing governed native operations with preconditions, verification and Undo/Redo fidelity.

No human-review mean, F1 value, model confidence, or alternative-comparison score automatically opens this gate.

## 7. Future execution translator invariant

If/when the gate is opened, the translator must not execute a proposal object directly. It must:
1. revalidate the current project revision and proposal/context hashes;
2. resolve every candidate/reference to current authoritative project/source state;
3. refuse unresolved B-roll source excerpts or stale evidence;
4. emit an ordinary revision-bound governed `EditPlan` composed only of registered native tools;
5. pass the existing authorization/policy layer;
6. execute through the existing checkpoint/rollback runtime;
7. verify requested postconditions;
8. measure Undo/Redo fidelity through the existing mutation evaluator.

There will be no separate privileged “AI rough-cut executor.”
