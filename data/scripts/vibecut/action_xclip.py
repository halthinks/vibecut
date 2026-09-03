#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut zero-shot action helper using pinned Microsoft X-CLIP.

The action vocabulary is built in and versioned. Scores are softmax values over
that fixed candidate set, not factual probabilities. The helper emits the exact
8 source frames used for every prediction window and never writes VibeCut
evidence directly.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from PIL import Image
import torch
import transformers
from transformers import XCLIPModel, XCLIPProcessor

MODEL_ID = "microsoft/xclip-base-patch32"
MODEL_REVISION = "47627d79085e55e641829bd120ac64a3cc3c2238"
TAXONOMY = "VibeCutActionSet-v1"
FRAMES_PER_WINDOW = 8
MAX_WINDOWS = 100
MAX_SAMPLED_FRAMES = 800

# (stable id implied by list index, short label, actual zero-shot prompt)
ACTION_SET: Sequence[Tuple[str, str]] = (
    ("no_clear_action", "a video with no clear action from the listed set"),
    ("talking", "a video of a person talking"),
    ("presenting", "a video of a person presenting to an audience"),
    ("talking_to_camera", "a video of a person talking directly to the camera"),
    ("walking", "a video of a person walking"),
    ("running", "a video of a person running"),
    ("sitting", "a video of a person sitting"),
    ("standing", "a video of a person standing"),
    ("driving", "a video of a person driving a vehicle"),
    ("riding_bicycle", "a video of a person riding a bicycle"),
    ("cooking", "a video of a person cooking"),
    ("eating", "a video of a person eating"),
    ("drinking", "a video of a person drinking"),
    ("typing", "a video of a person typing on a keyboard"),
    ("writing", "a video of a person writing"),
    ("reading", "a video of a person reading"),
    ("using_phone", "a video of a person using a phone"),
    ("using_computer", "a video of a person using a computer"),
    ("assembling", "a video of a person assembling something"),
    ("repairing", "a video of a person repairing something"),
    ("using_hand_tool", "a video of a person using a hand tool"),
    ("lifting", "a video of a person lifting something"),
    ("carrying", "a video of a person carrying something"),
    ("opening", "a video of a person opening something"),
    ("closing", "a video of a person closing something"),
    ("entering", "a video of a person entering an area"),
    ("exiting", "a video of a person exiting an area"),
    ("pointing", "a video of a person pointing"),
    ("gesturing", "a video of a person gesturing"),
    ("dancing", "a video of a person dancing"),
    ("exercising", "a video of a person exercising"),
    ("throwing", "a video of a person throwing something"),
    ("catching", "a video of a person catching something"),
    ("cutting", "a video of a person cutting something"),
    ("pouring", "a video of a person pouring something"),
    ("cleaning", "a video of a person cleaning"),
    ("loading_unloading", "a video of a person loading or unloading something"),
    ("operating_machinery", "a video of a person operating machinery"),
    ("demonstrating_product", "a video of a person demonstrating a product"),
    ("inspecting", "a video of a person inspecting something"),
    ("working_at_bench", "a video of a person working at a bench"),
    ("welding", "a video of a person welding"),
    ("drilling", "a video of a person drilling"),
    ("hammering", "a video of a person hammering"),
    ("fastening", "a video of a person fastening a screw or bolt"),
    ("handling_vehicle_part", "a video of a person handling a vehicle part"),
    ("handling_electronics", "a video of a person handling an electronic device"),
)

_CURRENT: Optional[subprocess.Popen] = None


def _action_set_hash() -> str:
    payload = "\n".join(f"{idx}\t{label}\t{prompt}" for idx, (label, prompt) in enumerate(ACTION_SET))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _terminate_child(_signum, _frame) -> None:
    global _CURRENT
    if _CURRENT is not None and _CURRENT.poll() is None:
        try:
            _CURRENT.terminate()
        except OSError:
            pass
    raise SystemExit(143)


