# ByteLeash

Windows app that throttles per-app upload traffic (burst → trickle → daily cap).
Service + tray app design. See docs/ideas.md for the full idea and open questions.

## Status
`core/` throttling engine (`AppBudget`, `Engine`) built and unit-tested.
No Windows platform code yet. Build + test: `scripts\build.cmd [debug|release]`
(or open the folder in Visual Studio; it uses CMakePresets.json).

## Tooling
C++20, CMake, MSVC (Visual Studio Community), static CRT (`/MT`), doctest,
nlohmann/json for the `ByteLeash.json` settings file.
App behavior for users is documented in docs/behavior.md; keep it in sync.

## Licensing
Ships a combined LICENSE.txt (one labeled section per shipped component with
its required license text). Adding a shipped third-party dependency means
adding it there. WinDivert is used under its LGPLv3 option.

## Working agreements
- Don't commit without asking.
- docs/ideas.md is the source of truth for design; update it when decisions change.
