# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable, Mapping

MAX_EXACT_JSON_INTEGER = 9_007_199_254_740_991


class PlanValidationError(ValueError):
    """Raised when an EditPlan violates the public runtime contract."""


@dataclass(frozen=True)
class PlanOperation:
    id: str
    tool: str
    input: dict[str, Any]
    depends_on: tuple[str, ...]
    expected_postconditions: tuple[str, ...]

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "PlanOperation":
        if not isinstance(value, Mapping):
            raise PlanValidationError("operation must be an object")
        op_id = _required_string(value, "id")
        tool = _required_string(value, "tool")
        raw_input = value.get("input")
        if not isinstance(raw_input, dict):
            raise PlanValidationError(f"operation {op_id} input must be an object")
        depends_on = _string_tuple(value.get("depends_on"), f"operation {op_id} depends_on")
        expected = _string_tuple(value.get("expected_postconditions"), f"operation {op_id} expected_postconditions")
        return cls(op_id, tool, dict(raw_input), depends_on, expected)

    def to_json(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "tool": self.tool,
            "input": dict(self.input),
            "depends_on": list(self.depends_on),
            "expected_postconditions": list(self.expected_postconditions),
        }


@dataclass(frozen=True)
class EditPlan:
    id: str
    base_revision: int
    objective: str
    operations: tuple[PlanOperation, ...]

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "EditPlan":
        if not isinstance(value, Mapping):
            raise PlanValidationError("plan must be an object")
        plan_id = _required_string(value, "id")
        objective = _required_string(value, "objective")
        base_revision = _exact_integer(value.get("base_revision"), "base_revision", 0, MAX_EXACT_JSON_INTEGER)
        raw_operations = value.get("operations")
        if not isinstance(raw_operations, list):
            raise PlanValidationError("operations must be an array")
        operations = tuple(PlanOperation.from_json(item) for item in raw_operations)
        plan = cls(plan_id, base_revision, objective, operations)
        validate_plan(plan)
        return plan

    def to_json(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "base_revision": self.base_revision,
            "objective": self.objective,
            "operations": [operation.to_json() for operation in self.operations],
        }

    def operation(self, operation_id: str) -> PlanOperation:
        for operation in self.operations:
            if operation.id == operation_id:
                return operation
        raise PlanValidationError(f"unknown operation id: {operation_id}")


def validate_plan(plan: EditPlan) -> tuple[str, ...]:
    """Validate semantic EditPlan invariants and return deterministic execution order."""
    if not plan.id.strip():
        raise PlanValidationError("plan id is required")
    if not plan.objective.strip():
        raise PlanValidationError("plan objective is required")
    if not plan.operations:
        raise PlanValidationError("plan must contain at least one operation")

    by_id: dict[str, PlanOperation] = {}
    for operation in plan.operations:
        if not operation.id.strip():
            raise PlanValidationError("operation id is required")
        if operation.id in by_id:
            raise PlanValidationError(f"duplicate operation id: {operation.id}")
        if not operation.tool.strip():
            raise PlanValidationError(f"operation {operation.id} has no tool")
        if len(set(operation.depends_on)) != len(operation.depends_on):
            raise PlanValidationError(f"operation {operation.id} has duplicate dependencies")
        by_id[operation.id] = operation

    for operation in plan.operations:
        for dependency in operation.depends_on:
            if dependency == operation.id:
                raise PlanValidationError(f"operation {operation.id} depends on itself")
            if dependency not in by_id:
                raise PlanValidationError(
                    f"operation {operation.id} depends on unknown operation {dependency}"
                )

    order: list[str] = []
    complete: set[str] = set()
    while len(order) < len(plan.operations):
        progressed = False
        for operation in plan.operations:
            if operation.id in complete:
                continue
            if all(dependency in complete for dependency in operation.depends_on):
                order.append(operation.id)
                complete.add(operation.id)
                progressed = True
        if not progressed:
            raise PlanValidationError("operation dependency graph contains a cycle")
    return tuple(order)


def _required_string(value: Mapping[str, Any], key: str) -> str:
    raw = value.get(key)
    if not isinstance(raw, str):
        raise PlanValidationError(f"{key} must be a string")
    result = raw.strip()
    if not result:
        raise PlanValidationError(f"{key} is required")
    if len(result) > 16_384:
        raise PlanValidationError(f"{key} exceeds the bounded contract")
    return result


def _string_tuple(raw: Any, label: str) -> tuple[str, ...]:
    if not isinstance(raw, list):
        raise PlanValidationError(f"{label} must be an array")
    values: list[str] = []
    for item in raw:
        if not isinstance(item, str) or not item.strip():
            raise PlanValidationError(f"{label} may contain non-empty strings only")
        values.append(item.strip())
    return tuple(values)


def _exact_integer(raw: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(raw, bool) or not isinstance(raw, int):
        raise PlanValidationError(f"{label} must be an integer")
    if raw < minimum or raw > maximum:
        raise PlanValidationError(f"{label} must be in {minimum}..{maximum}")
    return raw
