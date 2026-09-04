# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

from dataclasses import dataclass

from .contracts import MAX_EXACT_JSON_INTEGER


class RevisionError(RuntimeError):
    """Raised when adapter revision state violates the runtime contract."""


class StaleRevisionError(RevisionError):
    """Raised when a valid revision token no longer matches authorized state."""


@dataclass
class RevisionGate:
    """Track immutable plan provenance separately from moving execution revision.

    ``base_revision`` is frozen when the plan is created. ``expected_revision``
    is established only when the adapter authorizes that plan and then advances
    after each successful adapter-owned project mutation.
    """

    base_revision: int
    expected_revision: int | None = None
    authorized: bool = False

    def __post_init__(self) -> None:
        _validate_revision(self.base_revision, "base_revision")
        if self.expected_revision is not None:
            _validate_revision(self.expected_revision, "expected_revision")

    def authorize(self, current_revision: int) -> int:
        _validate_revision(current_revision, "current_revision")
        if current_revision != self.base_revision:
            raise StaleRevisionError(
                f"plan was created for revision {self.base_revision} but current revision is {current_revision}"
            )
        self.expected_revision = current_revision
        self.authorized = True
        return current_revision

    def require_current(self, current_revision: int) -> int:
        _validate_revision(current_revision, "current_revision")
        if not self.authorized or self.expected_revision is None:
            raise RevisionError("plan has not been authorized")
        if current_revision != self.expected_revision:
            raise StaleRevisionError(
                f"expected revision {self.expected_revision} but adapter reports {current_revision}"
            )
        return current_revision

    def advance(self, revision_before: int, revision_after: int) -> int:
        _validate_revision(revision_before, "revision_before")
        _validate_revision(revision_after, "revision_after")
        self.require_current(revision_before)
        if revision_after < revision_before:
            raise RevisionError(
                f"adapter revision regressed from {revision_before} to {revision_after}"
            )
        self.expected_revision = revision_after
        return revision_after

    def observe_external(self, current_revision: int) -> None:
        """Fail when unrelated editor state drifts while more plan work remains."""
        self.require_current(current_revision)

    def close(self) -> None:
        self.authorized = False
        self.expected_revision = None


def _validate_revision(value: int, label: str) -> None:
    if isinstance(value, bool) or not isinstance(value, int):
        raise RevisionError(f"{label} must be an integer")
    if value < 0 or value > MAX_EXACT_JSON_INTEGER:
        raise RevisionError(
            f"{label} must be in 0..{MAX_EXACT_JSON_INTEGER}"
        )
