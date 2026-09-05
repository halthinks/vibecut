# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass
from typing import Any, Mapping, Protocol

PROTOCOL_VERSION = 1
# Must remain byte-for-byte compatible with
# VibeCutRuntimeStdioTransport::MaxProtocolLineBytes on the GPL adapter side.
MAX_MESSAGE_BYTES = 2 * 1024 * 1024
MESSAGE_KINDS = {"request", "response", "event"}
MESSAGE_TYPES = {
    "hello",
    "inspect",
    "propose_plan",
    "authorize",
    "invoke",
    "verify",
    "complete_plan",
    "abort_plan",
    "job_update",
    "revision",
    "evidence_put",
    "evidence_get",
    "error",
}


class ProtocolError(RuntimeError):
    pass


@dataclass(frozen=True)
class Envelope:
    v: int
    id: str
    kind: str
    type: str
    payload: dict[str, Any]

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "Envelope":
        if not isinstance(value, Mapping):
            raise ProtocolError("protocol envelope must be an object")
        if value.get("v") != PROTOCOL_VERSION:
            raise ProtocolError(f"unsupported protocol version: {value.get('v')!r}")
        message_id = value.get("id")
        kind = value.get("kind")
        message_type = value.get("type")
        payload = value.get("payload")
        if not isinstance(message_id, str) or not message_id.strip() or len(message_id) > 1024:
            raise ProtocolError("protocol id must be a non-empty string up to 1024 characters")
        if kind not in MESSAGE_KINDS:
            raise ProtocolError(f"unsupported protocol kind: {kind!r}")
        if message_type not in MESSAGE_TYPES:
            raise ProtocolError(f"unsupported protocol type: {message_type!r}")
        if not isinstance(payload, dict):
            raise ProtocolError("protocol payload must be an object")
        return cls(PROTOCOL_VERSION, message_id.strip(), str(kind), str(message_type), dict(payload))

    def to_json(self) -> dict[str, Any]:
        return {
            "v": self.v,
            "id": self.id,
            "kind": self.kind,
            "type": self.type,
            "payload": dict(self.payload),
        }

    def encode_line(self) -> bytes:
        raw = json.dumps(self.to_json(), ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(raw) > MAX_MESSAGE_BYTES:
            raise ProtocolError(f"protocol envelope exceeds {MAX_MESSAGE_BYTES} bytes")
        return raw + b"\n"


def decode_line(raw: bytes) -> Envelope:
    if not isinstance(raw, (bytes, bytearray)):
        raise ProtocolError("protocol input must be bytes")
    if len(raw) > MAX_MESSAGE_BYTES + 1:
        raise ProtocolError(f"protocol line exceeds {MAX_MESSAGE_BYTES} bytes")
    try:
        value = json.loads(bytes(raw).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError(f"malformed protocol JSON: {exc}") from exc
    return Envelope.from_json(value)


def request(message_type: str, payload: Mapping[str, Any], message_id: str | None = None) -> Envelope:
    if message_type not in MESSAGE_TYPES:
        raise ProtocolError(f"unsupported request type: {message_type}")
    return Envelope(
        PROTOCOL_VERSION,
        message_id or f"msg-{uuid.uuid4()}",
        "request",
        message_type,
        dict(payload),
    )


class AdapterClient(Protocol):
    """Minimal transport-neutral adapter boundary used by the commercial runtime."""

    def exchange(self, message: Envelope) -> Envelope:
        """Send one request and synchronously return its correlated response."""

    def next_event(self) -> Envelope | None:
        """Return one pending adapter event, or None when no event is queued."""
