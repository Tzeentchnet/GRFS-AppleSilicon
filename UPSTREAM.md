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

## Local media convention

All local build and validation commands use `.build/media/GRFS.iso`, which is a
hard link to the contributor-owned ISO. The link and source must report the same
filesystem device and inode before use. Do not copy the ISO into the repository
or build output.

Both `.build/` and `*.iso` are ignored. The hard link is local-only and must
never be committed or uploaded. Recreate and revalidate it if the source ISO is
replaced with a different inode.

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
| UP-005 | Installation storage guidance | ready | Replace the unquantified storage requirement with measured ISO, extraction, and staging requirements | Draft a documentation change that distinguishes required free space from total on-disk use |
| UP-006 | Successful reinstall retains full game backup | ready | Clarify or fix the unbounded `Game-backup-<UUID>` left after replacing an existing installation | Confirm whether rollback retention is intended, then add cleanup/retention behavior and a transaction test |
| UP-007 | Frame/Pipeline Correlation Trace v1 for [issue #3](https://github.com/alex-stampede/GRFS-Recomp/issues/3) | drafting | Bounded local core, guest ABI, summarizer and ReXGlue patch now correlate guest swaps, submissions/fences, present enqueue and redacted pipeline events | Collect trace-off/on overhead and gameplay evidence after the generated game and matching runtime components can be rebuilt; do not claim display scanout |

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
| Compatible GRFS ISO validation | pass | A confirmed personal dump matched both executable SHA-256 values and all 376 required paths and sizes |
| Local ISO handling | pass | Build and validation use a git-ignored hard link with the same device/inode as the source ISO; no ISO copy is created |
| Isolated GRFS extraction | pass | Extracted exactly 8,191,666,861 bytes (7.63 GiB) into a git-ignored audit directory, then revalidated every size and both executable hashes |
| GUI installation and game launch | not run | The audit intentionally avoided changing real Application Support data, saves, or launching gameplay |
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

The compatible personal ISO was 8,738,846,720 bytes (8.14 GiB).
`launcher/required-files.json` lists 376 extracted files totaling
8,191,666,861 bytes (7.63 GiB), and the isolated extraction matched that
inventory exactly. The launcher currently requires extracted bytes plus
512 MiB of available capacity before installation.

These figures still exclude shader and pipeline caches, logs, updater downloads,
and app backups. Documentation should distinguish:

- at least 8.13 GiB of free destination capacity for the extraction guard;
- 7.63 GiB of installed game data after success;
- the separately stored 8.14 GiB ISO if it remains on the same volume;
- additional temporary capacity when replacing an existing installation.

The local extraction completed in 5.80 seconds on an Apple M5/APFS host, but
that is an audit observation, not a portable installation-time estimate.

#### UP-006: retained reinstall backup

When `Game/` already exists, `Launcher.selectISO()` moves it to a unique
`Game-backup-<UUID>` directory before promoting the staged extraction. The
rollback path restores that backup if promotion fails, but the success path
does not remove it and no other source references that backup name.

For this compatible edition, each retained directory is approximately 7.63
GiB. Repeated successful reinstalls can therefore consume another full game
copy each time. Upstream should decide between:

1. deleting the backup only after the new installation and state file are
   committed successfully; or
2. retaining a documented, bounded rollback copy with explicit replacement or
   cleanup behavior.

The fix should include a synthetic transaction test for promotion failure,
successful cleanup/retention, and preservation of the previous installation
until commit.

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
