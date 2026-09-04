# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import collections
import sys
import threading
from typing import BinaryIO

from .protocol import AdapterClient, Envelope, decode_line


class ChildStdioError(RuntimeError):
    pass


class ChildStdioAdapterClient(AdapterClient):
    """Protocol client for a runtime launched as a child of the GPL editor.

    Production Kdenlive topology:

        GPL Kdenlive adapter -> child stdin   (hello/responses/events)
        proprietary runtime -> child stdout  (requests only)
        proprietary diagnostics -> stderr    (never protocol)

    The GPL editor owns the child-process lifecycle and therefore acts as the
    watchdog. The child protocol path is intentionally synchronous: there is no
    background thread blocked on Python's global stdin, eliminating the shutdown
    race that can otherwise abort a successfully completed runtime process.
    """

    def __init__(
        self,
        reader: BinaryIO | None = None,
        writer: BinaryIO | None = None,
    ) -> None:
        self._reader = reader or sys.stdin.buffer
        self._writer = writer or sys.stdout.buffer
        self._events: collections.deque[Envelope] = collections.deque()
        self._exchange_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._closed = False

    def read_hello(self) -> Envelope:
        while True:
            message = self._read_one()
            if message.kind == "event" and message.type == "hello":
                return message
            if message.kind == "event":
                self._events.append(message)
                continue
            raise ChildStdioError(f"received {message.kind}/{message.type} before adapter hello")

    def exchange(self, message: Envelope) -> Envelope:
        if message.kind != "request":
            raise ChildStdioError("exchange accepts request envelopes only")
        with self._exchange_lock:
            self._write(message)
            while True:
                incoming = self._read_one()
                if incoming.kind == "event":
                    self._events.append(incoming)
                    continue
                if incoming.kind != "response":
                    raise ChildStdioError(f"unexpected adapter message kind {incoming.kind}")
                if incoming.id != message.id:
                    raise ChildStdioError(
                        f"adapter response id {incoming.id!r} does not match in-flight request {message.id!r}"
                    )
                return incoming

    def next_event(self) -> Envelope | None:
        if self._events:
            return self._events.popleft()
        while True:
            incoming = self._read_one()
            if incoming.kind == "event":
                return incoming
            raise ChildStdioError(
                f"unexpected unsolicited {incoming.kind}/{incoming.type} while waiting for event"
            )

    def close(self) -> None:
        """Mark the client closed without closing process-global stdin/stdout."""
        self._closed = True

    def __enter__(self) -> "ChildStdioAdapterClient":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _write(self, message: Envelope) -> None:
        if self._closed:
            raise ChildStdioError("child stdio protocol client is closed")
        payload = message.encode_line()
        with self._write_lock:
            try:
                self._writer.write(payload)
                self._writer.flush()
            except (BrokenPipeError, OSError, ValueError) as exc:
                raise ChildStdioError("GPL adapter parent closed runtime stdout protocol pipe") from exc

    def _read_one(self) -> Envelope:
        if self._closed:
            raise ChildStdioError("child stdio protocol client is closed")
        try:
            raw = self._reader.readline()
        except (OSError, ValueError) as exc:
            raise ChildStdioError("could not read GPL adapter protocol pipe") from exc
        if raw == b"":
            raise ChildStdioError("GPL adapter parent closed runtime stdin protocol pipe")
        if not raw.endswith(b"\n"):
            raise ChildStdioError("adapter protocol record ended without newline")
        try:
            return decode_line(raw)
        except Exception as exc:
            raise ChildStdioError(f"GPL adapter emitted malformed protocol data: {exc}") from exc
