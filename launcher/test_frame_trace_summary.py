#!/usr/bin/env python3
"""Deterministic tests for the frame-trace summarizer."""

from pathlib import Path
import tempfile
import unittest

from summarize_frame_trace import summarize


HEADER = (
    "schema_version,event_sequence,timestamp_ns,guest_frame_id,event_type,"
    "correlation_id,duration_ns,redacted_key_hash,state\n"
)


class FrameTraceSummaryTests(unittest.TestCase):
    def write_trace(self, rows: list[str], header: str = HEADER) -> Path:
        directory = tempfile.TemporaryDirectory(prefix="grfs-trace-summary-")
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "trace.csv"
        path.write_text(
            "# grfs_frame_trace_schema=1\n"
            "# dropped_events=2\n"
            "# ignored_events=3\n"
            "# capture_complete=true\n"
            "# completion_reason=capacity\n"
            + header
            + "".join(rows),
            encoding="utf-8",
        )
        return path

    def test_correlated_summary_and_nearest_rank_percentiles(self):
        path = self.write_trace(
            [
                "1,1,100,1,guest_swap_begin,,,,success\n",
                "1,2,110,1,guest_swap_end,,10,,success\n",
                "1,3,120,1,issue_swap_end,9,1000000,,success\n",
                "1,4,130,1,gpu_submit,9,,,success\n",
                "1,5,140,1,gpu_complete,9,,,success\n",
                "1,6,150,1,gpu_complete,10,,,duplicate\n",
                "1,7,160,1,present_enqueue,22,2000000,,success\n",
                "1,8,170,1,present_enqueue,22,3000000,,success\n",
                "1,9,180,,pipeline_lookup,,,0x1,cache_miss\n",
                "1,10,190,,pipeline_compile,,10000000,0x1,success\n",
                "1,11,200,,pipeline_lookup,,,0x2,cache_hit\n",
                "1,12,210,,pipeline_compile,,20000000,0x2,failure\n",
            ]
        )
        result = summarize(path, hitch_ms=16.0)
        self.assertEqual(result["capture"]["dropped_events"], 2)
        self.assertEqual(result["capture"]["ignored_events"], 3)
        self.assertEqual(result["capture"]["completion_reason"], "capacity")
        self.assertEqual(result["gpu"]["unique_completed_guest_frames"], 1)
        self.assertEqual(result["gpu"]["duplicate_completion_count"], 1)
        self.assertEqual(
            result["gpu"]["submission_to_observed_completion"]["mean_ms"],
            0.00001,
        )
        self.assertEqual(
            result["presentation_enqueue"]["repeated_guest_frame_count"], 1
        )
        self.assertEqual(
            result["presentation_enqueue"]["call_duration"]["mean_ms"], 2.5
        )
        self.assertEqual(result["pipelines"]["cache_hits"], 1)
        self.assertEqual(result["pipelines"]["cache_misses"], 1)
        self.assertEqual(result["pipelines"]["cache_hit_rate"], 0.5)
        self.assertEqual(result["pipelines"]["compile_duration"]["p50_ms"], 10.0)
        self.assertEqual(result["pipelines"]["compile_duration"]["p95_ms"], 20.0)
        self.assertEqual(
            result["pipelines"]["compile_duration"][
                "hitches_at_or_above_threshold"
            ],
            1,
        )

    def test_missing_durations_remain_unavailable(self):
        path = self.write_trace(
            [
                "1,1,100,4,guest_swap_legacy,,,,legacy\n",
                "1,2,101,4,issue_swap_end,8,,,failure\n",
                "1,3,102,,pipeline_compile,,,0x1,failure\n",
                "1,4,103,,present_enqueue,7,,,success\n",
            ]
        )
        result = summarize(path)
        self.assertIsNone(result["issue_swap_cpu"]["mean_ms"])
        self.assertEqual(result["issue_swap_cpu"]["duration_unavailable"], 1)
        self.assertEqual(result["guest_swaps"]["duration_unavailable"], 1)
        self.assertEqual(result["pipelines"]["compile_duration_unavailable"], 1)
        self.assertEqual(result["presentation_enqueue"]["unlinked_count"], 1)

    def test_rejects_incomplete_schema(self):
        path = self.write_trace([], header="schema_version,event_sequence\n")
        with self.assertRaisesRegex(ValueError, "required schema"):
            summarize(path)

    def test_rejects_nonmonotonic_events(self):
        path = self.write_trace(
            [
                "1,2,100,1,guest_swap_begin,,,,success\n",
                "1,1,101,1,guest_swap_end,,1,,success\n",
            ]
        )
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            summarize(path)


if __name__ == "__main__":
    unittest.main()