def _device(requested: str) -> torch.device:
    if requested == "cpu":
        return torch.device("cpu")
    if requested == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def _sample_frames(start: int, end: int) -> List[int]:
    if end - start < FRAMES_PER_WINDOW:
        raise RuntimeError("action window is too short to provide eight unique source frames")
    values: List[int] = []
    for i in range(FRAMES_PER_WINDOW):
        frame = start + (i * (end - start - 1)) // (FRAMES_PER_WINDOW - 1)
        if values and frame <= values[-1]:
            frame = values[-1] + 1
        if frame >= end:
            raise RuntimeError("could not construct eight unique source-frame samples inside action window")
        values.append(frame)
    return values


def _windows(start: int, end: int, window_frames: int, hop_frames: int, max_windows: int) -> List[Dict[str, object]]:
    result: List[Dict[str, object]] = []
    cursor = start
    while cursor < end:
        window_end = min(end, cursor + window_frames)
        if window_end - cursor < FRAMES_PER_WINDOW:
            break
        observed = _sample_frames(cursor, window_end)
        result.append({"start": cursor, "end": window_end, "observed": observed})
        if len(result) > max_windows:
            raise RuntimeError(f"action request requires more than max_windows={max_windows}")
        cursor += hop_frames
    if not result:
        raise RuntimeError("action request produced no valid eight-frame windows")
    return result


def _extract_frames(source: str, ffmpeg: str, frame_indices: List[int], directory: Path) -> Dict[int, Path]:
    global _CURRENT
    if len(frame_indices) > MAX_SAMPLED_FRAMES:
        raise RuntimeError(f"action request exceeds the {MAX_SAMPLED_FRAMES} sampled-frame safety limit")
    expression = "+".join(f"eq(n\\,{frame})" for frame in frame_indices)
    pattern = str(directory / "frame_%06d.png")
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-i", source,
               "-vf", f"select='{expression}'", "-vsync", "0", pattern]
    _CURRENT = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = _CURRENT.communicate()
    code = _CURRENT.returncode
    _CURRENT = None
    if code != 0:
        raise RuntimeError(stderr.decode("utf-8", errors="replace").strip() or f"FFmpeg frame extraction failed with exit {code}")
    paths = sorted(directory.glob("frame_*.png"))
    if len(paths) != len(frame_indices):
        raise RuntimeError(f"FFmpeg produced {len(paths)} action frame(s), expected {len(frame_indices)}")
    return dict(zip(frame_indices, paths))


