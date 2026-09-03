#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut SigLIP cross-modal embedding helper.

Reads one JSON request from stdin and writes normalized 768-D vectors to stdout.
Image requests refer only to exact source-frame indices supplied by the native
VibeCut layer. Query requests encode text into the same SigLIP space. This
helper never writes project state.
"""

from __future__ import annotations

import json
import math
import os
import signal
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

from PIL import Image
import torch
import torch.nn.functional as F
import transformers
from transformers import AutoProcessor, SiglipModel

MODEL_ID = "google/siglip-base-patch16-224"
MODEL_REVISION = "7fd15f0689c79d79e38b1c2e2e2370a7bf2761ed"
DIMENSION = 768
MAX_ITEMS = 500
MAX_INPUT_BYTES = 8 * 1024 * 1024
MAX_OUTPUT_BYTES_HINT = 32 * 1024 * 1024
_CURRENT: Optional[subprocess.Popen] = None


def _terminate(_signum, _frame) -> None:
    global _CURRENT
    if _CURRENT is not None and _CURRENT.poll() is None:
        try:
            _CURRENT.terminate()
        except OSError:
            pass
    raise SystemExit(143)


def _read_request() -> Dict[str, object]:
    raw = sys.stdin.buffer.read(MAX_INPUT_BYTES + 1)
    if len(raw) > MAX_INPUT_BYTES:
        raise RuntimeError(f"SigLIP request exceeded the {MAX_INPUT_BYTES} byte safety limit")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("SigLIP request root must be an object")
    return value


def _device(requested: str) -> torch.device:
    if requested == "cpu":
        return torch.device("cpu")
    if requested == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
        return torch.device("cuda")
    if requested != "auto":
        raise RuntimeError("device must be auto, cpu, or cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def _extract_exact_frames(source: str, ffmpeg: str, frames: List[int], directory: Path) -> Dict[int, Path]:
    global _CURRENT
    if not frames or len(frames) > MAX_ITEMS or frames != sorted(set(frames)) or frames[0] < 0:
        raise RuntimeError("SigLIP image request requires 1..500 unique sorted non-negative source frames")
    expression = "+".join(f"eq(n\\,{frame})" for frame in frames)
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
    if len(paths) != len(frames):
        raise RuntimeError(f"FFmpeg produced {len(paths)} frame(s), expected {len(frames)}")
    return dict(zip(frames, paths))


def _tensor_from_feature_output(output):
    if isinstance(output, torch.Tensor):
        return output
    pooled = getattr(output, "pooler_output", None)
    if isinstance(pooled, torch.Tensor):
        return pooled
    if isinstance(output, (tuple, list)) and output and isinstance(output[0], torch.Tensor):
        return output[0]
    raise RuntimeError("SigLIP feature API returned an unsupported output shape")


def _vectors_to_json(ids: List[str], features: torch.Tensor) -> List[Dict[str, object]]:
    if features.ndim != 2 or features.shape[0] != len(ids) or features.shape[1] != DIMENSION:
        raise RuntimeError(f"SigLIP returned shape {tuple(features.shape)}, expected ({len(ids)}, {DIMENSION})")
    features = F.normalize(features.float(), p=2, dim=-1)
    result = []
    for item_id, row in zip(ids, features.detach().cpu().tolist()):
        values = [float(value) for value in row]
        if len(values) != DIMENSION or any(not math.isfinite(value) for value in values):
            raise RuntimeError("SigLIP returned an invalid vector")
        norm = math.sqrt(sum(value * value for value in values))
        if not math.isfinite(norm) or abs(norm - 1.0) > 0.001:
            raise RuntimeError("SigLIP vector normalization failed")
        result.append({"id": item_id, "vector": values})
    return result


def main() -> int:
    signal.signal(signal.SIGTERM, _terminate)
    signal.signal(signal.SIGINT, _terminate)
    os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
    os.environ.setdefault("DO_NOT_TRACK", "1")

    request = _read_request()
    if request.get("schema_version") != 1:
        raise RuntimeError("unsupported SigLIP request schema")
    operation = str(request.get("operation", ""))
    if operation not in ("images", "query"):
        raise RuntimeError("SigLIP operation must be images or query")
    if request.get("model", MODEL_ID) != MODEL_ID or request.get("model_revision", MODEL_REVISION) != MODEL_REVISION:
        raise RuntimeError(f"built-in SigLIP helper is pinned to {MODEL_ID}@{MODEL_REVISION}")
    device = _device(str(request.get("device", "auto")))
    batch_size = int(request.get("batch_size", 16))
    if not (1 <= batch_size <= 128):
        raise RuntimeError("batch_size must be 1..128")

    processor = AutoProcessor.from_pretrained(MODEL_ID, revision=MODEL_REVISION, trust_remote_code=False)
    model = SiglipModel.from_pretrained(MODEL_ID, revision=MODEL_REVISION, use_safetensors=True, trust_remote_code=False)
    model.to(device)
    model.eval()

    output_items: List[Dict[str, object]] = []
    if operation == "query":
        items = request.get("items")
        if not isinstance(items, list) or len(items) != 1 or not isinstance(items[0], dict):
            raise RuntimeError("SigLIP query requires exactly one item")
        item_id = str(items[0].get("id", "")).strip()
        text = str(items[0].get("text", "")).strip()
        if not item_id or len(item_id) > 1024 or not text or len(text) > 2048:
            raise RuntimeError("SigLIP query id/text is missing or exceeds bounds")
        inputs = processor(text=[text], padding="max_length", return_tensors="pt")
        inputs = {key: value.to(device) for key, value in inputs.items() if key in ("input_ids", "attention_mask")}
        with torch.inference_mode():
            features = _tensor_from_feature_output(model.get_text_features(**inputs))
        output_items = _vectors_to_json([item_id], features)
    else:
        source = str(request.get("source", "")).strip()
        ffmpeg = str(request.get("ffmpeg", "")).strip()
        items = request.get("items")
        if not source or not ffmpeg or not isinstance(items, list) or not items or len(items) > MAX_ITEMS:
            raise RuntimeError("SigLIP image request requires source, ffmpeg and 1..500 items")
        ids: List[str] = []
        frames: List[int] = []
        seen_ids = set()
        seen_frames = set()
        for item in items:
            if not isinstance(item, dict):
                raise RuntimeError("SigLIP image items must be objects")
            item_id = str(item.get("id", "")).strip()
            frame = int(item.get("frame", -1))
            if not item_id or len(item_id) > 1024 or item_id in seen_ids or frame < 0 or frame in seen_frames:
                raise RuntimeError("SigLIP image ids and source frames must be unique and valid")
            seen_ids.add(item_id)
            seen_frames.add(frame)
            ids.append(item_id)
            frames.append(frame)
        order = sorted(range(len(frames)), key=lambda index: frames[index])
        sorted_frames = [frames[index] for index in order]
        sorted_ids = [ids[index] for index in order]
        with tempfile.TemporaryDirectory(prefix="vibecut-siglip-") as temporary:
            mapping = _extract_exact_frames(source, ffmpeg, sorted_frames, Path(temporary))
            for offset in range(0, len(sorted_frames), batch_size):
                batch_frames = sorted_frames[offset : offset + batch_size]
                batch_ids = sorted_ids[offset : offset + batch_size]
                images = []
                for frame in batch_frames:
                    with Image.open(mapping[frame]) as image:
                        images.append(image.convert("RGB").copy())
                inputs = processor(images=images, return_tensors="pt")
                pixel_values = inputs["pixel_values"].to(device)
                with torch.inference_mode():
                    features = _tensor_from_feature_output(model.get_image_features(pixel_values=pixel_values))
                output_items.extend(_vectors_to_json(batch_ids, features))

    result = {
        "schema_version": 1,
        "operation": operation,
        "authority": "model_representation",
        "score_semantics": "cosine_similarity_same_embedding_space",
        "model": MODEL_ID,
        "model_revision": MODEL_REVISION,
        "model_license": "Apache-2.0",
        "dimension": DIMENSION,
        "unit_normalized": True,
        "transformers_version": transformers.__version__,
        "torch_version": torch.__version__,
        "device": str(device),
        "item_count": len(output_items),
        "items": output_items,
    }
    encoded = json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n"
    if len(encoded.encode("utf-8")) > MAX_OUTPUT_BYTES_HINT:
        raise RuntimeError("SigLIP output exceeded the helper safety limit")
    sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
