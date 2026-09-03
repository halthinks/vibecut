#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut AudioSet event-classification helper.

The caller provides an authoritative file path and frame-bounded excerpt. This
helper decodes only that excerpt through FFmpeg, evaluates fixed windows with a
pinned Audio Spectrogram Transformer, and emits ranked model predictions as
JSON. It never writes VibeCut evidence directly.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import signal
import subprocess
import sys
from typing import List, Optional

import numpy as np
import torch
import transformers
from transformers import AutoFeatureExtractor, AutoModelForAudioClassification

MODEL_ID = "MIT/ast-finetuned-audioset-10-10-0.4593"
SAMPLE_RATE = 16000
MAX_DECODE_SECONDS = 1800.0
MAX_PCM_BYTES = 256 * 1024 * 1024

_CURRENT: Optional[subprocess.Popen] = None


def _terminate_child(_signum, _frame) -> None:
    global _CURRENT
    if _CURRENT is not None and _CURRENT.poll() is None:
        try:
            _CURRENT.terminate()
        except OSError:
            pass
    raise SystemExit(143)


def _decode_audio(args: argparse.Namespace, duration_seconds: float) -> np.ndarray:
    global _CURRENT
    command = [
        args.ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        f"{args.start_frame / args.fps:.9f}",
        "-t",
        f"{duration_seconds:.9f}",
        "-i",
        args.source,
        "-vn",
        "-ac",
        "1",
        "-ar",
        str(SAMPLE_RATE),
        "-f",
        "f32le",
        "pipe:1",
    ]
    _CURRENT = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = _CURRENT.communicate()
    returncode = _CURRENT.returncode
    _CURRENT = None
    if returncode != 0:
        raise RuntimeError(stderr.decode("utf-8", errors="replace").strip() or f"FFmpeg audio decode failed with exit {returncode}")
    if len(stdout) > MAX_PCM_BYTES:
        raise RuntimeError(f"Decoded PCM exceeded the {MAX_PCM_BYTES} byte safety limit")
    if len(stdout) % 4 != 0:
        raise RuntimeError("FFmpeg returned misaligned f32le PCM")
    return np.frombuffer(stdout, dtype="<f4").copy()


