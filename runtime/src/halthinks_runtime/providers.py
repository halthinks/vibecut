# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import urllib.request
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Mapping, Protocol

MAX_EVENT_BYTES = 2 * 1024 * 1024
MAX_BODY_BYTES = 16 * 1024 * 1024


class ProviderError(RuntimeError):
    pass


@dataclass(frozen=True)
class ModelRequest:
    endpoint: str
    headers: dict[str, str]
    body: dict[str, Any]

    def encoded_body(self) -> bytes:
        data = json.dumps(self.body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(data) > MAX_BODY_BYTES:
            raise ProviderError(f"provider request body exceeds {MAX_BODY_BYTES} bytes")
        return data


class ModelProvider(Protocol):
    @property
    def id(self) -> str:
        ...

    def build_request(
        self,
        system_prompt: str,
        tools: list[dict[str, Any]],
        messages: list[dict[str, Any]],
        max_tokens: int,
    ) -> ModelRequest:
        ...

    def normalize_event(self, data: bytes) -> dict[str, Any] | None:
        ...


Transport = Callable[[ModelRequest], Iterable[bytes]]


class ProviderClient:
    """Provider-neutral model request/stream client.

    Transport is injectable so provider contracts can be tested without network
    access. The default urllib transport is HTTP-only plumbing and carries no
    editor/Kdenlive knowledge.
    """

    def __init__(self, transport: Transport | None = None) -> None:
        self._transport = transport or urllib_transport

    def run(
        self,
        provider: ModelProvider,
        *,
        system_prompt: str,
        tools: list[dict[str, Any]],
        messages: list[dict[str, Any]],
        max_tokens: int,
    ) -> tuple[dict[str, Any], ...]:
        if isinstance(max_tokens, bool) or not isinstance(max_tokens, int) or max_tokens < 1 or max_tokens > 1_000_000:
            raise ProviderError("max_tokens must be an integer in 1..1000000")
        request = provider.build_request(system_prompt, tools, messages, max_tokens)
        _validate_request(request)
        normalized: list[dict[str, Any]] = []
        for raw in self._transport(request):
            if not isinstance(raw, (bytes, bytearray)):
                raise ProviderError("provider transport yielded a non-bytes event")
            data = bytes(raw)
            if len(data) > MAX_EVENT_BYTES:
                raise ProviderError(f"provider event exceeds {MAX_EVENT_BYTES} bytes")
            event = provider.normalize_event(data)
            if event is None:
                continue
            if not isinstance(event, dict):
                raise ProviderError("provider normalize_event must return an object or None")
            normalized.append(dict(event))
        return tuple(normalized)


def urllib_transport(request: ModelRequest, *, timeout: float = 120.0) -> Iterable[bytes]:
    _validate_request(request)
    body = request.encoded_body()
    headers = {str(key): str(value) for key, value in request.headers.items()}
    headers.setdefault("Content-Type", "application/json")
    http_request = urllib.request.Request(request.endpoint, data=body, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(http_request, timeout=timeout) as response:  # nosec B310 - endpoint is provider-owned configuration
            for line in response:
                if len(line) > MAX_EVENT_BYTES:
                    raise ProviderError(f"provider event exceeds {MAX_EVENT_BYTES} bytes")
                yield bytes(line)
    except ProviderError:
        raise
    except Exception as exc:
        raise ProviderError(f"provider transport failed: {exc}") from exc


def _validate_request(request: ModelRequest) -> None:
    if not isinstance(request, ModelRequest):
        raise ProviderError("provider build_request must return ModelRequest")
    if not isinstance(request.endpoint, str) or not request.endpoint.startswith(("https://", "http://")):
        raise ProviderError("provider endpoint must be an http(s) URL")
    if not isinstance(request.headers, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in request.headers.items()):
        raise ProviderError("provider headers must be a string mapping")
    if not isinstance(request.body, dict):
        raise ProviderError("provider body must be an object")
    request.encoded_body()
