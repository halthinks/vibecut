# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any

MAX_RESULT_BYTES = 512 * 1024


class JobError(RuntimeError):
    pass


class JobState(str, Enum):
    QUEUED = "queued"
    RUNNING = "running"
    CANCEL_REQUESTED = "cancel_requested"
    SUCCEEDED = "succeeded"
    FAILED = "failed"
    CANCELLED = "cancelled"

    @property
    def terminal(self) -> bool:
        return self in {JobState.SUCCEEDED, JobState.FAILED, JobState.CANCELLED}


@dataclass
class JobRecord:
    id: str
    kind: str
    label: str
    state: JobState = JobState.QUEUED
    progress: int = -1
    message: str = ""
    cancelable: bool = False
    result: dict[str, Any] = field(default_factory=dict)

    def to_json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "id": self.id,
            "kind": self.kind,
            "label": self.label,
            "state": self.state.value,
            "progress": self.progress,
            "message": self.message,
            "cancelable": self.cancelable,
        }
        if self.result:
            payload["result"] = dict(self.result)
        return payload


class JobManager:
    """Editor-independent bounded job lifecycle.

    The manager owns state only. It does not spawn editor jobs, mutate projects,
    or infer success from process exit without a caller explicitly recording it.
    """

    def __init__(self) -> None:
        self._jobs: dict[str, JobRecord] = {}
        self._order: list[str] = []

    def create(self, kind: str, label: str, cancelable: bool = False) -> JobRecord:
        if not isinstance(kind, str) or not kind.strip():
            raise JobError("job kind is required")
        if not isinstance(label, str) or not label.strip():
            raise JobError("job label is required")
        job_id = str(uuid.uuid4())
        job = JobRecord(job_id, kind.strip(), label.strip(), cancelable=bool(cancelable))
        self._jobs[job_id] = job
        self._order.append(job_id)
        return job

    def get(self, job_id: str) -> JobRecord:
        try:
            return self._jobs[job_id]
        except KeyError as exc:
            raise JobError(f"unknown job: {job_id}") from exc

    def all(self) -> tuple[JobRecord, ...]:
        return tuple(self._jobs[job_id] for job_id in self._order)

    def mark_running(self, job_id: str, message: str = "") -> JobRecord:
        job = self.get(job_id)
        self._transition(job, JobState.RUNNING, {JobState.QUEUED})
        job.message = message
        return job

    def set_progress(self, job_id: str, progress: int, message: str | None = None) -> JobRecord:
        job = self.get(job_id)
        if job.state not in {JobState.RUNNING, JobState.CANCEL_REQUESTED}:
            raise JobError("progress may be updated only while a job is active")
        if isinstance(progress, bool) or not isinstance(progress, int) or progress < -1 or progress > 100:
            raise JobError("progress must be an integer in -1..100")
        job.progress = progress
        if message is not None:
            job.message = str(message)
        return job

    def set_result(self, job_id: str, result: dict[str, Any]) -> JobRecord:
        job = self.get(job_id)
        if job.state.terminal:
            raise JobError("cannot set a result on a terminal job")
        if not isinstance(result, dict):
            raise JobError("job result must be an object")
        encoded = json.dumps(result, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        if len(encoded) > MAX_RESULT_BYTES:
            raise JobError(f"job result exceeds {MAX_RESULT_BYTES} bytes")
        job.result = dict(result)
        return job

    def request_cancel(self, job_id: str) -> JobRecord:
        job = self.get(job_id)
        if not job.cancelable:
            raise JobError("job is not cancelable")
        if job.state not in {JobState.QUEUED, JobState.RUNNING}:
            raise JobError("job is not in a cancelable state")
        job.state = JobState.CANCEL_REQUESTED
        return job

    def mark_succeeded(self, job_id: str, message: str = "") -> JobRecord:
        job = self.get(job_id)
        self._transition(job, JobState.SUCCEEDED, {JobState.RUNNING, JobState.CANCEL_REQUESTED})
        job.message = message
        if job.progress >= 0:
            job.progress = 100
        return job

    def mark_failed(self, job_id: str, message: str) -> JobRecord:
        job = self.get(job_id)
        self._transition(job, JobState.FAILED, {JobState.QUEUED, JobState.RUNNING, JobState.CANCEL_REQUESTED})
        job.message = str(message)
        job.result = {}
        return job

    def mark_cancelled(self, job_id: str, message: str = "") -> JobRecord:
        job = self.get(job_id)
        self._transition(job, JobState.CANCELLED, {JobState.CANCEL_REQUESTED})
        job.message = message
        job.result = {}
        return job

    @staticmethod
    def _transition(job: JobRecord, target: JobState, allowed: set[JobState]) -> None:
        if job.state not in allowed:
            raise JobError(f"invalid job transition {job.state.value} -> {target.value}")
        job.state = target
