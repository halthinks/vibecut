#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
"""Run pyannote speaker diarization and emit anonymous JSON segments.

Human identity is intentionally out of scope. The C++ evidence contract rejects
identity fields even if this script or a future provider were modified to emit
them.
"""

import argparse
import json
import os
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--start-seconds", type=float, default=0.0)
    parser.add_argument("--end-seconds", type=float, default=-1.0)
    parser.add_argument("--model", default="pyannote/speaker-diarization-community-1")
    parser.add_argument("--local-model-path", default="")
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--exclusive", action="store_true")
    parser.add_argument("--min-speakers", type=int, default=0)
    parser.add_argument("--max-speakers", type=int, default=0)
    return parser.parse_args()


def main():
    args = parse_args()
    if args.start_seconds < 0:
        raise ValueError("start-seconds must be >= 0")
    if args.end_seconds >= 0 and args.end_seconds <= args.start_seconds:
        raise ValueError("end-seconds must be greater than start-seconds")
    if args.min_speakers < 0 or args.max_speakers < 0:
        raise ValueError("speaker bounds must be >= 0")
    if args.min_speakers and args.max_speakers and args.min_speakers > args.max_speakers:
        raise ValueError("min-speakers may not exceed max-speakers")

    import torch
    from pyannote.audio import Audio, Pipeline
    from pyannote.core import Segment

    token = os.environ.get("HF_TOKEN") or os.environ.get("HUGGINGFACE_TOKEN")
    model_ref = args.local_model_path or args.model
    if args.local_model_path:
        pipeline = Pipeline.from_pretrained(model_ref)
    else:
        if not token:
            raise RuntimeError("HF_TOKEN is required for first-time/community-1 model acquisition")
        pipeline = Pipeline.from_pretrained(model_ref, token=token)

    use_cuda = args.device == "cuda" or (args.device == "auto" and torch.cuda.is_available())
    if args.device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but torch reports no CUDA device")
    if use_cuda:
        pipeline.to(torch.device("cuda"))

    offset = 0.0
    source_input = args.source
    if args.start_seconds > 0 or args.end_seconds >= 0:
        if args.end_seconds < 0:
            raise ValueError("end-seconds is required when cropping an excerpt")
        waveform, sample_rate = Audio().crop(
            args.source,
            Segment(args.start_seconds, args.end_seconds),
        )
        source_input = {"waveform": waveform, "sample_rate": sample_rate}
        offset = args.start_seconds

    kwargs = {}
    if args.min_speakers:
        kwargs["min_speakers"] = args.min_speakers
    if args.max_speakers:
        kwargs["max_speakers"] = args.max_speakers

    output = pipeline(source_input, **kwargs)
    annotation = output.speaker_diarization
    exclusive_used = False
    if args.exclusive and getattr(output, "exclusive_speaker_diarization", None) is not None:
        annotation = output.exclusive_speaker_diarization
        exclusive_used = True

    segments = []
    for turn, _, speaker in annotation.itertracks(yield_label=True):
        segments.append(
            {
                "start_seconds": float(turn.start + offset),
                "end_seconds": float(turn.end + offset),
                "speaker_cluster_id": str(speaker),
                "exclusive": exclusive_used,
                "overlap": False,
            }
        )

    if not exclusive_used:
        for i, segment in enumerate(segments):
            for j, other in enumerate(segments):
                if i == j or segment["speaker_cluster_id"] == other["speaker_cluster_id"]:
                    continue
                if segment["start_seconds"] < other["end_seconds"] and other["start_seconds"] < segment["end_seconds"]:
                    segment["overlap"] = True
                    break

    json.dump(
        {
            "schema_version": 1,
            "model": model_ref,
            "device": "cuda" if use_cuda else "cpu",
            "exclusive": exclusive_used,
            "segments": segments,
        },
        sys.stdout,
        separators=(",", ":"),
    )
    sys.stdout.write("\n")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # keep stdout machine-readable on success only
        print(f"pyannote diarization failed: {exc}", file=sys.stderr)
        sys.exit(1)
