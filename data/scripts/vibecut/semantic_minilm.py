#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut MiniLM semantic-embedding helper.

Request JSON is read from stdin. The helper returns normalized vectors only and
never writes VibeCut state. Model/revision/runtime identity are pinned so cosine
scores are meaningful only inside the declared embedding space.
"""

from __future__ import annotations

import json
import math
import os
import signal
import sys
from typing import Dict, List

import sentence_transformers
import torch
import transformers
from sentence_transformers import SentenceTransformer

MODEL_ID = "sentence-transformers/all-MiniLM-L6-v2"
MODEL_REVISION = "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
DIMENSION = 384
MAX_ITEMS = 5000
MAX_TEXT_CHARS = 8192
MAX_TOTAL_CHARS = 8_000_000
MAX_INPUT_BYTES = 16 * 1024 * 1024


def _die(_signum, _frame) -> None:
    raise SystemExit(143)


def _read_request() -> Dict[str, object]:
    raw = sys.stdin.buffer.read(MAX_INPUT_BYTES + 1)
    if len(raw) > MAX_INPUT_BYTES:
        raise RuntimeError(f"semantic request exceeded the {MAX_INPUT_BYTES} byte safety limit")
    try:
        value = json.loads(raw.decode("utf-8"))
    except Exception as exc:
        raise RuntimeError(f"semantic request JSON is invalid: {exc}") from exc
    if not isinstance(value, dict):
        raise RuntimeError("semantic request root must be an object")
    return value


def _device(requested: str) -> str:
    if requested == "cpu":
        return "cpu"
    if requested == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")
        return "cuda"
    if requested != "auto":
        raise RuntimeError("device must be auto, cpu, or cuda")
    return "cuda" if torch.cuda.is_available() else "cpu"


def main() -> int:
    signal.signal(signal.SIGTERM, _die)
    signal.signal(signal.SIGINT, _die)
    os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")
    os.environ.setdefault("DO_NOT_TRACK", "1")

    request = _read_request()
    if request.get("schema_version") != 1:
        raise RuntimeError("unsupported semantic request schema")
    operation = str(request.get("operation", ""))
    if operation not in ("documents", "query"):
        raise RuntimeError("semantic operation must be documents or query")
    if request.get("model", MODEL_ID) != MODEL_ID or request.get("model_revision", MODEL_REVISION) != MODEL_REVISION:
        raise RuntimeError(f"built-in semantic helper is pinned to {MODEL_ID}@{MODEL_REVISION}")
    device = _device(str(request.get("device", "auto")))
    batch_size = int(request.get("batch_size", 32))
    if not (1 <= batch_size <= 256):
        raise RuntimeError("batch_size must be 1..256")

    raw_items = request.get("items")
    if not isinstance(raw_items, list) or not raw_items or len(raw_items) > MAX_ITEMS:
        raise RuntimeError(f"semantic request requires 1..{MAX_ITEMS} items")
    ids: List[str] = []
    texts: List[str] = []
    seen = set()
    total_chars = 0
    for raw in raw_items:
        if not isinstance(raw, dict):
            raise RuntimeError("semantic items must be objects")
        item_id = str(raw.get("id", "")).strip()
        text = str(raw.get("text", "")).strip()
        if not item_id or len(item_id) > 1024 or item_id in seen:
            raise RuntimeError("semantic item ids must be unique non-empty strings up to 1024 characters")
        if not text or len(text) > MAX_TEXT_CHARS:
            raise RuntimeError(f"semantic item text must contain 1..{MAX_TEXT_CHARS} characters")
        total_chars += len(text)
        if total_chars > MAX_TOTAL_CHARS:
            raise RuntimeError(f"semantic request exceeds the {MAX_TOTAL_CHARS} total-character limit")
        seen.add(item_id)
        ids.append(item_id)
        texts.append(text)

    model = SentenceTransformer(
        MODEL_ID,
        revision=MODEL_REVISION,
        device=device,
        trust_remote_code=False,
        model_kwargs={"use_safetensors": True},
    )
    vectors = model.encode(
        texts,
        batch_size=batch_size,
        show_progress_bar=False,
        convert_to_numpy=True,
        normalize_embeddings=True,
    )
    if len(vectors) != len(ids):
        raise RuntimeError("semantic model returned the wrong embedding count")

    output_items = []
    for item_id, vector in zip(ids, vectors):
        values = [float(value) for value in vector.tolist()]
        if len(values) != DIMENSION or any(not math.isfinite(value) for value in values):
            raise RuntimeError("semantic model returned an invalid embedding vector")
        norm = math.sqrt(sum(value * value for value in values))
        if not math.isfinite(norm) or abs(norm - 1.0) > 0.001:
            raise RuntimeError("semantic model output was not unit-normalized")
        output_items.append({"id": item_id, "vector": values})

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
        "sentence_transformers_version": sentence_transformers.__version__,
        "transformers_version": transformers.__version__,
        "torch_version": torch.__version__,
        "device": device,
        "item_count": len(output_items),
        "items": output_items,
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
