#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut DETR object-detection helper.

The caller supplies authoritative source-frame bounds. The helper samples only
those decoded frame indices, performs COCO object detection with a pinned DETR
revision, and emits JSON. It never writes VibeCut evidence directly.
"""

from __future__ import annotations

import argparse
import json
import math
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple

import torch
import torchvision
import transformers
from PIL import Image
from transformers import AutoImageProcessor, AutoModelForObjectDetection

MODEL_ID = "facebook/detr-resnet-50"
MODEL_REVISION = "ebd66332d81f2ee6d9fbfefd0235026b46a381d0"
TAXONOMY = "COCO-2017"
MAX_SAMPLES = 1000

_CURRENT: Optional[subprocess.Popen] = None


def _terminate_child(_signum, _frame) -> None:
    global _CURRENT
    if _CURRENT is not None and _CURRENT.poll() is None:
        try:
            _CURRENT.terminate()
        except OSError:
            pass
    raise SystemExit(143)


def _run(command: List[str]) -> subprocess.CompletedProcess:
    global _CURRENT
    _CURRENT = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = _CURRENT.communicate()
    result = subprocess.CompletedProcess(command, _CURRENT.returncode, stdout, stderr)
    _CURRENT = None
    return result


def _png_size(path: Path) -> Tuple[int, int]:
    with path.open("rb") as handle:
        header = handle.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"FFmpeg did not produce a valid PNG frame: {path}")
    width, height = struct.unpack(">II", header[16:24])
    if width <= 0 or height <= 0:
        raise RuntimeError("sampled PNG has invalid dimensions")
    return width, height


def _extract_samples(args: argparse.Namespace, directory: Path) -> List[Tuple[int, Path]]:
    expected_frames = list(range(args.start_frame, args.end_frame, args.sample_interval_frames))[: args.max_samples]
    if not expected_frames:
        return []
    escaped = (
        f"between(n\\,{args.start_frame}\\,{args.end_frame - 1})"
        f"*not(mod(n-{args.start_frame}\\,{args.sample_interval_frames}))"
    )
    pattern = str(directory / "frame_%06d.png")
    result = _run([
        args.ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        args.source,
        "-vf",
        f"select='{escaped}'",
        "-vsync",
        "0",
        "-frames:v",
        str(len(expected_frames)),
        pattern,
    ])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace").strip() or f"FFmpeg frame sampling failed with exit {result.returncode}")
    paths = sorted(directory.glob("frame_*.png"))
    if len(paths) != len(expected_frames):
        raise RuntimeError(f"FFmpeg produced {len(paths)} sampled frame(s), expected {len(expected_frames)}")
    return list(zip(expected_frames, paths))


def _device(requested: str) -> torch.device:
    if requested == "cpu":
        return torch.device("cpu")
    if requested == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def _box_pixels(box: List[float], width: int, height: int) -> Optional[dict]:
    x0, y0, x1, y1 = box
    left = max(0, min(width, int(math.floor(x0))))
    top = max(0, min(height, int(math.floor(y0))))
    right = max(0, min(width, int(math.ceil(x1))))
    bottom = max(0, min(height, int(math.ceil(y1))))
    if right <= left or bottom <= top:
        return None
    return {"x": left, "y": top, "width": right - left, "height": bottom - top}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--start-frame", required=True, type=int)
    parser.add_argument("--end-frame", required=True, type=int)
    parser.add_argument("--sample-interval-frames", type=int, default=30)
    parser.add_argument("--max-samples", type=int, default=300)
    parser.add_argument("--min-score", type=float, default=0.70)
    parser.add_argument("--max-detections-per-frame", type=int, default=50)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--model", default=MODEL_ID)
    parser.add_argument("--revision", default=MODEL_REVISION)
    args = parser.parse_args()

    if args.model != MODEL_ID or args.revision != MODEL_REVISION:
        raise SystemExit(f"built-in DETR helper is pinned to {MODEL_ID}@{MODEL_REVISION}")
    if args.start_frame < 0 or args.end_frame <= args.start_frame:
        raise SystemExit("frame bounds must satisfy 0 <= start_frame < end_frame")
    if not (1 <= args.sample_interval_frames <= 1_000_000):
        raise SystemExit("sample_interval_frames must be 1..1000000")
    if not (1 <= args.max_samples <= MAX_SAMPLES):
        raise SystemExit(f"max_samples must be 1..{MAX_SAMPLES}")
    if not math.isfinite(args.min_score) or not (0.0 <= args.min_score <= 1.0):
        raise SystemExit("min_score must be finite and between 0 and 1")
    if not (1 <= args.max_detections_per_frame <= 100):
        raise SystemExit("max_detections_per_frame must be 1..100")

    required = 1 + (args.end_frame - 1 - args.start_frame) // args.sample_interval_frames
    if required > args.max_samples:
        raise SystemExit(
            f"requested frame cadence requires {required} samples, exceeding max_samples={args.max_samples}; increase sample_interval_frames or use a smaller range"
        )

    signal.signal(signal.SIGTERM, _terminate_child)
    signal.signal(signal.SIGINT, _terminate_child)
    device = _device(args.device)
    processor = AutoImageProcessor.from_pretrained(MODEL_ID, revision=MODEL_REVISION)
    model = AutoModelForObjectDetection.from_pretrained(
        MODEL_ID,
        revision=MODEL_REVISION,
        use_safetensors=True,
    )
    model.to(device)
    model.eval()

    samples = []
    with tempfile.TemporaryDirectory(prefix="vibecut-objects-") as temporary:
        for frame, path in _extract_samples(args, Path(temporary)):
            width, height = _png_size(path)
            with Image.open(path) as opened:
                image = opened.convert("RGB")
                inputs = processor(images=image, return_tensors="pt")
            inputs = {name: tensor.to(device) for name, tensor in inputs.items()}
            with torch.inference_mode():
                outputs = model(**inputs)
            target_sizes = torch.tensor([[height, width]], device=device)
            processed = processor.post_process_object_detection(outputs, threshold=args.min_score, target_sizes=target_sizes)[0]
            detections = []
            for score_tensor, label_tensor, box_tensor in zip(processed["scores"], processed["labels"], processed["boxes"]):
                score = float(score_tensor.detach().cpu().item())
                label_id = int(label_tensor.detach().cpu().item())
                if not math.isfinite(score) or score < args.min_score or score > 1.0 or label_id < 0:
                    continue
                box = _box_pixels([float(v) for v in box_tensor.detach().cpu().tolist()], width, height)
                if box is None:
                    continue
                label = str(model.config.id2label.get(label_id, str(label_id)))[:256]
                detections.append({"label": label, "label_id": label_id, "score": score, "bbox_pixels": box})
            detections.sort(key=lambda item: float(item["score"]), reverse=True)
            detections = detections[: args.max_detections_per_frame]
            samples.append({"frame": frame, "image_width": width, "image_height": height, "detections": detections})

    result = {
        "schema_version": 1,
        "authority": "model_prediction",
        "taxonomy": TAXONOMY,
        "model": MODEL_ID,
        "model_revision": MODEL_REVISION,
        "transformers_version": transformers.__version__,
        "torch_version": torch.__version__,
        "torchvision_version": torchvision.__version__,
        "device": str(device),
        "sample_interval_frames": args.sample_interval_frames,
        "sample_count": len(samples),
        "min_score": args.min_score,
        "samples": samples,
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
