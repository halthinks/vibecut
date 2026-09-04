# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import collections
import queue
import sys
import threading
import time
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

    The client never imports editor types and never launches Kdenlive itself.
    """

    def __init__(
        self,
        reader: BinaryIO | None = None,
        writer: BinaryIO | None = None,
        *,
        response_timeout: float = 30.0,
        event_timeout: float = 30.0,
    ) -> None:
        if response_timeout <= 0 or event_timeout <= 0:
            raise ChildStdioError("stdio timeouts must be positive")
        self._reader = reader or sys.stdin.buffer
        self._writer = writer or sys.stdout.buffer
        self._response_timeout = float(response_timeout)
        self._event_timeout = float(event_timeout)
        self._inbox: queue.Queue[Envelope | BaseException | None] = queue.Queue()
        self._events: collections.deque[Envelope] = collections.deque()
        self._exchange_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._closed = False
        self._thread = threading.Thread(target=self._reader_loop, name="vibecut-parent-adapter-stdin", daemon=True)
        self._thread.start()

    def read_hello(self, timeout: float | None = None) -> Envelope:
        duration = self._event_timeout if timeout is None else timeout
        if duration <= 0:
            raise ChildStdioError("hello timeout must be positive")
        end = time.monotonic() + duration
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise ChildStdioError("timed out waiting for GPL adapter hello")
            message = self._receive(remaining)
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
            end = time.monotonic() + self._response_timeout
            while True:
                remaining = end - time.monotonic()
                if remaining <= 0:
                    raise ChildStdioError(f"timed out waiting for response to {message.id}")
                incoming = self._receive(remaining)
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
        end = time.monotonic() + self._event_timeout
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                return None
            incoming = self._receive(remaining)
            if incoming.kind == "event":
                return incoming
            raise ChildStdioError(
                f"unexpected unsolicited {incoming.kind}/{incoming.type} while waiting for event"
            )

    def close(self) -> None:
        self._closed = True

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

    def _receive(self, timeout: float) -> Envelope:
        try:
            item = self._inbox.get(timeout=timeout)
        except queue.Empty as exc:
            raise ChildStdioError("GPL adapter protocol receive timed out") from exc
        if item is None:
            raise ChildStdioError("GPL adapter parent closed runtime stdin protocol pipe")
        if isinstance(item, BaseException):
            raise ChildStdioError(f"GPL adapter protocol reader failed: {item}") from item
        return item

    def _reader_loop(self) -> None:
        try:
            while not self._closed:
                raw = self._reader.readline()
                if raw == b"":
                    self._inbox.put(None)
                    return
                if not raw.endswith(b"\n"):
                    self._inbox.put(ChildStdioError("adapter protocol record ended without newline"))
                    return
                try:
                    self._inbox.put(decode_line(raw))
                except BaseException as exc:
                    self._inbox.put(exc)
                    return
        except BaseException as exc:
            self._inbox.put(exc)
