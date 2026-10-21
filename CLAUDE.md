# ByteLeash

Windows app that throttles per-app upload traffic (burst → trickle → daily cap).
Service + tray app design, with a Learning mode (watch only, the default on
first run) and a Leash mode (limits enforced). See docs/ideas.md for the full
design, decisions, open items and next steps.

## Status (2026-10-21)
- `core/`: throttling engine (`AppBudget`, `Engine`) with Learning/Leash modes
  and dry-run counters, LAN address classifier (provisional), app-identity
  rules. Unit-tested (doctest).
- `platforms/windows/probe/`: watch-only ETW prototype, `byteleash-probe.exe`.
  No driver; run as admin: `byteleash-probe --seconds 120 --out report.txt`.
  First run's findings are in docs/ideas.md (Next steps 5).
- No service, tray app or settings file yet.

## Where to pick up
1. **Next:** driverless enforcement test. Can a user-mode WFP block cut an
   app's existing connections, and how low can a Windows QoS throttle rate
   go? This decides whether Leash mode works without WinDivert.
2. **Open items to discuss** (docs/ideas.md, Open items): 5 LAN exemption
   details, 6 WinDivert as the shipping dependency (frozen, no ARM64,
   antivirus false positives; ETW looks like the better path for watching).
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
- `docs/ideas.md` (design source of truth), `docs/behavior.md` (user-facing
  behavior), `SECURITY.md`, `README.md`.

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
