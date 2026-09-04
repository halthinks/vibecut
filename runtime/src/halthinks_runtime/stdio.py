# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import collections
import os
import queue
import subprocess
import threading
import time
from pathlib import Path
from typing import Mapping, Sequence

from .protocol import AdapterClient, Envelope, ProtocolError, decode_line


class StdioClientError(RuntimeError):
    pass


class StdioAdapterClient(AdapterClient):
    """NDJSON subprocess client for a GPL adapter shim.

    The runtime starts an adapter executable directly with ``shell=False``.
    Stdout is protocol-only. Stderr is captured separately as bounded
    diagnostics and never parsed as protocol authority.
    """

    def __init__(
        self,
        command: Sequence[str],
        *,
        cwd: str | os.PathLike[str] | None = None,
        env: Mapping[str, str] | None = None,
        response_timeout: float = 30.0,
        event_timeout: float = 30.0,
        max_diagnostic_lines: int = 200,
    ) -> None:
        if not command or not all(isinstance(part, str) and part for part in command):
            raise StdioClientError("adapter command must contain non-empty argv strings")
        if response_timeout <= 0 or event_timeout <= 0:
            raise StdioClientError("stdio timeouts must be positive")
        self._command = tuple(command)
        self._cwd = str(Path(cwd)) if cwd is not None else None
        self._env = dict(env) if env is not None else None
        self._response_timeout = float(response_timeout)
        self._event_timeout = float(event_timeout)
        self._diagnostics: collections.deque[str] = collections.deque(maxlen=max_diagnostic_lines)
        self._inbox: queue.Queue[Envelope | BaseException | None] = queue.Queue()
        self._events: collections.deque[Envelope] = collections.deque()
        self._exchange_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._process: subprocess.Popen[bytes] | None = None
        self._reader_thread: threading.Thread | None = None
        self._stderr_thread: threading.Thread | None = None

    def start(self) -> "StdioAdapterClient":
        if self._process is not None:
            raise StdioClientError("adapter process is already started")
        try:
            process = subprocess.Popen(
                list(self._command),
                cwd=self._cwd,
                env=self._env,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                shell=False,
                bufsize=0,
            )
        except OSError as exc:
            raise StdioClientError(f"could not start adapter process: {exc}") from exc
        if process.stdin is None or process.stdout is None or process.stderr is None:
            process.kill()
            raise StdioClientError("adapter process did not expose all stdio pipes")
        self._process = process
        self._reader_thread = threading.Thread(target=self._reader_loop, name="vibecut-adapter-stdout", daemon=True)
        self._stderr_thread = threading.Thread(target=self._stderr_loop, name="vibecut-adapter-stderr", daemon=True)
        self._reader_thread.start()
        self._stderr_thread.start()
        return self

    def close(self, *, terminate_timeout: float = 2.0) -> None:
        process = self._process
        if process is None:
            return
        self._process = None
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=terminate_timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=terminate_timeout)
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

    def __enter__(self) -> "StdioAdapterClient":
        return self.start()

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def read_hello(self, timeout: float | None = None) -> Envelope:
        deadline = self._event_timeout if timeout is None else timeout
        if deadline <= 0:
            raise StdioClientError("hello timeout must be positive")
        end = time.monotonic() + deadline
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise StdioClientError("timed out waiting for adapter hello")
            message = self._receive(remaining)
            if message.kind == "event" and message.type == "hello":
                return message
            if message.kind == "event":
                self._events.append(message)
                continue
            raise StdioClientError(f"received {message.kind}/{message.type} before adapter hello")

    def exchange(self, message: Envelope) -> Envelope:
        process = self._require_process()
        if message.kind != "request":
            raise StdioClientError("exchange accepts request envelopes only")
        with self._exchange_lock:
            self._write(message)
            end = time.monotonic() + self._response_timeout
            while True:
                remaining = end - time.monotonic()
                if remaining <= 0:
                    raise StdioClientError(f"timed out waiting for response to {message.id}")
                incoming = self._receive(remaining)
                if incoming.kind == "event":
                    self._events.append(incoming)
                    continue
                if incoming.kind != "response":
                    raise StdioClientError(f"unexpected adapter message kind {incoming.kind}")
                if incoming.id != message.id:
                    raise StdioClientError(
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
            raise StdioClientError(
                f"unexpected unsolicited {incoming.kind}/{incoming.type} while waiting for event"
            )

    def diagnostics(self) -> tuple[str, ...]:
        return tuple(self._diagnostics)

    def _write(self, message: Envelope) -> None:
        process = self._require_process()
        if process.stdin is None:
            raise StdioClientError("adapter stdin is unavailable")
        payload = message.encode_line()
        with self._write_lock:
            try:
                process.stdin.write(payload)
                process.stdin.flush()
            except (BrokenPipeError, OSError) as exc:
                raise StdioClientError(
                    "adapter stdin closed" + self._diagnostic_suffix()
                ) from exc

    def _receive(self, timeout: float) -> Envelope:
        try:
            item = self._inbox.get(timeout=timeout)
        except queue.Empty as exc:
            process = self._process
            if process is not None and process.poll() is not None:
                raise StdioClientError(
                    f"adapter exited with code {process.returncode}" + self._diagnostic_suffix()
                ) from exc
            raise StdioClientError("adapter protocol receive timed out") from exc
        if item is None:
            process = self._process
            code = process.returncode if process is not None else None
            raise StdioClientError(f"adapter stdout closed (exit={code})" + self._diagnostic_suffix())
        if isinstance(item, BaseException):
            raise StdioClientError(f"adapter protocol reader failed: {item}" + self._diagnostic_suffix()) from item
        return item

    def _reader_loop(self) -> None:
        process = self._process
        if process is None or process.stdout is None:
            self._inbox.put(StdioClientError("adapter stdout reader started without stdout"))
            return
        try:
            while True:
                raw = process.stdout.readline()
                if raw == b"":
                    self._inbox.put(None)
                    return
                if not raw.endswith(b"\n"):
                    self._inbox.put(ProtocolError("adapter stdout ended a protocol record without newline"))
                    return
                try:
                    self._inbox.put(decode_line(raw))
                except BaseException as exc:
                    self._inbox.put(exc)
                    return
        except BaseException as exc:
            self._inbox.put(exc)

    def _stderr_loop(self) -> None:
        process = self._process
        if process is None or process.stderr is None:
            return
        try:
            while True:
                raw = process.stderr.readline()
                if raw == b"":
                    return
                self._diagnostics.append(raw.decode("utf-8", errors="replace").rstrip("\r\n")[:4096])
        except OSError:
            return

    def _require_process(self) -> subprocess.Popen[bytes]:
        process = self._process
        if process is None:
            raise StdioClientError("adapter process is not started")
        if process.poll() is not None:
            raise StdioClientError(
                f"adapter exited with code {process.returncode}" + self._diagnostic_suffix()
            )
        return process

    def _diagnostic_suffix(self) -> str:
        if not self._diagnostics:
            return ""
        return "; stderr: " + " | ".join(list(self._diagnostics)[-5:])
