# Copyright (c) 2026 halthinks. All rights reserved.
# SPDX-License-Identifier: LicenseRef-halthinks-Proprietary

from __future__ import annotations

import json
import math
import os
import tempfile
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping

MAX_RECORDS = 100_000
MAX_BYTES = 64 * 1024 * 1024
MAX_FRAME = 2_147_483_647


class EvidenceError(ValueError):
    pass


@dataclass(frozen=True)
class EvidenceRecord:
    id: str
    source_id: str
    source_fingerprint: str
    extractor_id: str
    extractor_version: str
    kind: str
    start_frame: int = -1
    end_frame: int = -1
    text: str = ""
    confidence: float = -1.0
    produced_utc: str = ""
    metadata: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_json(cls, value: Mapping[str, Any]) -> "EvidenceRecord":
        if not isinstance(value, Mapping):
            raise EvidenceError("evidence record must be an object")
        source_id = _required_string(value, "source_id")
        source_fingerprint = _required_string(value, "source_fingerprint")
        extractor_id = _required_string(value, "extractor_id")
        extractor_version = _required_string(value, "extractor_version")
        kind = _required_string(value, "kind")
        record_id = value.get("id")
        if record_id is None or record_id == "":
            record_id = str(uuid.uuid4())
        if not isinstance(record_id, str) or not record_id.strip():
            raise EvidenceError("evidence id must be a non-empty string")
        if len(record_id.strip()) > 1024:
            raise EvidenceError("evidence id exceeds 1024 characters")
        start_frame = _frame(value.get("start_frame", -1), "start_frame")
        end_frame = _frame(value.get("end_frame", -1), "end_frame")
        if start_frame >= 0 and end_frame >= 0 and end_frame < start_frame:
            raise EvidenceError("evidence end_frame must be >= start_frame")
        confidence = value.get("confidence", -1.0)
        if isinstance(confidence, bool) or not isinstance(confidence, (int, float)):
            raise EvidenceError("evidence confidence must be numeric")
        confidence = float(confidence)
        if not math.isfinite(confidence) or not (confidence == -1.0 or 0.0 <= confidence <= 1.0):
            raise EvidenceError("evidence confidence must be -1 (unknown) or between 0 and 1")
        text = value.get("text", "")
        if not isinstance(text, str):
            raise EvidenceError("evidence text must be a string")
        if len(text) > 1_048_576:
            raise EvidenceError("evidence text exceeds the 1 Mi-character bound")
        produced_utc = value.get("produced_utc") or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        if not isinstance(produced_utc, str) or not produced_utc.strip() or len(produced_utc.strip()) > 128:
            raise EvidenceError("produced_utc must be a non-empty string up to 128 characters")
        metadata = value.get("metadata", {})
        if not isinstance(metadata, dict):
            raise EvidenceError("evidence metadata must be an object")
        return cls(
            id=record_id.strip(),
            source_id=source_id,
            source_fingerprint=source_fingerprint,
            extractor_id=extractor_id,
            extractor_version=extractor_version,
            kind=kind,
            start_frame=start_frame,
            end_frame=end_frame,
            text=text,
            confidence=confidence,
            produced_utc=produced_utc.strip(),
            metadata=dict(metadata),
        )

    def to_json(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "source_id": self.source_id,
            "source_fingerprint": self.source_fingerprint,
            "extractor_id": self.extractor_id,
            "extractor_version": self.extractor_version,
            "kind": self.kind,
            "start_frame": self.start_frame,
            "end_frame": self.end_frame,
            "text": self.text,
            "confidence": self.confidence,
            "produced_utc": self.produced_utc,
            "metadata": dict(self.metadata),
        }

    @property
    def slice_key(self) -> tuple[str, str, str, str]:
        return (
            self.source_id,
            self.source_fingerprint,
            self.extractor_id,
            self.extractor_version,
        )


