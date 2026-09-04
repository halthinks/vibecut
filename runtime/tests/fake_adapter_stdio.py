#!/usr/bin/env python3
# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import sys

from fake_adapter import FakeAdapter
from halthinks_runtime.policy import TrustMode
from halthinks_runtime.protocol import ProtocolError, decode_line


def send(message) -> None:
    sys.stdout.buffer.write(message.encode_line())
    sys.stdout.buffer.flush()


def main() -> int:
    adapter = FakeAdapter(revision=7, value=1)
    send(adapter.hello(TrustMode.OFF))
    while True:
        raw = sys.stdin.buffer.readline()
        if raw == b"":
            return 0
        try:
            request = decode_line(raw)
            response = adapter.exchange(request)
            send(response)
            if request.type == "propose_plan" and response.type != "error":
                send(adapter.authorize(TrustMode.OFF, human_approved=True))
            if request.type == "invoke" and response.type != "error" and response.payload.get("started") is True:
                while True:
                    event = adapter.next_event()
                    if event is None:
                        break
                    send(event)
                    job = event.payload.get("job", {})
                    if event.type == "job_update" and job.get("state") in {"succeeded", "failed", "cancelled"}:
                        break
        except ProtocolError as exc:
            print(f"protocol error: {exc}", file=sys.stderr, flush=True)
            return 2
        except Exception as exc:
            print(f"fake adapter failure: {exc}", file=sys.stderr, flush=True)
            return 3


if __name__ == "__main__":
    raise SystemExit(main())
