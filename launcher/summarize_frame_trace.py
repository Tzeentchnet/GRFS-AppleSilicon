#!/usr/bin/env python3
"""Summarize a local GRFS frame trace without exporting per-pipeline keys."""

from __future__ import annotations

import argparse
import csv
import io
import json
import math
from pathlib import Path
from typing import Iterable, Optional

REQUIRED_COLUMNS = {
    "schema_version",
    "event_sequence",
    "timestamp_ns",
    "guest_frame_id",
    "event_type",
    "correlation_id",
    "duration_ns",
    "redacted_key_hash",
    "state",
}


def _optional_int(value: str) -> Optional[int]:
    return int(value, 0) if value else None


def _nearest_rank(values: Iterable[float], percentile: float) -> Optional[float]:
    ordered = sorted(values)
    if not ordered:
        return None
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def _duration_summary(milliseconds: list[float], hitch_ms: float) -> dict:
    if not milliseconds:
        return {
            "count": 0,
            "mean_ms": None,
            "p50_ms": None,
            "p95_ms": None,
            "p99_ms": None,
            "max_ms": None,
            "hitches_at_or_above_threshold": 0,
        }
    return {
        "count": len(milliseconds),
        "mean_ms": sum(milliseconds) / len(milliseconds),
        "p50_ms": _nearest_rank(milliseconds, 0.50),
        "p95_ms": _nearest_rank(milliseconds, 0.95),
        "p99_ms": _nearest_rank(milliseconds, 0.99),
        "max_ms": max(milliseconds),
        "hitches_at_or_above_threshold": sum(
            duration >= hitch_ms for duration in milliseconds
        ),
    }


def _read_trace(path: Path) -> tuple[dict[str, str], list[dict]]:
    metadata: dict[str, str] = {}
    csv_lines: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines(keepends=True):
        if line.startswith("# "):
            key, separator, value = line[2:].strip().partition("=")
            if separator:
                metadata[key] = value
            continue
        csv_lines.append(line)
    if not csv_lines:
        raise ValueError("Trace has no CSV header.")

    reader = csv.DictReader(io.StringIO("".join(csv_lines)))
    if reader.fieldnames is None or not REQUIRED_COLUMNS.issubset(reader.fieldnames):
        raise ValueError("Trace CSV does not contain the required schema columns.")
    events = []
    previous_sequence = 0
    previous_timestamp = 0
    for row in reader:
        if row["schema_version"] != "1":
            raise ValueError(f"Unsupported trace schema {row['schema_version']!r}.")
        event = {
            "sequence": int(row["event_sequence"]),
            "timestamp_ns": int(row["timestamp_ns"]),
            "guest_frame_id": _optional_int(row["guest_frame_id"]),
            "event_type": row["event_type"],
            "correlation_id": _optional_int(row["correlation_id"]),
            "duration_ns": _optional_int(row["duration_ns"]),
            "state": row["state"],
        }
        if event["sequence"] <= previous_sequence:
            raise ValueError("Trace event sequences are not strictly increasing.")
        if event["timestamp_ns"] < previous_timestamp:
            raise ValueError("Trace timestamps are not monotonic.")
        previous_sequence = event["sequence"]
        previous_timestamp = event["timestamp_ns"]
        events.append(event)
    return metadata, events


