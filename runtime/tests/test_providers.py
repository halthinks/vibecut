# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import unittest

from halthinks_runtime.providers import ModelRequest, ProviderClient, ProviderError


class FakeProvider:
    id = "fake"

    def __init__(self, endpoint: str = "https://provider.invalid/v1/messages") -> None:
        self.endpoint = endpoint

    def build_request(self, system_prompt, tools, messages, max_tokens):
        return ModelRequest(
            endpoint=self.endpoint,
            headers={"Authorization": "Bearer secret"},
            body={
                "system": system_prompt,
                "tools": tools,
                "messages": messages,
                "max_tokens": max_tokens,
            },
        )

    def normalize_event(self, data: bytes):
        value = json.loads(data.decode("utf-8"))
        if value.get("ignore"):
            return None
        return {"type": value["type"], "text": value.get("text", "")}


class ProviderClientTests(unittest.TestCase):
    def test_injected_transport_normalizes_provider_events(self) -> None:
        captured = []

        def transport(request):
            captured.append(request)
            yield b'{"type":"delta","text":"hello"}'
            yield b'{"ignore":true}'
            yield b'{"type":"done"}'

        client = ProviderClient(transport)
        events = client.run(
            FakeProvider(),
            system_prompt="system",
            tools=[{"name": "x"}],
            messages=[{"role": "user", "content": "hi"}],
            max_tokens=256,
        )
        self.assertEqual(events, ({"type": "delta", "text": "hello"}, {"type": "done", "text": ""}))
        self.assertEqual(len(captured), 1)
        self.assertEqual(captured[0].body["max_tokens"], 256)

    def test_invalid_token_bound_fails_before_transport(self) -> None:
        called = False

        def transport(request):
            nonlocal called
            called = True
            yield b"{}"

        with self.assertRaises(ProviderError):
            ProviderClient(transport).run(
                FakeProvider(), system_prompt="s", tools=[], messages=[], max_tokens=0
            )
        self.assertFalse(called)

    def test_non_bytes_transport_event_fails_closed(self) -> None:
        def transport(request):
            yield "not-bytes"

        with self.assertRaises(ProviderError):
            ProviderClient(transport).run(
                FakeProvider(), system_prompt="s", tools=[], messages=[], max_tokens=1
            )

    def test_remote_cleartext_provider_endpoint_is_rejected_before_transport(self) -> None:
        called = False

        def transport(request):
            nonlocal called
            called = True
            yield b'{"type":"done"}'

        with self.assertRaisesRegex(ProviderError, "require HTTPS"):
            ProviderClient(transport).run(
                FakeProvider("http://api.example.com/v1/messages"),
                system_prompt="s", tools=[], messages=[], max_tokens=1,
            )
        self.assertFalse(called)

    def test_loopback_http_provider_endpoint_is_allowed_for_local_development(self) -> None:
        captured = []

        def transport(request):
            captured.append(request.endpoint)
            yield b'{"type":"done"}'

        events = ProviderClient(transport).run(
            FakeProvider("http://127.0.0.1:11434/v1/messages"),
            system_prompt="s", tools=[], messages=[], max_tokens=1,
        )
        self.assertEqual(captured, ["http://127.0.0.1:11434/v1/messages"])
        self.assertEqual(events[0]["type"], "done")

    def test_url_embedded_credentials_and_fragments_are_rejected(self) -> None:
        for endpoint in (
            "https://user:pass@example.com/v1/messages",
            "https://example.com/v1/messages#secret",
        ):
            with self.subTest(endpoint=endpoint):
                with self.assertRaises(ProviderError):
                    ProviderClient(lambda request: ()).run(
                        FakeProvider(endpoint), system_prompt="s", tools=[], messages=[], max_tokens=1
                    )


if __name__ == "__main__":
    unittest.main()