def _png_size(path: Path) -> Tuple[int, int]:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"invalid PNG action frame: {path}")
    return struct.unpack(">II", header[16:24])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--fps", required=True, type=float)
    parser.add_argument("--start-frame", required=True, type=int)
    parser.add_argument("--end-frame", required=True, type=int)
    parser.add_argument("--window-seconds", type=float, default=4.0)
    parser.add_argument("--hop-seconds", type=float, default=2.0)
    parser.add_argument("--max-windows", type=int, default=50)
    parser.add_argument("--top-k", type=int, default=5)
    parser.add_argument("--min-score", type=float, default=0.05)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--model", default=MODEL_ID)
    parser.add_argument("--revision", default=MODEL_REVISION)
    args = parser.parse_args()

    if args.model != MODEL_ID or args.revision != MODEL_REVISION:
        raise SystemExit(f"built-in X-CLIP helper is pinned to {MODEL_ID}@{MODEL_REVISION}")
    if args.fps <= 0.0 or not math.isfinite(args.fps):
        raise SystemExit("fps must be positive and finite")
    if args.start_frame < 0 or args.end_frame <= args.start_frame:
        raise SystemExit("frame bounds must satisfy 0 <= start_frame < end_frame")
    if not (0.5 <= args.window_seconds <= 10.0) or not math.isfinite(args.window_seconds):
        raise SystemExit("window_seconds must be finite and between 0.5 and 10")
    if not (0.25 <= args.hop_seconds <= args.window_seconds) or not math.isfinite(args.hop_seconds):
        raise SystemExit("hop_seconds must be finite, >=0.25 and <= window_seconds")
    if not (1 <= args.max_windows <= MAX_WINDOWS):
        raise SystemExit(f"max_windows must be 1..{MAX_WINDOWS}")
    if not (1 <= args.top_k <= 10):
        raise SystemExit("top_k must be 1..10")
    if not (0.0 <= args.min_score <= 1.0) or not math.isfinite(args.min_score):
        raise SystemExit("min_score must be finite and between 0 and 1")

    window_frames = max(FRAMES_PER_WINDOW, int(round(args.window_seconds * args.fps)))
    hop_frames = max(1, int(round(args.hop_seconds * args.fps)))
    windows = _windows(args.start_frame, args.end_frame, window_frames, hop_frames, args.max_windows)
    unique_frames = sorted({frame for window in windows for frame in window["observed"]})

    signal.signal(signal.SIGTERM, _terminate_child)
    signal.signal(signal.SIGINT, _terminate_child)
    os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
    os.environ.setdefault("DO_NOT_TRACK", "1")

    device = _device(args.device)
    processor = XCLIPProcessor.from_pretrained(MODEL_ID, revision=MODEL_REVISION)
    model = XCLIPModel.from_pretrained(MODEL_ID, revision=MODEL_REVISION, use_safetensors=True)
    model.to(device)
    model.eval()
    prompts = [prompt for _, prompt in ACTION_SET]

    output_windows = []
    with tempfile.TemporaryDirectory(prefix="vibecut-xclip-") as temporary:
        mapping = _extract_frames(args.source, args.ffmpeg, unique_frames, Path(temporary))
        for index, window in enumerate(windows):
            observed = [int(frame) for frame in window["observed"]]
            frames = []
            for frame in observed:
                path = mapping[frame]
                width, height = _png_size(path)
                if width <= 0 or height <= 0:
                    raise RuntimeError("sampled action frame has invalid dimensions")
                with Image.open(path) as image:
                    frames.append(image.convert("RGB").copy())
            inputs = processor(text=prompts, videos=frames, return_tensors="pt", padding=True)
            inputs = {name: tensor.to(device) if hasattr(tensor, "to") else tensor for name, tensor in inputs.items()}
            with torch.inference_mode():
                outputs = model(**inputs)
                probabilities = torch.softmax(outputs.logits_per_video[0].float(), dim=-1)
            count = min(args.top_k, int(probabilities.numel()))
            values, indices = torch.topk(probabilities, k=count)
            predictions = []
            for rank, (score_tensor, label_tensor) in enumerate(zip(values.tolist(), indices.tolist()), start=1):
                score = float(score_tensor)
                if score < args.min_score:
                    continue
                label_id = int(label_tensor)
                label, prompt = ACTION_SET[label_id]
                predictions.append({"rank": rank, "label_id": label_id, "label": label,
                                    "prompt": prompt, "score": max(0.0, min(1.0, score))})
            output_windows.append({"index": index, "start_frame": int(window["start"]),
                                   "end_frame": int(window["end"]), "observed_frames": observed,
                                   "predictions": predictions})

    result = {
        "schema_version": 1,
        "authority": "model_prediction",
        "score_semantics": "softmax_over_fixed_action_set",
        "taxonomy": TAXONOMY,
        "action_set_sha256": _action_set_hash(),
        "candidate_count": len(ACTION_SET),
        "model": MODEL_ID,
        "model_revision": MODEL_REVISION,
        "model_license": "MIT",
        "transformers_version": transformers.__version__,
        "torch_version": torch.__version__,
        "device": str(device),
        "frames_per_window": FRAMES_PER_WINDOW,
        "window_seconds": args.window_seconds,
        "hop_seconds": args.hop_seconds,
        "window_frames": window_frames,
        "hop_frames": hop_frames,
        "window_count": len(output_windows),
        "windows": output_windows,
    }
    json.dump(result, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