def summarize(path: Path, hitch_ms: float = 16.67) -> dict:
    metadata, events = _read_trace(path)
    timestamps = [event["timestamp_ns"] for event in events]

    def events_of(event_type: str) -> list[dict]:
        return [event for event in events if event["event_type"] == event_type]

    guest_begin = events_of("guest_swap_begin")
    guest_end = events_of("guest_swap_end")
    guest_legacy = events_of("guest_swap_legacy")
    issue_end = events_of("issue_swap_end")
    gpu_submits = events_of("gpu_submit")
    gpu_completions = events_of("gpu_complete")
    presentations = [
        event
        for event in events_of("present_enqueue")
        if event["state"] in {"success", "suboptimal"}
    ]
    pipeline_lookups = events_of("pipeline_lookup")
    pipeline_compiles = events_of("pipeline_compile")

    issue_ms = [
        event["duration_ns"] / 1_000_000
        for event in issue_end
        if event["duration_ns"] is not None
    ]
    pipeline_ms = [
        event["duration_ns"] / 1_000_000
        for event in pipeline_compiles
        if event["duration_ns"] is not None
    ]
    present_ms = [
        event["duration_ns"] / 1_000_000
        for event in presentations
        if event["duration_ns"] is not None
    ]

    submitted_frames = {
        event["guest_frame_id"]
        for event in gpu_submits
        if event["guest_frame_id"] is not None
    }
    completed_frames = {
        event["guest_frame_id"]
        for event in gpu_completions
        if event["guest_frame_id"] is not None and event["state"] != "duplicate"
    }
    presented_frame_ids = [
        event["guest_frame_id"]
        for event in presentations
        if event["guest_frame_id"] is not None
    ]
    unique_presented_frames = set(presented_frame_ids)
    guest_timestamps = {
        event["guest_frame_id"]: event["timestamp_ns"]
        for event in guest_begin + guest_legacy
        if event["guest_frame_id"] is not None
    }
    submissions_by_id = {
        event["correlation_id"]: event
        for event in gpu_submits
        if event["correlation_id"] is not None
    }
    submission_completion_ms = []
    guest_completion_ms = []
    for event in gpu_completions:
        if event["state"] == "duplicate":
            continue
        submission = submissions_by_id.get(event["correlation_id"])
        if submission and event["timestamp_ns"] >= submission["timestamp_ns"]:
            submission_completion_ms.append(
                (event["timestamp_ns"] - submission["timestamp_ns"]) / 1_000_000
            )
        guest_timestamp = guest_timestamps.get(event["guest_frame_id"])
        if guest_timestamp is not None and event["timestamp_ns"] >= guest_timestamp:
            guest_completion_ms.append(
                (event["timestamp_ns"] - guest_timestamp) / 1_000_000
            )

    first_present_by_frame = {}
    for event in presentations:
        frame_id = event["guest_frame_id"]
        if frame_id is not None and frame_id not in first_present_by_frame:
            first_present_by_frame[frame_id] = event
    guest_present_ms = [
        (event["timestamp_ns"] - guest_timestamps[frame_id]) / 1_000_000
        for frame_id, event in first_present_by_frame.items()
        if frame_id in guest_timestamps
        and event["timestamp_ns"] >= guest_timestamps[frame_id]
    ]

    return {
        "schema_version": 1,
        "source": path.name,
        "hitch_threshold_ms": hitch_ms,
        "capture": {
            "event_count": len(events),
            "duration_seconds": (
                (timestamps[-1] - timestamps[0]) / 1_000_000_000
                if len(timestamps) >= 2
                else 0.0
            ),
            "dropped_events": int(metadata.get("dropped_events", "0")),
            "ignored_events": int(metadata.get("ignored_events", "0")),
            "complete": metadata.get("capture_complete") == "true",
            "completion_reason": metadata.get("completion_reason", "unknown"),
        },
        "guest_swaps": {
            "begin_count": len(guest_begin),
            "end_count": len(guest_end),
            "legacy_count": len(guest_legacy),
            "duration_unavailable": sum(
                event["duration_ns"] is None for event in guest_end
            )
            + len(guest_legacy),
        },
        "issue_swap_cpu": {
            **_duration_summary(issue_ms, hitch_ms),
            "duration_unavailable": sum(
                event["duration_ns"] is None for event in issue_end
            ),
            "skipped": sum(event["state"] == "skipped" for event in issue_end),
            "failed": sum(event["state"] == "failure" for event in issue_end),
        },
        "gpu": {
            "submission_count": len(gpu_submits),
            "completed_event_count": len(gpu_completions),
            "unique_completed_guest_frames": len(completed_frames),
            "duplicate_completion_count": sum(
                event["state"] == "duplicate" for event in gpu_completions
            ),
            "submitted_without_observed_completion": len(
                submitted_frames - completed_frames
            ),
            "unlinked_submission_count": sum(
                event["guest_frame_id"] is None for event in gpu_submits
            ),
            "submission_to_observed_completion": _duration_summary(
                submission_completion_ms, hitch_ms
            ),
            "guest_swap_to_observed_completion": _duration_summary(
                guest_completion_ms, hitch_ms
            ),
        },
        "presentation_enqueue": {
            "successful_or_suboptimal_count": len(presentations),
            "unique_guest_frames": len(unique_presented_frames),
            "repeated_guest_frame_count": len(presented_frame_ids)
            - len(unique_presented_frames),
            "unlinked_count": sum(
                event["guest_frame_id"] is None for event in presentations
            ),
            "failed_count": sum(
                event["state"] == "failure"
                for event in events_of("present_enqueue")
            ),
            "call_duration": _duration_summary(present_ms, hitch_ms),
            "duration_unavailable": sum(
                event["duration_ns"] is None for event in presentations
            ),
            "guest_swap_to_first_enqueue": _duration_summary(
                guest_present_ms, hitch_ms
            ),
        },
        "pipelines": {
            "lookup_count": len(pipeline_lookups),
            "cache_hits": sum(
                event["state"] == "cache_hit" for event in pipeline_lookups
            ),
            "cache_misses": sum(
                event["state"] == "cache_miss" for event in pipeline_lookups
            ),
            "cache_hit_rate": (
                sum(event["state"] == "cache_hit" for event in pipeline_lookups)
                / len(pipeline_lookups)
                if pipeline_lookups
                else None
            ),
            "compile_duration": _duration_summary(pipeline_ms, hitch_ms),
            "compile_duration_unavailable": sum(
                event["duration_ns"] is None for event in pipeline_compiles
            ),
            "compile_failures": sum(
                event["state"] == "failure" for event in pipeline_compiles
            ),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument(
        "--hitch-ms",
        type=float,
        default=16.67,
        help="Duration threshold used for hitch counts (default: 16.67).",
    )
    parser.add_argument("--output", type=Path, help="Write JSON here instead of stdout.")
    arguments = parser.parse_args()
    if arguments.hitch_ms <= 0:
        parser.error("--hitch-ms must be positive")
    payload = json.dumps(
        summarize(arguments.trace, arguments.hitch_ms),
        indent=2,
        sort_keys=True,
    ) + "\n"
    if arguments.output:
        arguments.output.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")


if __name__ == "__main__":
    main()
