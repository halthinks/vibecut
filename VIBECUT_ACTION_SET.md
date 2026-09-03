# VibeCutActionSet-v1

`VibeCutActionSet-v1` is the fixed zero-shot action vocabulary used by the built-in local X-CLIP provider.

**Canonical SHA-256:** `005794f327b4bbf0cea1dd3801009f1c9c51066fec0bb129b7a01b0f8d5520fc`

The hash is computed over UTF-8 lines of:

`<zero-based-label-id>\t<label>\t<prompt>`

joined with `\n` and no trailing newline. Both the Python helper and C++ provider must agree on this hash before action evidence is accepted.

## Authority and score semantics

- Provider output has `authority = model_prediction`.
- Scores use `softmax_over_fixed_action_set` semantics: they are relative compatibility scores among this exact candidate vocabulary, **not calibrated probabilities that an action is factually occurring**.
- Every prediction window preserves the exact eight sampled source frames supplied to X-CLIP.
- A prediction window does not mean every frame in the window was directly observed by the model.
- `media_action_tracks` may group repeated same-label windows only when source fingerprint, extractor, model revision, taxonomy and action-set hash match. The result remains a derived prediction summary.
- This taxonomy is intentionally fixed in v1. Adding, removing, reordering or rewording a label/prompt requires a new action-set version/hash so scores from different candidate sets are never silently treated as comparable.

## Canonical vocabulary

| ID | Label | Prompt |
|---:|---|---|
| 0 | `no_clear_action` | a video with no clear action from the listed set |
| 1 | `talking` | a video of a person talking |
| 2 | `presenting` | a video of a person presenting to an audience |
| 3 | `talking_to_camera` | a video of a person talking directly to the camera |
| 4 | `walking` | a video of a person walking |
| 5 | `running` | a video of a person running |
| 6 | `sitting` | a video of a person sitting |
| 7 | `standing` | a video of a person standing |
| 8 | `driving` | a video of a person driving a vehicle |
| 9 | `riding_bicycle` | a video of a person riding a bicycle |
| 10 | `cooking` | a video of a person cooking |
| 11 | `eating` | a video of a person eating |
| 12 | `drinking` | a video of a person drinking |
| 13 | `typing` | a video of a person typing on a keyboard |
| 14 | `writing` | a video of a person writing |
| 15 | `reading` | a video of a person reading |
| 16 | `using_phone` | a video of a person using a phone |
| 17 | `using_computer` | a video of a person using a computer |
| 18 | `assembling` | a video of a person assembling something |
| 19 | `repairing` | a video of a person repairing something |
| 20 | `using_hand_tool` | a video of a person using a hand tool |
| 21 | `lifting` | a video of a person lifting something |
| 22 | `carrying` | a video of a person carrying something |
| 23 | `opening` | a video of a person opening something |
| 24 | `closing` | a video of a person closing something |
| 25 | `entering` | a video of a person entering an area |
| 26 | `exiting` | a video of a person exiting an area |
| 27 | `pointing` | a video of a person pointing |
| 28 | `gesturing` | a video of a person gesturing |
| 29 | `dancing` | a video of a person dancing |
| 30 | `exercising` | a video of a person exercising |
| 31 | `throwing` | a video of a person throwing something |
| 32 | `catching` | a video of a person catching something |
| 33 | `cutting` | a video of a person cutting something |
| 34 | `pouring` | a video of a person pouring something |
| 35 | `cleaning` | a video of a person cleaning |
| 36 | `loading_unloading` | a video of a person loading or unloading something |
| 37 | `operating_machinery` | a video of a person operating machinery |
| 38 | `demonstrating_product` | a video of a person demonstrating a product |
| 39 | `inspecting` | a video of a person inspecting something |
| 40 | `working_at_bench` | a video of a person working at a bench |
| 41 | `welding` | a video of a person welding |
| 42 | `drilling` | a video of a person drilling |
| 43 | `hammering` | a video of a person hammering |
| 44 | `fastening` | a video of a person fastening a screw or bolt |
| 45 | `handling_vehicle_part` | a video of a person handling a vehicle part |
| 46 | `handling_electronics` | a video of a person handling an electronic device |

## Model boundary

The built-in v1 provider is pinned to `microsoft/xclip-base-patch32` at the repository revision encoded in source and loads safetensors only. The model/runtime are execution dependencies; they do not receive project-edit authority. X-CLIP output enters VibeCut only through the `actions` evidence contract and validated persistent media-evidence sink.

Model licensing and runtime behavior must be rechecked when the pinned model revision or dependency versions change. A different action model or vocabulary should register as a distinct provider/taxonomy rather than silently replacing `VibeCutActionSet-v1` semantics.
