#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Bounded VibeCut OCR helper.

The caller supplies authoritative source-frame bounds. This helper samples only
those decoded frame numbers, runs Tesseract TSV OCR, and emits JSON to stdout.
It never writes VibeCut evidence directly.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import os
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_CURRENT: Optional[subprocess.Popen] = None


def _terminate_child(_signum, _frame) -> None:
    global _CURRENT
    if _CURRENT is not None and _CURRENT.poll() is None:
        try:
            _CURRENT.terminate()
        except OSError:
            pass
    raise SystemExit(143)


def _run(command: List[str], *, text: bool = True) -> subprocess.CompletedProcess:
    global _CURRENT
    _CURRENT = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=text,
    )
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
        raise RuntimeError("Sampled PNG has invalid dimensions")
    return width, height


def _engine_version(tesseract: str) -> str:
    result = _run([tesseract, "--version"])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "Could not query Tesseract version")
    first = (result.stdout.splitlines() or [""])[0].strip()
    if not first:
        raise RuntimeError("Tesseract version output was empty")
    return first[:128]


def _extract_samples(args: argparse.Namespace, directory: Path) -> List[Tuple[int, Path]]:
    expected_frames = list(range(args.start_frame, args.end_frame, args.sample_interval_frames))[: args.max_samples]
    if not expected_frames:
        return []

    # Decode in one pass and select exact decoded source frame indices. The
    # returned file order therefore maps deterministically to expected_frames.
    escaped = (
        f"between(n\\,{args.start_frame}\\,{args.end_frame - 1})"
        f"*not(mod(n-{args.start_frame}\\,{args.sample_interval_frames}))"
    )
    pattern = str(directory / "frame_%06d.png")
    command = [
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
    ]
    result = _run(command)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"FFmpeg frame sampling failed with exit {result.returncode}")

    paths = sorted(directory.glob("frame_*.png"))
    if len(paths) != len(expected_frames):
        raise RuntimeError(
            f"FFmpeg produced {len(paths)} sampled frame(s), expected {len(expected_frames)} within authoritative bounds"
        )
    return list(zip(expected_frames, paths))


def _ocr_lines(path: Path, args: argparse.Namespace) -> List[Dict[str, object]]:
    command = [
        args.tesseract,
        str(path),
        "stdout",
        "-l",
        args.language,
        "--psm",
        str(args.psm),
        "tsv",
    ]
    result = _run(command)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"Tesseract failed with exit {result.returncode}")

    groups: Dict[Tuple[int, int, int], List[Dict[str, object]]] = {}
    reader = csv.DictReader(io.StringIO(result.stdout), delimiter="\t")
    for row in reader:
        try:
            if int(row.get("level", "0")) != 5:
                continue
            text = (row.get("text") or "").strip()
            confidence = float(row.get("conf", "-1"))
            if not text or confidence < args.min_confidence * 100.0:
                continue
            x = int(row.get("left", "-1"))
            y = int(row.get("top", "-1"))
            width = int(row.get("width", "-1"))
            height = int(row.get("height", "-1"))
            if x < 0 or y < 0 or width <= 0 or height <= 0:
                continue
            key = (int(row.get("block_num", "0")), int(row.get("par_num", "0")), int(row.get("line_num", "0")))
            groups.setdefault(key, []).append(
                {
                    "word_num": int(row.get("word_num", "0")),
                    "text": text,
                    "confidence": confidence / 100.0,
                    "x": x,
                    "y": y,
                    "width": width,
                    "height": height,
                }
            )
        except (TypeError, ValueError):
            continue

    lines: List[Dict[str, object]] = []
    for key in sorted(groups):
        words = sorted(groups[key], key=lambda item: int(item["word_num"]))
        text = " ".join(str(item["text"]) for item in words).strip()
        if not text:
            continue
        left = min(int(item["x"]) for item in words)
        top = min(int(item["y"]) for item in words)
        right = max(int(item["x"]) + int(item["width"]) for item in words)
        bottom = max(int(item["y"]) + int(item["height"]) for item in words)
        weights = [max(1, len(str(item["text"]))) for item in words]
        weight_sum = sum(weights)
        confidence = sum(float(item["confidence"]) * weight for item, weight in zip(words, weights)) / weight_sum
        lines.append(
            {
                "text": text[:4096],
                "confidence": max(0.0, min(1.0, confidence)),
                "bbox_pixels": {"x": left, "y": top, "width": right - left, "height": bottom - top},
            }
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--tesseract", required=True)
    parser.add_argument("--fps", required=True, type=float)
    parser.add_argument("--start-frame", required=True, type=int)
    parser.add_argument("--end-frame", required=True, type=int)
    parser.add_argument("--sample-interval-frames", type=int, default=30)
    parser.add_argument("--max-samples", type=int, default=300)
    parser.add_argument("--language", default="eng")
    parser.add_argument("--psm", type=int, default=11)
    parser.add_argument("--min-confidence", type=float, default=0.50)
    args = parser.parse_args()

    if args.fps <= 0.0:
        raise SystemExit("fps must be positive")
    if args.start_frame < 0 or args.end_frame <= args.start_frame:
        raise SystemExit("frame bounds must satisfy 0 <= start_frame < end_frame")
    if not (1 <= args.sample_interval_frames <= 1_000_000):
        raise SystemExit("sample_interval_frames must be 1..1000000")
    if not (1 <= args.max_samples <= 2000):
        raise SystemExit("max_samples must be 1..2000")
    if not (0.0 <= args.min_confidence <= 1.0):
        raise SystemExit("min_confidence must be between 0 and 1")
    if not (3 <= args.psm <= 13):
        raise SystemExit("psm must be 3..13")
    if not args.language or len(args.language) > 128 or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+-." for ch in args.language):
        raise SystemExit("language contains unsupported characters")

    signal.signal(signal.SIGTERM, _terminate_child)
    signal.signal(signal.SIGINT, _terminate_child)

    engine_version = _engine_version(args.tesseract)
    with tempfile.TemporaryDirectory(prefix="vibecut-ocr-") as temporary:
        samples = _extract_samples(args, Path(temporary))
        output_samples = []
        for frame, path in samples:
            width, height = _png_size(path)
            lines = _ocr_lines(path, args)
            output_samples.append(
                {
                    "frame": frame,
                    "image_width": width,
                    "image_height": height,
                    "lines": lines,
                }
            )

    result = {
        "schema_version": 1,
        "engine": "tesseract",
        "engine_version": engine_version,
        "language": args.language,
        "psm": args.psm,
        "sample_interval_frames": args.sample_interval_frames,
        "sample_count": len(output_samples),
        "samples": output_samples,
    }
    json.dump(result, sys.stdout, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:  # keep stderr human-readable; stdout remains JSON-only on success
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
