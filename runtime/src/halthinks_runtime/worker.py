# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import sys
from dataclasses import replace
from typing import Any

from .child_stdio import ChildStdioAdapterClient, ChildStdioError
from .contracts import EditPlan
from .protocol import Envelope
from .session import RuntimeSession, SessionError


def _hello_with_revision(hello: Envelope, revision: int) -> Envelope:
    payload = dict(hello.payload)
    payload["project_revision"] = revision
    return Envelope(hello.v, hello.id, hello.kind, hello.type, payload)


def _handoff_plan(event: Envelope) -> tuple[EditPlan, int]:
    if event.kind != "event" or event.type != "plan_handoff":
        raise SessionError("hosted worker requires a plan_handoff event")
    raw_plan: Any = event.payload.get("plan")
    revision = event.payload.get("project_revision")
    if isinstance(revision, bool) or not isinstance(revision, int) or revision < 0:
        raise SessionError("plan_handoff requires an exact non-negative project_revision")
    if not isinstance(raw_plan, dict):
        raise SessionError("plan_handoff requires a plan object")
    plan = EditPlan.from_json(raw_plan)
    if plan.base_revision != revision:
        raise SessionError("plan_handoff plan base_revision does not match adapter project_revision")
    return plan, revision


def main() -> int:
    """Run the proprietary orchestration worker as a child of the GPL editor.

    The GPL parent remains authoritative for tools, authorization, revisions,
    native mutations and Undo. The worker accepts only host-staged plans and then
    drives them through RuntimeSession over the versioned NDJSON protocol.
    """
    client = ChildStdioAdapterClient()
    active = False
    try:
        initial_hello = client.read_hello()
        current_hello = initial_hello
        session = RuntimeSession()
        session.accept_hello(current_hello)

        while True:
            try:
                event = client.next_event()
            except ChildStdioError as exc:
                if not active and "closed runtime stdin" in str(exc):
                    return 0
                raise
            if event is None:
                continue
            if event.type == "error":
                raise SessionError(f"adapter error event: {event.payload.get('code')}: {event.payload.get('message')}")

            if event.type == "plan_handoff":
                if active:
                    raise SessionError("received a second plan_handoff while another plan is active")
                plan, revision = _handoff_plan(event)
                current_hello = _hello_with_revision(initial_hello, revision)
                session = RuntimeSession()
                session.accept_hello(current_hello)
                session.prepare_plan(plan)
                active = True
                continue

            if event.type == "authorize":
                if not active:
                    raise SessionError("received authorization without an active handed-off plan")
                if event.payload.get("decision") == "rejected":
                    try:
                        session.accept_authorization(event)
                    except SessionError:
                        pass
                    active = False
                    continue
                session.accept_authorization(event)
                result = session.execute_authorized(client)
                current_hello = _hello_with_revision(initial_hello, result.final_revision)
                session = RuntimeSession()
                session.accept_hello(current_hello)
                active = False
                continue

            # Job/revision events are consumed synchronously by RuntimeSession
            # while an authorized plan is executing. Receiving one here means it
            # is not attributable to an active operation and therefore carries no
            # execution authority.
            if event.type in {"job_update", "revision"}:
                continue

    except Exception as exc:
        print(f"halthinks runtime worker failed: {exc}", file=sys.stderr, flush=True)
        return 2
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
