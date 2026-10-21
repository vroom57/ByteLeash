# Security policy

## Reporting a vulnerability

Please report security problems privately, not in a public issue:

1. Go to the repository's **Security** tab.
2. Choose **Report a vulnerability**
   ([direct link](https://github.com/vroom57/ByteLeash/security/advisories/new)).
3. Describe the problem, the steps to reproduce it, the ByteLeash version and
   Windows version, and what an attacker could do with it.

The report is visible only to you and the maintainer. We can discuss it,
work on a fix privately, and publish an advisory once a fix is available.
ByteLeash is a personal project, so responses are best effort.

## Supported versions

ByteLeash hasn't had a release yet. Until it does, reports should be against
the latest code on `main`. Once releases start, only the latest release will
receive security fixes.

## What counts as a vulnerability

ByteLeash runs a Windows service as SYSTEM and loads the WinDivert driver, so
the most important problems are ones that let something gain privileges or
tamper with the system through ByteLeash, for example:

- A non-admin user or program getting code to run as SYSTEM through the
  service, its settings file (`ByteLeash.json`) or the tray-to-service
  connection.
- Changing another user's limits or turning protection off without the
  rights to do so.
- Crashing the service or the PC with crafted network traffic.

ByteLeash is a traffic limiter, not a security product. Ways for an app to
get around its limits that are already listed under **Known limitations**
in [docs/behavior.md](docs/behavior.md) (for example handing uploads to
Windows services, or leaking data through DNS or packet headers) are known
and aren't treated as vulnerabilities. New bypasses are welcome as regular
issues.

Problems in WinDivert itself should also be reported to the
[WinDivert project](https://github.com/basil00/WinDivert).
