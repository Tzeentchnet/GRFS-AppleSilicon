#!/usr/bin/env python3
"""Run game-free host checks on macOS; all executable fixtures are temporary."""
from pathlib import Path
import os
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    if platform.system() != "Darwin":
        raise SystemExit("These host checks require macOS and Xcode command-line tools.")
    for tool in ("swiftc", "clang++"):
        if not shutil.which(tool):
            raise SystemExit(f"Missing {tool}; install Xcode command-line tools.")
    with tempfile.TemporaryDirectory(prefix="grfs-host-checks-") as temporary:
        temp = Path(temporary)
        cache = temp / "swift-cache"
        env = os.environ.copy()
        env.update(GRFS_TEST_LAUNCHER_BINARY=str(temp / "launcher"),
                   GRFS_TEST_UPDATE_HELPER=str(temp / "update-helper"),
                   GRFS_SWIFT_MODULE_CACHE=str(cache))
        env.pop("GRFS_FRAME_TRACE", None)
        env.pop("GRFS_FRAME_TRACE_SECONDS", None)
        env.pop("GRFS_FRAME_TRACE_CAPACITY", None)

        def run(*args):
            subprocess.run(list(map(str, args)), cwd=ROOT, env=env, check=True)

        launcher = ROOT / "launcher"
        sources = [launcher / name for name in (
            "main.swift", "Launcher.swift", "Updater.swift", "LauncherTheme.swift",
            "LegacyBackups.swift", "RestartRequest.swift", "ShaderPreparation.swift")]
        run("swiftc", *sources, "-framework", "AppKit", "-framework", "CryptoKit",
            "-module-cache-path", cache, "-o", env["GRFS_TEST_LAUNCHER_BINARY"])
        run(sys.executable, launcher / "test_iso.py")
        run("swiftc", launcher / "UpdateHelper.swift", "-framework", "AppKit",
            "-module-cache-path", cache, "-o", env["GRFS_TEST_UPDATE_HELPER"])
        run(sys.executable, launcher / "test_updates.py")
        restart = temp / "main.swift"
        restart.write_text((launcher / "test_restart.swift").read_text())
        run("swiftc", restart, launcher / "RestartRequest.swift",
            "-module-cache-path", cache, "-o", temp / "restart-check")
        run(temp / "restart-check")
        run("clang++", "-std=c++17", ROOT / "game-src/test_output_size.cpp",
            "-o", temp / "output-size-check")
        run(temp / "output-size-check")
        run("clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            launcher / "FrameTrace.cpp", launcher / "test_frame_trace.cpp",
            "-o", temp / "frame-trace-check")
        run(temp / "frame-trace-check")
        run(sys.executable, launcher / "test_frame_trace_summary.py")
        run(sys.executable, launcher / "test_frame_trace_contract.py")
        trace_library = temp / "libGRFSTrace.dylib"
        run("clang++", "-x", "objective-c++", "-std=c++17", "-dynamiclib",
            "-fobjc-arc", launcher / "PerformanceHUD.mm", launcher / "FrameTrace.cpp",
            "-framework", "QuartzCore", "-framework", "AppKit", "-o", trace_library)
        run("clang++", "-std=c++17", launcher / "test_frame_trace_abi.cpp",
            trace_library, "-o", temp / "frame-trace-abi-check")
        run(temp / "frame-trace-abi-check")
        trace_path = temp / "frame-trace.csv"
        env["GRFS_FRAME_TRACE"] = str(trace_path)
        run(temp / "frame-trace-abi-check", trace_path)
        env.pop("GRFS_FRAME_TRACE")
    print("PASS: game-free host checks. GPU/gameplay correctness was not tested.")


if __name__ == "__main__":
    main()
