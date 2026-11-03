# Upstream contribution tracker

Last reviewed: 2026-11-03

This file tracks work intended for
[alex-stampede/GRFS-Recomp](https://github.com/alex-stampede/GRFS-Recomp).
It is a fork-maintenance document, not an upstream claim that an issue is fixed
or a pull request has been accepted.

## Repository relationship

| Role | Repository / remote | State |
| --- | --- | --- |
| Upstream | `https://github.com/alex-stampede/GRFS-Recomp` / `upstream` | Fetch enabled; push intentionally disabled |
| Friendly fork | `https://github.com/Tzeentchnet/GRFS-AppleSilicon` / `origin` | Fetch and push enabled |
| Audited baseline | Upstream `main` at `1829d0b` | Reviewed on 2026-11-03 |
| Release baseline | `v0.10.0.12` | Published ZIP and checksum verified |

Keep local `main` suitable for fast-forwarding from upstream. Use focused topic
branches for contributions and check for a matching upstream issue or pull
request before filing anything.

## Status vocabulary

- `idea`: worth investigating, but evidence or scope is incomplete.
- `drafting`: a local issue or pull request draft is being prepared.
- `ready`: evidence and proposed scope are ready for upstream.
- `filed`: an upstream issue or pull request exists.
- `blocked`: a named prerequisite is unavailable.
- `merged`, `closed`, or `superseded`: terminal upstream states.

## Upstream issue and pull request log

| Local ID | Upstream item | Status | Local purpose | Next action |
| --- | --- | --- | --- | --- |
| UP-001 | [Issue #1: complete ISO-to-runtime workflow](https://github.com/alex-stampede/GRFS-Recomp/issues/1) | filed, open | Track the known clean-build gap; do not file a duplicate setup issue | Revisit when public bootstrap/code-generation inputs and a contributor-owned compatible ISO are available |
| UP-002 | [Issue #2: Gunsmith materials and NVG geometry](https://github.com/alex-stampede/GRFS-Recomp/issues/2) | filed, open | Correctness gate for renderer experiments | Preserve this reproduction matrix in every graphics or performance proposal |
| UP-003 | [Issue #3: sustained frame cost vs. pipeline hitches](https://github.com/alex-stampede/GRFS-Recomp/issues/3) | filed, open | Measurement umbrella for Apple Silicon work | Develop instrumentation before claiming performance gains |
| UP-004 | Toolchain preflight and setup clarification | idea | Explain that the selected Swift compiler and macOS SDK must be compatible; make the host check fail with an actionable preflight message | Draft a small documentation/script PR after reproducing with an updated matching toolchain |
| UP-005 | Installation storage guidance | idea | Replace the unquantified storage requirement with a measured, conservative estimate | Measure ISO, staged extraction, installed data, caches, and update headroom using a contributor-owned compatible ISO |

No upstream pull requests have been filed from this fork yet.

## Setup audit

### Test host

| Component | Observed value |
| --- | --- |
| Hardware | Apple M5, 24 GiB, ARM64 |
| macOS | 27.0.1 |
| Selected developer directory | Command Line Tools |
| Python | 3.9.6 |
| Selected Swift compiler | 6.3.3 |
| Default macOS SDK | 27.0, built with Swift 6.4 |
| Compatible audit SDK | Bundled macOS 26.5 SDK |

The host had an internally mismatched Command Line Tools installation. This is
a local toolchain condition, not evidence of a GRFS source compilation defect.
Command Line Tools 27.2 was offered by Software Update but was not installed as
part of this audit.

### Results

| Check | Result | Evidence / limitation |
| --- | --- | --- |
| Clone and contributor entry points | pass | `CONTRIBUTING.md`, `docs/BUILDING.md`, and `docs/TESTING.md` agree about the supported host-only and release-runtime boundaries |
| Latest release resolution | pass | Latest download resolved to `v0.10.0.12`; size was 49,716,029 bytes |
| Release integrity | pass | ZIP SHA-256 matched published value `63a3b2a03d75f4cfe36b1076055d767b4d3bff7d8b1cffaaf23137b2b06d2cbd` |
| Host checks with default selected tools | fail | Swift 6.3.3 could not import an SDK built with Swift 6.4; the current prerequisite text checks tool presence but does not call out compiler/SDK compatibility |
| Host checks with a compatible compiler/SDK pairing | pass | The audit-only `SDKROOT` isolation ran synthetic ISO extraction, update, restart, and output-size checks successfully |
| Launcher rebuild around release runtime | pass | `launcher/build.py` produced an ad-hoc signed app and ZIP |
| Package verification | pass | `launcher/test_package.py` confirmed identities, icons, bundle resolution, unchanged runtime code, and signatures |
| Incompatible ISO handling | pass | Two ISO 9660 PSP images were rejected with `No compatible Xbox partition was found in this ISO.` |
| Compatible GRFS player installation | blocked | Requires a verified, personally dumped compatible Xbox 360 ISO; third-party-distributed game archives are not used for this work |
| Clean ISO-to-runtime recompilation | blocked upstream | The documentation accurately says this workflow is not public or reproducible yet; tracked by [issue #1](https://github.com/alex-stampede/GRFS-Recomp/issues/1) |

The documented release-runtime packaging path is reproducible. The documented
host suite is also reproducible once the selected compiler and SDK are
compatible. The complete game/runtime build is explicitly unavailable rather
than merely under-documented, so new work should contribute to issue #1 instead
of inventing unverified commands.

### Actionable setup findings

#### UP-004: matching Apple toolchain preflight

Proposed contribution scope:

1. Update `docs/BUILDING.md` and the collapsed build section in `README.md` to
   require a mutually compatible selected Xcode/Command Line Tools compiler and
   SDK, not only binaries named `swiftc` and `clang++`.
2. Add diagnostic commands for `xcode-select -p`, `swiftc --version`, and
   `xcrun --sdk macosx --show-sdk-path`.
3. Add a minimal Swift SDK probe before compiling project sources so a broken
   toolchain produces a short remediation message.
4. Validate both the failure path and the normal `scripts/check_contributor.py`
   path before filing a PR.

This should stay separate from issue #1: it improves the already-supported host
workflow but does not claim to rebuild generated game modules.

#### UP-005: storage estimate

`launcher/required-files.json` currently lists 376 files totaling 7.63 GiB.
That is only a lower bound. It excludes staging duplication, the ISO, shader and
pipeline caches, logs, updater downloads, and a previous-app backup. Do not
publish a recommended free-space number until a compatible installation is
measured end to end.

## Contribution hygiene

- Never commit or upload ISOs, XEX files, extracted assets, generated game
  implementations, saves, shader microcode, native pipeline caches, signing
  secrets, or unreviewed diagnostic archives.
- Record the upstream commit, release/runtime source, Mac model, macOS version,
  cache state, scene, display mode, and exact check used.
- Separate correctness fixes, instrumentation, and performance changes.
- Link a local result to upstream only after reproducing it against the current
  upstream baseline.
- Update this file when an item is filed, renumbered, closed, merged, or made
  obsolete by upstream.

## Fork workflow

Fast-forward local `main` from the fetch-only upstream remote, then update the
fork explicitly:

```sh
git fetch upstream
git switch main
git merge --ff-only upstream/main
git push origin main
```

Create contribution branches from the refreshed upstream baseline and push them
to `origin`. Do not restore a push URL for `upstream`.
