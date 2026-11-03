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
| AS-PERF-001 | P0 | instrumentation | Count unique completed game images and correlate guest submit, Vulkan submit, drawable present, and GPU completion timestamps | [Issue #3](https://github.com/alex-stampede/GRFS-Recomp/issues/3) |
| AS-PERF-002 | P0 | instrumentation | Produce a pipeline/shader hitch timeline with cache-hit state and compile duration | AS-PERF-001; issue #3 |
| AS-PERF-003 | P1 | idea | Evaluate bounded pipeline preparation concurrency that respects thermal and memory pressure | AS-PERF-002 and visual correctness |
| AS-PERF-004 | P1 | idea | Audit unified-memory copies, staging allocations, and CPU/GPU synchronization | Instruments evidence; issue #3 |
| AS-PERF-005 | P1 | idea | Profile CPU draw-extent estimation and EDRAM transfer volume before changing either path | AS-PERF-001; issue #2 |
| AS-PERF-006 | P1 | idea | Improve frame pacing without treating repeated presentation as new game frames | AS-PERF-001 |
| AS-PERF-007 | P1 | idea | Define an 8 GiB memory-pressure and cache-budget policy | M3 8 GiB reproduction |
| AS-PERF-008 | P2 | idea | Profile translated CPU work, runtime locks, and public QoS choices across Apple performance/efficiency cores | Time Profiler evidence |
| AS-FEAT-001 | P2 | idea | Add true internal render-resolution scaling, distinct from the current presentation cap | Correct render-target and UI behavior |
| AS-FEAT-002 | P3 | idea | Feasibility spike for an optional spatial upscaler at presentation | AS-FEAT-001; Vulkan/Metal interop design |
| AS-FEAT-003 | P2 | idea | Add a privacy-reviewed performance report exporter with machine-readable samples | AS-PERF-001 and AS-PERF-002 |

## Experiment outlines

### AS-PERF-001: end-to-end frame correlation

Problem: the Metal HUD can count repeated presentations while the guest is
stalled, and the guest swap counter does not prove that a new image completed on
the GPU.

Experiment:

1. Assign a monotonic frame/correlation ID at guest swap.
2. Carry it through command submission, drawable presentation, and completion
   callbacks where the current architecture permits.
3. Emit low-overhead signposts or a bounded in-memory ring buffer.
4. Report dropped, repeated, and completed IDs separately.

Success means the project can explain a hitch without calling repeated output a
high frame rate. Instrumentation overhead must be measured and switchable.

### AS-PERF-002: pipeline hitch attribution

Record shader module lookup/creation, pipeline descriptor identity, portable
cache hit, native driver-cache state, compile start/end, and the correlated
frame. Keep game-derived keys local and export only redacted hashes and timing
summaries.

Use the timeline to distinguish:

- startup catalog preparation;
- first encounter with a new shader or render-state combination;
- repeat pipeline creation that should have been reused;
- queue or resource stalls unrelated to compilation.

Do not turn every pipeline compile into a performance bug until repeat creation
or frame impact is demonstrated.

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
