# Apple Silicon performance and feature ideas

Last reviewed: 2026-11-03

This is a hypothesis backlog for Apple Silicon work. An entry is not a claim of
a bottleneck, a measured speedup, or rendering correctness. Upstream
[issue #3](https://github.com/alex-stampede/GRFS-Recomp/issues/3) defines the
performance evidence standard, while
[issue #2](https://github.com/alex-stampede/GRFS-Recomp/issues/2) is a mandatory
graphics-correctness gate.

## Guardrails

- Measure startup preparation, first-use hitches, and sustained frame cost
  separately.
- Name every counter. Host presentations, guest swap requests, and unique
  completed game images are different quantities.
- Compare the same checkpoint, camera, vision mode, output cap, cache state,
  display mode, power state, and thermal conditions.
- Prefer A/B/A runs and raw frame intervals. Report mean, p95, p99, hitch count,
  hitch duration, sample count, and capture duration where available.
- A gain caused by missing effects, repeated old frames, corrupted geometry,
  reduced workload, or save/input regressions is a failure.
- Use public Apple APIs and reversible feature flags. Do not depend on private
  scheduling, driver, or signing interfaces.

## Status vocabulary

- `idea`: plausible direction without sufficient profiling.
- `instrumentation`: measurement work, not a speedup.
- `experiment`: bounded implementation with a defined rollback.
- `validated`: repeated comparable evidence and correctness checks exist.
- `blocked`: a named dependency prevents a useful experiment.
- `rejected`: evidence showed no value or unacceptable regressions.

## Backlog

| ID | Priority | Status | Idea | Dependency / upstream path |
| --- | --- | --- | --- | --- |
| AS-PERF-001 | P0 | instrumentation | Local v1 correlates guest swaps, Vulkan submissions/fences and present enqueue; display scanout remains unavailable | [Issue #3](https://github.com/alex-stampede/GRFS-Recomp/issues/3) |
| AS-PERF-002 | P0 | instrumentation | Local v1 records redacted pipeline lookup and compile duration on the frame timeline | AS-PERF-001; issue #3 |
| AS-PERF-003 | P1 | idea | Evaluate bounded pipeline preparation concurrency that respects thermal and memory pressure | AS-PERF-002 and visual correctness |
| AS-PERF-004 | P1 | idea | Audit unified-memory copies, staging allocations, and CPU/GPU synchronization | Instruments evidence; issue #3 |
| AS-PERF-005 | P1 | idea | Profile CPU draw-extent estimation and EDRAM transfer volume before changing either path | AS-PERF-001; issue #2 |
| AS-PERF-006 | P1 | idea | Improve frame pacing without treating repeated presentation as new game frames | AS-PERF-001 |
| AS-PERF-007 | P1 | idea | Define an 8 GiB memory-pressure and cache-budget policy | M3 8 GiB reproduction |
| AS-PERF-008 | P2 | idea | Profile translated CPU work, runtime locks, and public QoS choices across Apple performance/efficiency cores | Time Profiler evidence |
| AS-PERF-009 | P1 | rejected | The release baseline already records the swapchain format after output-pipeline creation; no GRFS patch is needed | Audited against the public SDK source |
| AS-PERF-010 | P1 | experiment | Compare synchronous and asynchronous MoltenVK queue submission without changing other presentation behavior | AS-PERF-001 and AS-PERF-009 |
| AS-PERF-011 | P1 | idea | Profile POSIX multi-wait and timer polling before selecting an event-driven implementation | Time Profiler; ReXGlue PR #501 |
| AS-PERF-012 | P2 | blocked | Evaluate safe guest-upload hoisting from ReXGlue PRs #535 and #536 | AS-PERF-001; issue #2 correctness baseline |
| AS-PERF-013 | P1 | idea | Audit the pipeline-cache writer for the pending-flush behavior from ReXGlue PR #529 | Existing GRFS persistent-cache patch |
| AS-PERF-014 | P3 | blocked | Test opt-in pixel-shader FMA contraction from ReXGlue PR #540 | Stable issue #2 reproduction and image comparison |
| AS-FEAT-001 | P2 | idea | Add true internal render-resolution scaling, distinct from the current presentation cap | Correct render-target and UI behavior |
| AS-FEAT-002 | P3 | idea | Feasibility spike for an optional spatial upscaler at presentation | AS-FEAT-001; Vulkan/Metal interop design |
| AS-FEAT-003 | P2 | idea | Add a privacy-reviewed performance report exporter with machine-readable samples | AS-PERF-001 and AS-PERF-002 |

## Research project 1: ReXGlue Apple Silicon survey

Research snapshot: 2026-11-03. Measurements below are author-reported results
from the cited project or pull request, not independently reproduced GRFS
results. Canonical ReXGlue was reviewed at
[`c94f5eb`](https://github.com/rexglue/rexglue-sdk/commit/c94f5ebdcb3c9d1a460ca48e04f9758448f8d518).

### Recent projects reviewed

| Project | Reusable evidence | Decision for GRFS |
| --- | --- | --- |
| [Ridge Racer 6 Recompilation](https://github.com/Sirhalo23/RidgeRacer6-Recompilation) ([snapshot](https://github.com/Sirhalo23/RidgeRacer6-Recompilation/commit/50d1c9d3268eff1ada34a413b026697a02331e0a)) | M4 Max testing reported synchronous vs. asynchronous IssueSwap CPU mean of 4.774 vs. 0.029 ms and p95 of 5.983 vs. 0.047 ms, while explicitly rejecting an overall-FPS interpretation. It also supplies a bounded CSV, percentile summarizer, presenter-pipeline reuse fix, persistent cache checks, and timer evidence ([results](https://github.com/Sirhalo23/RidgeRacer6-Recompilation/blob/50d1c9d3268eff1ada34a413b026697a02331e0a/tools/macos-performance-results.md#L38-L96), [summarizer](https://github.com/Sirhalo23/RidgeRacer6-Recompilation/blob/50d1c9d3268eff1ada34a413b026697a02331e0a/tools/perf_summary.py#L14-L91)). | Primary Vulkan/MoltenVK reference. Adapt tracing concepts and selected fixes, not its open/conflicting SDK branch. BSD-3-Clause. |
| [GoldenEye Metal](https://github.com/ysrdevs/goldeneye-metal) ([snapshot](https://github.com/ysrdevs/goldeneye-metal/commit/d9ea7daf295b6d3ea1eed33ff789a9bdf534bfe3)) | Carries monotonic guest-frame IDs into GPU completion, rejects duplicate completion IDs, and records compile/cache events in bounded reports ([interface](https://github.com/ysrdevs/goldeneye-metal/blob/d9ea7daf295b6d3ea1eed33ff789a9bdf534bfe3/include/rex/perf/metal_performance.h#L8-L54), [implementation](https://github.com/ysrdevs/goldeneye-metal/blob/d9ea7daf295b6d3ea1eed33ff789a9bdf534bfe3/src/core/perf/metal_performance.cpp#L151-L255)). | Best correlation model. Reuse BSD-licensed telemetry concepts, not the native-Metal renderer or game integration. |
| [Pinyon Shift](https://github.com/arcanite24/pinyon-shift) ([snapshot](https://github.com/arcanite24/pinyon-shift/commit/9494adadef6a73b340d204f29cfa75d711b78c6a)) | Provides deterministic capture routes, distinct-presentation checks, p95/p99/hitch reporting, memory-pressure evidence, and an event-driven waiter in its BSD ShiftGlue baseline ([macOS evidence](https://github.com/arcanite24/pinyon-shift/blob/9494adadef6a73b340d204f29cfa75d711b78c6a/docs/MACOS.md#L188-L237), [wait implementation](https://github.com/arcanite24/shiftglue-sdk/blob/8e32080bfa4383b0bb7402a0f5289e4308de4383/src/core/threading_posix.cpp#L792-L845)). | Adapt qualification methodology. Its large wait/renderer gains were not measured on Apple Silicon, so profile GRFS before porting. BSD-3-Clause. |
| [ForzaRecomp](https://github.com/NerunSmarts/ForzaRecomp) ([snapshot](https://github.com/NerunSmarts/ForzaRecomp/commit/db6c4618110cf79fb3fbe6f0508bcc7ee00bfb93)) | A macOS alertable-wait microbenchmark reported one-core use falling from 100.04% to 1.30% by preserving fractional wait duration ([profiling](https://github.com/NerunSmarts/ForzaRecomp/blob/db6c4618110cf79fb3fbe6f0508bcc7ee00bfb93/docs/profiling.md#L95-L176), [patch](https://github.com/NerunSmarts/ForzaRecomp/blob/db6c4618110cf79fb3fbe6f0508bcc7ee00bfb93/patches/0016-preserve-fractional-multi-wait-sleeps.patch#L1-L16)). | Evidence for profiling wait loops, not a whole-game gain. The project has no project-level license; do not copy code without permission. |
| [Edge of Time Recomp](https://github.com/goliathret/EdgeOfTimeRecomp) ([snapshot](https://github.com/goliathret/EdgeOfTimeRecomp/commit/ebf7bb904d88062a3bd5b1d59ea642cdc2c1b780)) | Implements bounded traces and a three-lane, deduplicated PSO worker system sized from physical cores ([PSO workers](https://github.com/goliathret/EdgeOfTimeRecomp/blob/ebf7bb904d88062a3bd5b1d59ea642cdc2c1b780/src/gpu/pipeline/pso_precache.cpp#L37-L270), [trace](https://github.com/goliathret/EdgeOfTimeRecomp/blob/ebf7bb904d88062a3bd5b1d59ea642cdc2c1b780/src/gpu/trace.cpp#L16-L150)). | Design reference for AS-PERF-003 only. It has no verified Apple performance comparison, and its title-specific predictor overlaps GRFS catalog preparation. BSD-3-Clause. |
| [Forza Horizon 1 ReImagined](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined) ([snapshot](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined/commit/287804efe13637a266439203b9eb232a3b769809)) | Its M4 Pro MetalFX/FSR runs did not establish a general advantage; more useful are negative results where a memory-tracking backport corrupted UI and a pixel-NaN trial regressed 28.9 to 27.6 FPS ([macOS report](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined/blob/287804efe13637a266439203b9eb232a3b769809/docs/macos.md#L288-L348), [rejected trials](https://github.com/FH1-ReImagined/ForzaHorizon1-ReImagined/blob/287804efe13637a266439203b9eb232a3b769809/docs/macos.md#L550-L564)). | Negative evidence against blind backports and MetalFX as the first project. Current code is GPL-3.0-or-later; use as design evidence unless GPL obligations are accepted. |
| [WET macOS add-on](https://github.com/kylebailey9/wet-recomp-macos) ([snapshot](https://github.com/kylebailey9/wet-recomp-macos/commit/2c10b5ad4dc11af3f7de0da09a78c03dbd018f69)) | Supplies ARM64 packaging and MoltenVK staging, but no performance implementation or benchmark ([README](https://github.com/kylebailey9/wet-recomp-macos/blob/2c10b5ad4dc11af3f7de0da09a78c03dbd018f69/README.md#L1-L28)). | Build-reference only; no PERFIDEAS candidate. MIT add-on. |

### Canonical ReXGlue pull requests

Statuses were verified on 2026-11-03. Open PRs are not released SDK behavior.

| PR | Status and evidence | GRFS decision |
| --- | --- | --- |
| [#261: Tracy and performance counters](https://github.com/rexglue/rexglue-sdk/pull/261) | Merged 2026-03-27. Adds optional Tracy, CSV counters, HUD support, and frame/GPU/audio/cache counters ([counter API](https://github.com/rexglue/rexglue-sdk/blob/a0aa766bb92fb05672a93b023f0e915980bc3cbc/include/rex/perf/counter.h#L16-L184)). | Instrumentation baseline, not a speedup. Extend it with correlation rather than adding another independent FPS counter. |
| [#501: event-driven POSIX `WaitMultiple`](https://github.com/rexglue/rexglue-sdk/pull/501) | Open. Replaces 1 ms polling with an epoch/condition variable; Linux author results report roughly 71-81% to 1.6-3.1% audio-worker CPU, with FPS differences characterized as noise ([implementation](https://github.com/Dyotson/rexglue-sdk/blob/d79d3ac8bdfdf21a7a49cf830a758e8d160144c5/src/core/threading_posix.cpp#L270-L459)). | Profile GRFS first; no macOS result exists for this exact implementation. |
| [#529: preserve macOS shader/pipeline storage](https://github.com/rexglue/rexglue-sdk/pull/529) | Open. Rewinds append-update streams and prevents the writer sleeping with a flush pending ([filesystem](https://github.com/wolfiesch/rexglue-sdk/blob/d5c1757ee648e4f86a73ce991aec5a9d6f1fd47d/src/core/filesystem_posix.cpp#L126-L134), [writer](https://github.com/wolfiesch/rexglue-sdk/blob/d5c1757ee648e4f86a73ce991aec5a9d6f1fd47d/src/graphics/vulkan/pipeline_cache.cpp#L3657-L3677)). | GRFS already has the rewind half. Audit only the pending-flush behavior. |
| [#535: ownership-transfer render-pass distinction](https://github.com/rexglue/rexglue-sdk/pull/535) and [#536: upload hoisting](https://github.com/rexglue/rexglue-sdk/pull/536) | Both open. #535 is a correctness prerequisite. #536 reports M1 Max/Tekken 6 changing from about 125 to 56 render passes/frame and 21 to 60 frames shown/s in one 90-second run ([overlap tracking](https://github.com/datamothra/rexglue-sdk/blob/90bb72c13f8813ea29a080a4fa4143cb4d75b50b/src/graphics/vulkan/shared_memory.cpp#L276-L337), [barriers](https://github.com/datamothra/rexglue-sdk/blob/90bb72c13f8813ea29a080a4fa4143cb4d75b50b/src/graphics/vulkan/deferred_command_buffer.cpp#L38-L92)). | Strongest Apple GPU-side candidate, but invasive and title-specific. Defer until tracing and issue #2 correctness gates exist. |
| [#540: optional pixel-shader FMA contraction](https://github.com/rexglue/rexglue-sdk/pull/540) | Open. M4/Skate 1 author results include 65 to 13.6 ms fragment time and a 15.0 to 31.5 FPS recheck ([translation](https://github.com/im-tyler/rexglue-sdk/blob/3c19cb99bf18d2ffc04f6e17ea70d3b3b1c7fee8/src/graphics/pipeline/shader/spirv_translator.cpp#L182-L188), [cache key](https://github.com/im-tyler/rexglue-sdk/blob/3c19cb99bf18d2ffc04f6e17ea70d3b3b1c7fee8/src/graphics/vulkan/pipeline_cache.cpp#L963-L975)). | Highest numerical-correctness risk. Defer behind stable material/NVG reproduction and fixed-camera image comparison. |
| [#546: PPC `sync`/`lwsync` host fences](https://github.com/rexglue/rexglue-sdk/pull/546) | Open. Emits host fences and adds focused codegen tests ([implementation](https://github.com/Hanz0Hatt0r1/rexglue-sdk/blob/923a7a03575b43f6c22ee97c376077a7022d17d6/src/codegen/builders/system.cpp#L33-L43)). | ARM64 correctness, not performance. It requires regenerated game code and is blocked by the clean-build gap. |

### Research decision

The first project is **Frame/Pipeline Correlation Trace v1**, implemented through
AS-PERF-001 and AS-PERF-002. It fills the exact measurement gap already
documented by GRFS and is a prerequisite for interpreting most optimization
claims.

Local implementation status: the bounded C++ core, backward-compatible guest
ABI, private CSV export, deterministic tests, summarizer and matching ReXGlue
patch are implemented on `perf/frame-pipeline-trace-v1`. Host tests and patch
application pass. End-to-end gameplay evidence is still unavailable because the
public tree cannot yet rebuild the generated game and all modified runtime
components from a clean checkout.

Ranked follow-up sequence:

1. Compare MoltenVK queue submission modes without combining that variable with
   presenter changes (AS-PERF-010).
2. Audit PR #529's pending-flush condition (AS-PERF-013).
3. Profile wait/polling CPU before choosing PR #501 or another waiter model
   (AS-PERF-011).
4. Evaluate PRs #535/#536 only with trace and rendering-correctness gates
   (AS-PERF-012).
5. Keep PR #540 default-off and last (AS-PERF-014).

Already-present or rejected directions:

- GRFS already has catalog preparation, native driver-cache persistence, a
  guest-swap counter, and a Metal HUD. Add correlation and missing integrity
  behavior rather than duplicate them.
- The public release baseline already assigns
  `swapchain_effect_pipeline.swapchain_format` after creating the presenter
  output pipeline. AS-PERF-009 is not an applicable GRFS fix.
- Edge of Time's fixed synchronous queue setting matches GRFS; Ridge's opposite
  result makes queue mode an A/B variable, not a universal replacement.
- Do not treat WET packaging, native-Metal rewrites, or MetalFX as evidence of a
  GRFS speedup.
- Do not port unlicensed ForzaRecomp code or GPL ReImagined code without
  permission and compatible licensing.

## Experiment outlines

### AS-PERF-001: end-to-end frame correlation

Problem: the Metal HUD can count repeated presentations while the guest is
stalled, and the guest swap counter does not prove that a new image completed on
the GPU.

Research basis: Ridge demonstrates a lightweight Release CSV and explicit
guest/display caveats. GoldenEye demonstrates carrying a producing guest ID into
asynchronous GPU completion and rejecting duplicate completion samples.

Experiment:

1. Assign a monotonic frame/correlation ID at guest swap.
2. Record IssueSwap begin/end and associate command-buffer/submission IDs.
3. Carry the ID through drawable acquisition, present request, and fence or GPU
   completion observation where MoltenVK exposes one.
4. Record duplicate, dropped, completed, and missing-completion states
   separately.
5. Store events in a bounded in-memory ring and optionally export a Release CSV.
   Avoid mandatory Tracy and per-draw atomics.
6. Compare trace-off and trace-on runs so instrumentation overhead is explicit.

Success means the project can explain a hitch without calling repeated output a
high frame rate. Missing completion data must remain `unavailable`, never a
zero-duration sample. Instrumentation overhead must be measured and switchable.

Local v1 result: guest IDs are allocated before `VdSwap`, carried through
IssueSwap, command submission/fence observation and the presenter mailbox, and
reported separately from `vkQueuePresentKHR` acceptance. Capacity and duration
limits, explicit legacy/unavailable/duplicate states, atomic mode-`0600` export,
and trace-off no-op resolution are covered by host tests. Display scanout is not
observable, so the summary reports unique **present-enqueued** guest IDs rather
than displayed frames.

### AS-PERF-002: pipeline hitch attribution

Record shader module lookup/creation, pipeline descriptor identity, portable
cache hit, native driver-cache state, compile start/end, placeholder/skip result,
and the correlated frame. Keep game-derived keys local and export only redacted
hashes and timing summaries.

Add a Ridge-style summarizer for p50/p95/p99/max, hitches over 25/50/100 ms,
unique completed frames, compile-hitch count, cache state, and missing GPU
samples. Label cold application cache, warm application cache, and unchanged
macOS/Metal driver cache independently.

Use the timeline to distinguish:

- startup catalog preparation;
- first encounter with a new shader or render-state combination;
- repeat pipeline creation that should have been reused;
- queue or resource stalls unrelated to compilation.

Do not turn every pipeline compile into a performance bug until repeat creation
or frame impact is demonstrated.

Local v1 result: `EnsurePipelineCreated` records in-memory hit/miss state and
compile duration with per-run salted descriptor hashes. Events share the frame
timeline but intentionally have no guest ID because asynchronous compilation
may outlive its requesting frame. Shader-module creation and portable/native
cache-layer attribution remain follow-up instrumentation rather than inferred
states.

### AS-PERF-003: bounded preparation concurrency

Only after AS-PERF-002 identifies independent compile work, test a small,
configurable worker count. Compare serial, limited parallel, and current release
behavior on M3 8 GiB and M4 Pro-class systems.

Required checks:

- peak resident memory and memory-pressure events;
- total preparation time and p95/p99 gameplay hitch duration;
- thermal state and sustained gameplay after preparation;
- identical shader validation, pipeline keys, and rendered output;
- deterministic cancellation and serial fallback.

Unbounded async compilation or rendering without a required pipeline is out of
scope because missing effects are not an acceptable speedup.

### AS-PERF-004: Apple unified-memory traffic

Use Metal System Trace, Allocations/VM Tracker, and runtime counters to identify
actual duplicate uploads, readbacks, staging buffers, and synchronization.
Apple unified memory does not make all copies free: guest tiling, endian
conversion, format conversion, and resource lifetime rules may still require
them.

Candidate experiments must name the avoided copy or wait, its byte volume and
frequency, and the coherency/lifetime rule that keeps the change correct.

### AS-PERF-005: draw extents and EDRAM

The current CPU unclipped-vertex path reduces oversized EDRAM transfers. Profile
both its CPU cost and the transfer savings by draw category before replacing or
caching it.

Possible bounded experiments:

- cache only when shader, buffers, constants, and draw parameters prove the
  extent immutable;
- vectorize a measured hot calculation using portable code or ARM64 intrinsics
  behind an equivalent scalar reference;
- improve conservative bounds without clipping valid particles, shadows, NVG,
  or transition effects.

A smaller transfer with missing geometry fails the correctness gate.

### AS-PERF-006: presentation pacing

Measure guest production intervals, queue depth, drawable waits, display
refresh, and completion intervals together. Evaluate whether the 30/60/120
presentation limits reduce jitter or only repeat frames. Any pacing change must
retain native fullscreen, Game Mode, windowed mode, explicit restart behavior,
and uncapped fallback.

### AS-PERF-007: 8 GiB memory budget

Capture resident/physical footprint, compressed memory, swap, cache sizes, and
memory-pressure notifications during startup, Gunsmith, campaign normal/NVG,
Guerrilla, and scene transitions.

Potential outcomes include bounded in-memory catalogs, lazy metadata loading,
or evidence-based cache eviction. Never delete saves or require users to wipe
all caches. Native driver caches remain hardware/driver specific.

### AS-PERF-008: translated CPU scheduling

Use Time Profiler and thread-state traces to find runtime locks, busy waits, and
threads that miss deadlines. Test public QoS changes only after identifying the
responsibility and latency sensitivity of each thread. Do not hard-code a chip
model, force private core affinity, or starve OS/driver work.

### AS-PERF-009: presenter pipeline-format reuse

Ridge's presenter fix records the swapchain format after successfully creating
the cached output pipeline. Without that assignment, later paints can appear
incompatible, wait for GPU completion, destroy the pipeline, and recreate it.

The public ReXGlue source used by the GRFS release already contains the
assignment. This candidate is rejected as already satisfied; do not duplicate
the write. Pipeline instrumentation can still verify steady-state creation,
while fullscreen and resize behavior remain normal regression checks.

### AS-PERF-010: MoltenVK queue-mode A/B

GRFS currently forces synchronous queue submission. Ridge measured much lower
IssueSwap CPU with asynchronous submission, while Edge of Time deliberately
uses synchronous submission. This is workload-specific.

Expose a reversible launch option and compare only after AS-PERF-009 is held
constant. Measure unique completed frames, IssueSwap CPU, drawable waits, GPU
completion, queue depth, shutdown, fullscreen transitions, and visual
correctness. Do not combine the presenter and queue-mode changes in one result.

### AS-PERF-011: POSIX wait/polling audit

Use Time Profiler and thread-state/wakeup counts to identify hot `WaitMultiple`,
alertable-wait, or timer polling before selecting an implementation. If the
problem exists, compare canonical PR #501 with a localized per-thread waiter and
preserve wait-any, wait-all, timeout, auto-reset, callback, and shutdown
semantics. CPU reduction alone is useful but is not an FPS claim.

### AS-PERF-012: guest-upload hoisting

Treat PR #535 as a correctness prerequisite for PR #536. Log every hoist,
overlapping range, pending GPU write, barrier, and fallback. Keep a runtime-off
switch and test normal/NVG, particles, shadows, scene transitions, Gunsmith
materials, and Guerrilla. A reduction in render passes with stale or missing
geometry fails.

### AS-PERF-013: pending pipeline-cache flush

Do not duplicate GRFS's existing append-stream rewinds. Audit whether the writer
can sleep while a final flush is pending, then port only that condition if it is
missing. Also verify external synchronization across both
`vkGetPipelineCacheData` calls, `VK_INCOMPLETE`, bounded size, corruption
recovery, and explicit hard-exit flushing.

### AS-PERF-014: pixel-shader FMA contraction

Keep the experiment default-off with a distinct shader/pipeline cache key.
Preserve vertex and depth precision. Require fixed-camera image comparisons plus
manual Gunsmith, normal/NVG, particle, shadow, transition, and repeat-session
checks. Current material and geometry corruption must be reproducible before
last-bit numerical changes can be evaluated honestly.

## Feature ideas

### AS-FEAT-001: internal render-resolution scaling

The current output cap changes presentation size, not the Xenos render-target
workload. A true internal scale could reduce GPU work but must preserve aspect
ratio, UI readability, clears, EDRAM resolves, post-processing, screenshots,
mouse mapping, and fullscreen/window transitions. Begin with an architecture
proposal and render-target inventory rather than a global dimension override.

### AS-FEAT-002: optional spatial upscaling

After a correct internal-resolution path exists, investigate whether a
presentation-stage spatial upscaler can be integrated without a native Metal
renderer rewrite. Compare quality and cost against existing MoltenVK scaling.
Temporal upscaling is not implied because motion vectors, depth, jitter, and
history validity may not be available.

### AS-FEAT-003: reproducible report export

Export a small JSON/CSV bundle containing app/release identifiers, redacted
hardware/OS data, settings, cache-state labels, frame intervals, hitch events,
and counter definitions. Preview the exact files before export and exclude home
paths, tokens, assets, shaders, caches, saves, and original game data.

## Required validation matrix

At minimum, performance candidates should cover:

| Area | Required comparison |
| --- | --- |
| Gunsmith | Repeat openings and known PP19/PP2000 material cases |
| Campaign | Same checkpoint in normal and NVG views |
| Guerrilla | Stable section plus a scene transition |
| Cache state | Cold/preparation and warmed sustained runs reported separately |
| Display | Same resolution cap and fullscreen/windowed mode |
| Duration | Comparable 60-second stabilized captures where practical |
| Repetition | A/B/A when thermal or warm-cache effects may bias results |
| Functional | F5, restart, input, saves, continuation, and normal exit |

## Known invalid shortcuts

- Counting host presentations as unique game frames.
- Comparing a cold candidate with a warmed baseline.
- Disabling effects, mip behavior, clears, synchronization, or validation and
  calling the resulting number a speedup.
- Recommending cache deletion without evidence that cache contents caused the
  problem.
- Treating successful SPIR-V validation or pipeline creation as proof of correct
  pixels.
- Assuming lower presentation resolution reduced internal render-target work.
- Applying an open ReXGlue PR wholesale without checking its dependencies,
  branch age, license, and overlap with GRFS patches.
- Treating asynchronous MoltenVK submission as universally faster.
- Applying upload hoisting or FMA contraction before trace and graphics
  correctness gates exist.
