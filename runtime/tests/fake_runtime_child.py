#!/usr/bin/env python3
# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import sys

from halthinks_runtime.child_stdio import ChildStdioAdapterClient
from halthinks_runtime.session import RuntimeSession


def main() -> int:
    client = ChildStdioAdapterClient(response_timeout=5.0, event_timeout=5.0)
    try:
        session = RuntimeSession()
        hello = session.accept_hello(client.read_hello())
        session.prepare_plan(
            {
                "id": "parent-hosted-plan",
                "base_revision": hello.project_revision,
                "objective": "prove editor-hosted runtime child topology",
                "operations": [
                    {
                        "id": "op-1",
                        "tool": "set_value",
                        "input": {"value": 10},
                        "depends_on": [],
                        "expected_postconditions": ["value is 10"],
                    },
                    {
                        "id": "op-2",
                        "tool": "add_value",
                        "input": {"delta": 3},
                        "depends_on": ["op-1"],
                        "expected_postconditions": ["value is 13"],
                    },
                ],
            }
        )
        session.submit_plan(client)
        authorization = client.next_event()
        if authorization is None:
            print("no authorization event", file=sys.stderr, flush=True)
            return 2
        session.accept_authorization(authorization)
        result = session.execute_authorized(client)
        final_value = result.verification_results[-1]["inspection_result"].get("value")
        if result.final_revision != hello.project_revision + 2 or final_value != 13:
            print(
                f"unexpected final state revision={result.final_revision} value={final_value}",
                file=sys.stderr,
                flush=True,
            )
            return 3
        print(
            f"runtime child success revision={result.final_revision} value={final_value}",
            file=sys.stderr,
            flush=True,
        )
        return 0
    except Exception as exc:
        print(f"runtime child failed: {exc}", file=sys.stderr, flush=True)
        return 4
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
