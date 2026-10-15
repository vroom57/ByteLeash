# ByteLeash

ByteLeash is a multi-platform project with Windows, Linux, macOS and firewall editions.

## Platform status

| Platform | Directory              | Status         |
|----------|------------------------|----------------|
| Windows  | `platforms/windows/`   | In development |
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
└── docs/            Documentation
```

Platform-independent code goes in `core/`. Each directory under `platforms/` contains
only what is specific to that operating system or device, and builds on `core/`.

## License

ByteLeash is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
