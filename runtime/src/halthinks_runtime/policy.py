# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any, Mapping


class ToolRisk(str, Enum):
    READ_ONLY = "read_only"
    REVERSIBLE_EDIT = "reversible_edit"
    MAJOR_EDIT = "major_edit"
    EXTERNAL_SIDE_EFFECT = "external_side_effect"
    IRREVERSIBLE = "irreversible"


class TrustMode(str, Enum):
    OFF = "off"
    AUTO = "auto"
    TURBO = "turbo"


@dataclass(frozen=True)
class ToolPolicy:
    name: str
    risk: ToolRisk
    reversible: bool
    mutates_project: bool
    asynchronous: bool
    confirmation_required: bool
    auto_allowed: bool
    enabled: bool

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "ToolPolicy":
        if not isinstance(value, Mapping):
            raise ValueError("tool policy must be an object")
        name = value.get("name")
        if not isinstance(name, str) or not name.strip():
            raise ValueError("tool policy name is required")
        try:
            risk = ToolRisk(value.get("risk"))
        except (ValueError, TypeError) as exc:
            raise ValueError("unsupported tool policy risk") from exc
        return cls(
            name=name.strip(),
            risk=risk,
            reversible=_boolean(value, "reversible"),
            mutates_project=_boolean(value, "mutates_project"),
            asynchronous=_boolean(value, "async"),
            confirmation_required=_boolean(value, "confirmation_required"),
            auto_allowed=_boolean(value, "auto_allowed"),
            enabled=_boolean(value, "enabled"),
        )

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "risk": self.risk.value,
            "reversible": self.reversible,
            "mutates_project": self.mutates_project,
            "async": self.asynchronous,
            "confirmation_required": self.confirmation_required,
            "auto_allowed": self.auto_allowed,
            "enabled": self.enabled,
        }

    def requires_confirmation(self, mode: TrustMode) -> bool:
        if self.risk is ToolRisk.READ_ONLY:
            return False
        if self.risk is ToolRisk.IRREVERSIBLE or self.confirmation_required:
            return True
        if self.auto_allowed:
            return False
        if mode is TrustMode.OFF:
            return True
        if mode is TrustMode.AUTO:
            return self.risk in {ToolRisk.MAJOR_EDIT, ToolRisk.EXTERNAL_SIDE_EFFECT}
        return False


def plan_requires_confirmation(tool_names: list[str] | tuple[str, ...],
                               policies: Mapping[str, ToolPolicy],
                               mode: TrustMode) -> bool:
    for name in tool_names:
        policy = policies.get(name)
        if policy is None or not policy.enabled:
            return True
        if policy.requires_confirmation(mode):
            return True
    return False


def _boolean(value: Mapping[str, Any], key: str) -> bool:
    raw = value.get(key)
    if not isinstance(raw, bool):
        raise ValueError(f"tool policy {key} must be boolean")
    return raw