class EvidenceStore:
    """Editor-agnostic evidence sidecar store.

    The store has no project mutation API. Persisting an evidence record cannot
    change editor truth; consumers must use the adapter for authoritative state.
    """

    SCHEMA_VERSION = 1

    def __init__(self, path: str | os.PathLike[str] | None = None) -> None:
        self.path = Path(path) if path is not None else None
        self._records: list[EvidenceRecord] = []
        if self.path is not None and self.path.exists():
            self._records = self._read_file(self.path)

    def records(self) -> tuple[EvidenceRecord, ...]:
        return tuple(self._records)

    def replace_slice(self, records: Iterable[EvidenceRecord]) -> None:
        incoming = list(records)
        if not incoming:
            raise EvidenceError("replace_slice requires at least one record")
        key = incoming[0].slice_key
        if any(record.slice_key != key for record in incoming):
            raise EvidenceError("replace_slice records must share one source/extractor slice")
        retained = [record for record in self._records if record.slice_key != key]
        self._commit(retained + incoming)

    def replace_slices(self, records: Iterable[EvidenceRecord]) -> None:
        grouped: dict[tuple[str, str, str, str], list[EvidenceRecord]] = {}
        for record in records:
            grouped.setdefault(record.slice_key, []).append(record)
        if not grouped:
            raise EvidenceError("evidence_put requires at least one record")
        keys = set(grouped)
        retained = [record for record in self._records if record.slice_key not in keys]
        flattened: list[EvidenceRecord] = []
        for key in sorted(grouped):
            flattened.extend(grouped[key])
        self._commit(retained + flattened)

    def query(
        self,
        *,
        source_id: str | None = None,
        source_fingerprint: str | None = None,
        extractor_id: str | None = None,
        extractor_version: str | None = None,
        kind: str | None = None,
        start_frame: int | None = None,
        end_frame: int | None = None,
        limit: int = 1000,
    ) -> tuple[EvidenceRecord, ...]:
        if isinstance(limit, bool) or not isinstance(limit, int) or limit < 1 or limit > 10_000:
            raise EvidenceError("query limit must be an integer in 1..10000")
        if start_frame is not None:
            start_frame = _query_frame(start_frame, "start_frame")
        if end_frame is not None:
            end_frame = _query_frame(end_frame, "end_frame")
        if start_frame is not None and end_frame is not None and end_frame < start_frame:
            raise EvidenceError("query end_frame must be >= start_frame")

        result: list[EvidenceRecord] = []
        for record in self._records:
            if source_id is not None and record.source_id != source_id:
                continue
            if source_fingerprint is not None and record.source_fingerprint != source_fingerprint:
                continue
            if extractor_id is not None and record.extractor_id != extractor_id:
                continue
            if extractor_version is not None and record.extractor_version != extractor_version:
                continue
            if kind is not None and record.kind != kind:
                continue
            # Match the GPL adapter's evidence_get semantics exactly: a bounded
            # frame query cannot claim an unknown-range record intersects it.
            if start_frame is not None and (record.end_frame < 0 or record.end_frame < start_frame):
                continue
            if end_frame is not None and (record.start_frame < 0 or record.start_frame > end_frame):
                continue
            result.append(record)
            if len(result) >= limit:
                break
        return tuple(result)

    def _commit(self, records: list[EvidenceRecord]) -> None:
        if len(records) > MAX_RECORDS:
            raise EvidenceError(f"evidence store exceeds {MAX_RECORDS} records")
        root = {
            "version": self.SCHEMA_VERSION,
            "records": [record.to_json() for record in records],
        }
        encoded = json.dumps(root, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_BYTES:
            raise EvidenceError(f"evidence store exceeds {MAX_BYTES} bytes")
        if self.path is not None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            fd, temp_name = tempfile.mkstemp(prefix=self.path.name + ".", dir=str(self.path.parent))
            try:
                with os.fdopen(fd, "wb") as handle:
                    handle.write(encoded)
                    handle.flush()
                    os.fsync(handle.fileno())
                os.replace(temp_name, self.path)
            except Exception:
                try:
                    os.unlink(temp_name)
                except OSError:
                    pass
                raise
        self._records = list(records)

    @classmethod
    def _read_file(cls, path: Path) -> list[EvidenceRecord]:
        if path.stat().st_size > MAX_BYTES:
            raise EvidenceError(f"evidence store exceeds {MAX_BYTES} bytes")
        try:
            root = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise EvidenceError(f"could not read evidence store: {exc}") from exc
        if not isinstance(root, dict) or root.get("version") != cls.SCHEMA_VERSION:
            raise EvidenceError("unsupported or malformed evidence store schema")
        raw = root.get("records")
        if not isinstance(raw, list) or len(raw) > MAX_RECORDS:
            raise EvidenceError("unsupported or malformed evidence records")
        return [EvidenceRecord.from_json(item) for item in raw]


def _required_string(value: Mapping[str, Any], key: str) -> str:
    raw = value.get(key)
    if not isinstance(raw, str) or not raw.strip():
        raise EvidenceError(f"evidence {key} is required")
    result = raw.strip()
    maximum = 4096 if key in {"source_id", "source_fingerprint"} else 1024
    if len(result) > maximum:
        raise EvidenceError(f"evidence {key} exceeds {maximum} characters")
    return result


def _frame(raw: Any, label: str) -> int:
    if isinstance(raw, bool) or not isinstance(raw, int) or raw < -1 or raw > MAX_FRAME:
        raise EvidenceError(f"{label} must be an integer in -1..{MAX_FRAME}")
    return raw


def _query_frame(raw: Any, label: str) -> int:
    if isinstance(raw, bool) or not isinstance(raw, int) or raw < 0 or raw > MAX_FRAME:
        raise EvidenceError(f"query {label} must be an integer in 0..{MAX_FRAME}")
    return raw