def _device(requested: str) -> torch.device:
    if requested == "cpu":
        return torch.device("cpu")
    if requested == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--fps", required=True, type=float)
    parser.add_argument("--start-frame", required=True, type=int)
    parser.add_argument("--end-frame", required=True, type=int)
    parser.add_argument("--model", default=MODEL_ID)
    parser.add_argument("--window-seconds", type=float, default=10.0)
    parser.add_argument("--hop-seconds", type=float, default=5.0)
    parser.add_argument("--max-windows", type=int, default=120)
    parser.add_argument("--top-k", type=int, default=8)
    parser.add_argument("--min-score", type=float, default=0.05)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    args = parser.parse_args()

    if args.model != MODEL_ID:
        raise SystemExit(f"built-in AST helper is pinned to {MODEL_ID}")
    if not math.isfinite(args.fps) or args.fps <= 0.0:
        raise SystemExit("fps must be positive and finite")
    if args.start_frame < 0 or args.end_frame <= args.start_frame:
        raise SystemExit("frame bounds must satisfy 0 <= start_frame < end_frame")
    if not math.isfinite(args.window_seconds) or not (1.0 <= args.window_seconds <= 10.0):
        raise SystemExit("window_seconds must be finite and in 1..10")
    if not math.isfinite(args.hop_seconds) or not (0.25 <= args.hop_seconds <= 10.0):
        raise SystemExit("hop_seconds must be finite and in 0.25..10")
    if args.hop_seconds > args.window_seconds:
        raise SystemExit("hop_seconds may not exceed window_seconds; VibeCut does not create unobserved gaps between classifier windows")
    if not (1 <= args.max_windows <= 500):
        raise SystemExit("max_windows must be 1..500")
    if not (1 <= args.top_k <= 20):
        raise SystemExit("top_k must be 1..20")
    if not math.isfinite(args.min_score) or not (0.0 <= args.min_score <= 1.0):
        raise SystemExit("min_score must be finite and between 0 and 1")

    duration_seconds = (args.end_frame - args.start_frame) / args.fps
    if not math.isfinite(duration_seconds) or duration_seconds <= 0.0 or duration_seconds > MAX_DECODE_SECONDS:
        raise SystemExit(f"requested excerpt must be finite, >0 and <= {MAX_DECODE_SECONDS:g} seconds")

    window_count = 1 if duration_seconds <= args.window_seconds else 1 + math.ceil((duration_seconds - args.window_seconds) / args.hop_seconds)
    if window_count > args.max_windows:
        raise SystemExit(
            f"requested excerpt/window cadence requires {window_count} windows, exceeding max_windows={args.max_windows}; increase hop_seconds or use a smaller range"
        )

    signal.signal(signal.SIGTERM, _terminate_child)
    signal.signal(signal.SIGINT, _terminate_child)
    os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
    os.environ.setdefault("DO_NOT_TRACK", "1")

    device = _device(args.device)
    extractor = AutoFeatureExtractor.from_pretrained(MODEL_ID)
    model = AutoModelForAudioClassification.from_pretrained(MODEL_ID, use_safetensors=True)
    model.to(device)
    model.eval()

    audio = _decode_audio(args, duration_seconds)
    expected_samples = max(1, int(round(duration_seconds * SAMPLE_RATE)))
    if audio.size == 0:
        raise RuntimeError("FFmpeg decoded no audio samples")
    # Container timestamps and decoder rounding can differ by a handful of
    # samples. Large mismatches indicate the authoritative excerpt was not
    # actually decoded as requested.
    if abs(int(audio.size) - expected_samples) > max(SAMPLE_RATE, int(expected_samples * 0.02)):
        raise RuntimeError(f"decoded sample count {audio.size} differs materially from expected {expected_samples}")

    window_samples = max(1, int(round(args.window_seconds * SAMPLE_RATE)))
    hop_samples = max(1, int(round(args.hop_seconds * SAMPLE_RATE)))
    windows: List[dict] = []
    offset = 0
    index = 0
    while offset < audio.size and index < window_count:
        chunk = audio[offset : min(audio.size, offset + window_samples)]
        if chunk.size == 0:
            break
        actual_seconds = float(chunk.size) / SAMPLE_RATE
        inputs = extractor(chunk, sampling_rate=SAMPLE_RATE, return_tensors="pt")
        input_values = inputs["input_values"].to(device)
        with torch.inference_mode():
            logits = model(input_values=input_values).logits[0]
            scores = torch.softmax(logits.float(), dim=-1)
        count = min(args.top_k, int(scores.numel()))
        values, indices = torch.topk(scores, k=count)
        predictions = []
        for rank, (score_tensor, label_tensor) in enumerate(zip(values.tolist(), indices.tolist()), start=1):
            score = float(score_tensor)
            if not math.isfinite(score) or score < args.min_score:
                continue
            label_id = int(label_tensor)
            label = str(model.config.id2label.get(label_id, str(label_id)))
            predictions.append({"rank": rank, "label_id": label_id, "label": label[:256], "score": max(0.0, min(1.0, score))})

        relative_start = offset / SAMPLE_RATE
        relative_end = min(duration_seconds, relative_start + actual_seconds)
        if not (0.0 <= relative_start < relative_end <= duration_seconds + 1e-6):
            raise RuntimeError("internal window construction escaped the authoritative excerpt")
        windows.append(
            {
                "index": index,
                "start_seconds": relative_start,
                "end_seconds": relative_end,
                "predictions": predictions,
            }
        )
        index += 1
        if duration_seconds <= args.window_seconds:
            break
        offset += hop_samples
        if offset >= audio.size:
            break

    if not windows or len(windows) > window_count:
        raise RuntimeError("bounded audio-event analysis produced an invalid window count")

    result = {
        "schema_version": 1,
        "authority": "model_prediction",
        "taxonomy": "AudioSet",
        "model": MODEL_ID,
        "transformers_version": transformers.__version__,
        "torch_version": torch.__version__,
        "device": str(device),
        "sampling_rate": SAMPLE_RATE,
        "window_seconds": args.window_seconds,
        "hop_seconds": args.hop_seconds,
        "top_k": args.top_k,
        "min_score": args.min_score,
        "window_count": len(windows),
        "windows": windows,
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
