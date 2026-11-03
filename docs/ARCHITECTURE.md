# Architecture and patch map

GRFS-Recomp executes statically translated Xbox 360 PowerPC game code on ARM64 through ReXGlue. The Xenos backend translates guest GPU work to Vulkan; MoltenVK maps Vulkan to Metal. The launcher is native AppKit. This is not a native Metal renderer and the Steam/PC executable is not used.

| Layer | Source entry points | Responsibility |
| --- | --- | --- |
| Installer / launcher | `launcher/Launcher.swift`, `main.swift`, `LauncherTheme.swift` | Local ISO validation/extraction, launch arguments, cache paths, UI |
| Updates / restart | `launcher/Updater.swift`, `UpdateHelper.swift`, `RestartRequest.swift` | Signed download verification, app replacement, explicit restart tokens |
| Host game integration | `game-src/grfs_app.h`, `grfs_pc_options.h`, `grfs_frame_counter.h`, `grfs_output_size.h` | F5 options, keyboard/mouse, preferences, guest swap measurement and trace IDs |
| Presentation adapters | `launcher/GameTitle.m`, `FullscreenDiagnostics.m`, `PerformanceHUD.mm`, `FrameTrace.*`, `ShaderPreparation.swift` | macOS title/icon/fullscreen integration, bounded frame trace, HUD and preparation panel |
| ReXGlue runtime | Release SDK source archives and `patches/` | Kernel/filesystem/input shims, SDL window and Vulkan presentation |
| Xenos graphics | Release SDK `src/graphics/` plus patches below | Texture fetch translation, shader modules, EDRAM, render targets, pipelines |
| Game implementation | Generated locally; excluded from Git | Translated default image and AICLASS module, derived from the user's game |

## Graphics modifications

This is a navigation map, not an automatically applicable patch series. Consult the release source archive and [BUILDING.md](BUILDING.md) before rebuilding.

| Patch / helper | Area and behavior |
| --- | --- |
| `macos-texture-exp-adjust.patch` | Corrects texture result exponent decoding in SPIR-V fetch translation |
| `macos-spirv-optimizer.patch` | Runs SPIRV-Tools optimization with structural validation and original-module fallback; validation does not prove visual equivalence |
| `macos-three-shader-flow.patch` | Simplifies control flow for three exact pixel-shader hash/modification pairs; not a global material replacement |
| `macos-independent-clear-pair.patch`, `ClearPairGuard.h` | Groups eligible independent depth/color clears while retaining fallback and dependency checks |
| `macos-vulkan-persistent-cache.patch` | Persistent shader/pipeline storage handling |
| `macos-pipeline-preload-bypass.patch` | Earlier on-demand pipeline route; superseded by the catalog implementation for v12 |
| `macos-catalog-prewarm.patch` | v12 local catalog merge, startup preparation and compatible private driver cache |
| `macos-frame-correlation-trace.patch` | Optional guest-frame, Vulkan submission/fence, present-enqueue and pipeline timing correlation; no-op without the injected trace ABI |
| `macos-metal-swapchain.patch` | macOS swapchain replacement ordering for native fullscreen |
| `macos-output-resolution.patch` | Presentation output cap; not internal Xenos render-target scaling |
| `macos-presentation-limit-*.patch`, `PresentLimit.h` | Host presentation pacing; does not unlock game logic |
| `macos-eager-drawable.patch` | Experimental presentation path; presence in the tree does not mean it is enabled by the launcher |
| `xgetlanguage-user-preference.patch`, `grfs-native-options.patch` | Runtime language selection and game-side options hook |

Mip0 is a sampler workaround that avoids observed seams; correct dynamic mipmaps remain unresolved. Unclipped vertex processing on the CPU is used for draw extent estimation, reducing oversized EDRAM transfers. It is not a general software renderer. Shader compilation and queue submission remain synchronous in the published setup.

## Frame correlation trace

The trace is disabled unless the launcher receives `GRFS_FRAME_TRACE=1`. The
launcher replaces that flag with a private CSV path under
`~/Library/Application Support/GRFS Recompiled/Diagnostics/FrameTrace/`.
`FrameTrace.cpp` keeps a duration- and capacity-bounded event list in memory,
uses monotonic timestamps and guest IDs, redacts pipeline keys with a per-run
salt, and writes the CSV atomically with mode `0600`. Normal process shutdown
flushes a partial capture; reaching the configured duration or capacity flushes
the bounded capture early.

The rebuilt game hook allocates an ID before calling the original `VdSwap` and
records its CPU duration afterward. The zero-argument `GRFSRecordGuestSwap`
symbol remains available for released game binaries, but those samples are
marked `legacy` and have no guest-swap duration. The ReXGlue patch resolves the
trace ABI with `dlsym`, so a rebuilt runtime remains functional when the
injected trace library is absent or tracing is off.

The patched Vulkan path dequeues guest IDs in `IssueSwap`, associates them with
command submissions, and records completion when the corresponding fence is
observed. A frame correlation ID travels through the presenter mailbox, while
the guest-output image version identifies repeated host presentation of the
same image. Pipeline lookup and creation events share the same timeline; their
descriptor keys are redacted only inside the host trace core and never written
raw.

A successful `present_enqueue` means `vkQueuePresentKHR` accepted or
suboptimally accepted the request. It is not proof of display scanout. A
`gpu_complete` event proves completion of the traced command submission, not
that the window server displayed the image. Report distinct correlated IDs and
repeats with these limits rather than calling either event displayed FPS.

## Data and measurement boundaries

Persistent user data lives outside the app at `~/Library/Application Support/GRFS Recompiled/`. `Game/` contains extracted assets and `UserData/` contains saves/settings. Cache namespaces vary by release: v11 uses `CacheCombinedFlowV1`; v12 uses `Diagnostics/CacheCatalogV1`. Preserve old namespaces when comparing versions.

XSH storage contains guest shader programs; pipeline descriptions contain combinations of shaders and render state. A native driver cache is hardware/driver dependent. Neither a shader inventory nor a completed startup progress bar proves that every pipeline needed by every mission is prepared. Do not distribute these game-derived caches in contribution reports.

Metal HUD reports host presentations and GPU timing. The custom counter reports guest swap submissions. Repeated presentation of an old image can produce a high Metal FPS reading during a game stall. The correlation trace can distinguish guest IDs, observed GPU completions and repeated present enqueues, but it does not observe display scanout. See [TESTING.md](TESTING.md).
