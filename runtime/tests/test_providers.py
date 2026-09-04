# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import unittest

from halthinks_runtime.providers import ModelRequest, ProviderClient, ProviderError


class FakeProvider:
    id = "fake"

    def build_request(self, system_prompt, tools, messages, max_tokens):
        return ModelRequest(
            endpoint="https://provider.invalid/v1/messages",
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


if __name__ == "__main__":
    unittest.main()
