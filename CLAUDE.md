# ByteLeash

Windows app that throttles per-app upload traffic (burst → trickle → daily cap).
Service + tray app design, with a Learning mode (watch only, the default on
first run) and a Leash mode (limits enforced). See docs/ideas.md for the full
design, decisions, open items and next steps.

**Current direction (2026-10-26):** the Leash work is paused. The active
step is a standalone **SRUM viewer**: lists every app's cumulative upload
and download from the data Windows records in SRUM. Then decide where to
pivot (possibly alerting). Steps V1–V3 in docs/plan.md; notes in
docs/ideas.md, "SRUM viewer".

## Status (2026-10-26)
- `core/`: throttling engine (`AppBudget`, `Engine`) with Learning/Leash modes
  and dry-run counters, LAN address classifier (provisional), app-identity
  rules. Unit-tested (doctest).
- `platforms/windows/probe/`: watch-only ETW prototype, `byteleash-probe.exe`.
  No driver; run as admin: `byteleash-probe --seconds 120 --out report.txt`.
  First run's findings are in docs/ideas.md (Next steps 5).
- `platforms/windows/enforce_test/`: `byteleash-enforce-test.exe` for plan
  M1 (WFP block / QoS throttle / job object rate limit against a sink on a
  second PC). WFP part run on loopback 2026-10-26 (results in
  docs/ideas.md, Next steps 6); QoS and job still need a second PC. Run
  steps in platforms/windows/README.md.
- No SRUM viewer code yet (plan V1 is next).
- Paused with the Leash work: no service, tray app or settings file yet.

## Where to pick up
For the author's own Windows 11 home PCs; order of work in docs/plan.md.
**Next: plan V1**, read SRUM (copy the locked SRUDB.dat via shadow copy,
open with esent.dll, dump per-app bytes sent / received per hour), then V2
(viewer window), V3 (review and pivot).

Paused (future development, Leash mode); resume from here if the project
pivots back:
1. **Plan M1 (paused):** driverless enforcement test. WFP
   answered on loopback: an app-ID block cuts existing connections at once
   (raw output in docs/test-results/2026-10-26-wfp-loopback/). Still open:
   how low a QoS or job object throttle goes (needs a second PC as sink;
   job throttling is the most promising driverless smooth throttle), WFP
   over a real
   adapter / Memory Integrity on, and Open item 7 (a WFP gate blocks
   downloads too; how should Leash mode use it?).
2. **WinDivert is not used at this time** (looks unsupported; decided
   2026-10-26, docs/ideas.md Open items 6). ByteLeash is driverless: ETW to
   watch, WFP user-mode filters / QoS policies to enforce.
   **Open items to discuss:** 5 LAN exemption details, 7 WFP gate design.
3. Gaps from the probe run: hostnames for apps with their own DNS (Chrome),
   short-lived processes, exe names that change on update, sends bigger than
   the burst, no empty-packet counters with ETW.

## Layout
- `core/`: portable logic, no OS calls. Headers in `core/include/byteleash/`.
- `platforms/windows/`: Windows code (currently only `probe/`).
- `tests/core/`: doctest unit tests, one file per core module.
- `scripts/build.cmd [debug|release]`: finds VS, configures, builds, tests.
  Or open the folder in Visual Studio (uses CMakePresets.json). Output in
  `build/<preset>/`.
- `docs/plan.md` (current steps V1–V3; paused Leash milestones M0–M6),
  `docs/ideas.md` (design
  source of truth), `docs/behavior.md` (user-facing behavior),
  `SECURITY.md`, `README.md`.

## Tooling
C++20, CMake, MSVC (Visual Studio Community 2026), static CRT (`/MT`),
`/W4 /WX`, doctest 2.5.3 (fetched at configure time with a pinned hash),
nlohmann/json planned for the `ByteLeash.json` settings file.
App behavior for users is documented in docs/behavior.md; keep it in sync.

## Licensing
GPLv3. Ships a combined LICENSE.txt (one labeled section per shipped component
with its required license text). Adding a shipped third-party dependency means
adding it there. If WinDivert ships, it's used under its LGPLv3 option.
doctest is test-only and doesn't ship.

## Working agreements
- Don't commit without asking. Pushing to GitHub (origin/main) is fine when
  asked to commit and push.
- Commits use the GitHub noreply address
  (190971508+vroom57@users.noreply.github.com, set globally); never the
  personal address.
- docs/ideas.md is the source of truth for design; update it when decisions
  change. Record new discussion topics there as Open items.
- Running the probe needs a UAC prompt the user approves; say so beforehand.
