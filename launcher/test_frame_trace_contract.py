#!/usr/bin/env python3
"""Keep the injected-host and ReXGlue frame-trace ABI declarations aligned."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = (ROOT / "launcher/FrameTrace.h").read_text(encoding="utf-8")
HUD = (ROOT / "launcher/PerformanceHUD.mm").read_text(encoding="utf-8")
PATCH = (ROOT / "patches/macos-frame-correlation-trace.patch").read_text(
    encoding="utf-8"
)

EVENTS = {
    "kIssueSwapBegin": 10,
    "kIssueSwapEnd": 11,
    "kGpuSubmit": 12,
    "kGpuComplete": 13,
    "kGuestOutputReady": 14,
    "kPresentEnqueue": 15,
    "kPipelineLookup": 20,
    "kPipelineCompile": 21,
}
STATES = {
    "kUnavailable": 0,
    "kSuccess": 1,
    "kFailure": 2,
    "kSkipped": 3,
    "kCacheHit": 4,
    "kCacheMiss": 5,
    "kDuplicate": 6,
    "kLegacy": 7,
    "kSuboptimal": 8,
}

for name, value in EVENTS.items():
    declaration = f"{name} = {value}"
    assert declaration in HOST, f"Host event ABI missing {declaration}"
    assert f"+  {declaration}" in PATCH, f"SDK event ABI missing {declaration}"
for name, value in STATES.items():
    declaration = f"{name} = {value}"
    assert declaration in HOST, f"Host state ABI missing {declaration}"
    assert f"+  {declaration}" in PATCH, f"SDK state ABI missing {declaration}"

for symbol in (
    "GRFSTraceEnabled",
    "GRFSTraceAcquireGuestFrame",
    "GRFSTraceRecordEvent",
):
    assert symbol in HUD, f"Injected shim does not export {symbol}"
    assert f'"{symbol}"' in PATCH, f"SDK patch does not resolve {symbol}"

for path in (
    "include/rex/perf/grfs_frame_trace.h",
    "include/rex/ui/presenter.h",
    "src/ui/presenter.cpp",
    "src/graphics/vulkan/command_processor.cpp",
    "src/graphics/vulkan/pipeline_cache.cpp",
    "src/ui/vulkan/vulkan_presenter.cpp",
):
    assert f"--- a/{path}" in PATCH, f"SDK patch does not cover {path}"

print("PASS: host and ReXGlue frame trace ABI contract")
