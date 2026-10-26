# ByteLeash

ByteLeash keeps apps on a leash: it shows which apps send data out to the
internet, and the long-term goal is to limit how much each app can upload
(a short burst, then a trickle, up to a daily cap).

ByteLeash is supposed to be a multi-platform project with Windows, Linux, macOS and firewall editions.

Project is under development on Windows, but as I was working with Claude on it, I was seeing a lot of scope creep from unintended issues, especially with low-level throttling (drivers and driver signing, etc).

So that led to the next question, what's the MVP for this?

Pivot and create a standalone app that reads the SRUM data that Windows already collects on historical network usage up and down by app. Then find a way to display that and possibly incrementally build an alert system for it before we even get to throttling.

That will be the foundation for actual next steps.

Incidentally, per-app throttling looks much easier to build on Linux than on Windows (no driver signing needed).

## Where things stand

- **Now:** the SRUM viewer, step V1 (reading the SRUM data). There's
  nothing to download or install yet.
- **Paused:** the throttling (Leash) work. Its design, test programs and
  results are kept for later.
- **Details:** [docs/plan.md](docs/plan.md) (order of work),
  [docs/ideas.md](docs/ideas.md) (design notes and decisions).

## Platform status

| Platform | Directory              | Status         |
|----------|------------------------|----------------|
| Windows  | `platforms/windows/`   | In development (SRUM viewer) |
| Linux    | `platforms/linux/`     | Planned        |
| macOS    | `platforms/macos/`     | Planned        |
| Firewall | `platforms/firewall/`  | Planned        |

## Repository layout

```
ByteLeash/
├── core/            Shared, platform-independent logic used by every edition
├── platforms/
│   ├── windows/     Windows-specific code, build files and installer
│   ├── linux/       Linux-specific code and packaging
│   ├── macos/       macOS-specific code and packaging
│   └── firewall/    Firewall/appliance edition
├── tests/           Tests for core and per-platform code
├── scripts/         Build, packaging and helper scripts
└── docs/            Plan, design notes, behavior and test results
```

Platform-independent code goes in `core/`. Each directory under `platforms/` contains
only what is specific to that operating system or device, and builds on `core/`.

## Contact and support

- **Bugs and feature requests:** open an
  [issue](https://github.com/vroom57/ByteLeash/issues). Please include your
  Windows version and what you expected to happen.
- **Security problems:** please don't open a public issue. Report them
  privately as described in [SECURITY.md](SECURITY.md).

ByteLeash is a personal project, so replies are best effort.

## License

ByteLeash is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
