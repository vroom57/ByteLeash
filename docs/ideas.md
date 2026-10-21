# ByteLeash: Ideas & Working Notes

> Working document. Rough notes are fine here; once ideas firm up they can move
> into a proper project description or design docs.

## Current status (updated 2026-10-15)

Design review complete (see Design Q&A below); all open items decided.
Core throttling engine written and unit-tested (2026-10-21); no Windows code
yet.

### Open items

1. ~~Dormant threshold~~ **Decided:** 5 minutes with no open connections and
   no traffic; adjustable in settings.
2. ~~Settings file format~~ **Decided:** JSON (`ByteLeash.json`), parsed with
   nlohmann/json in `core/`; atomic save (temp file + rename).
3. ~~License notices~~ **Decided:** ship one combined `LICENSE.txt` next to
   the exe, with each component clearly identified; About box shows the same.
4. ~~Tray UI toolkit~~ **Decided:** plain Win32 + Direct2D, with a compact
   tray flyout and a full window. Pause/disable protection from the flyout.

### Next steps

1. ~~Commit the design docs.~~ Done.
2. ~~Install Visual Studio Community with the "Desktop development with C++"
   workload (includes CMake).~~ Done 2026-10-21: VS Community 2026 (18.11),
   MSVC 19.51, CMake 4.3.1; C++20 + `/MT` smoke test builds and runs.
3. ~~CMake project skeleton, then the `core/` throttling engine (token
   bucket, daily cap, payload-only accounting) with doctest unit tests.~~
   Done 2026-10-21: `byteleash_core` library (`AppBudget`, `Engine`) and
   `core_tests`; build with `scripts\build.cmd` or open the folder in VS.
   Not yet in core: LAN-exemption address classification, settings file.
4. WinDivert prototype: log upload packets with owning app and destination,
   to validate the provisional app-identity rule against real traffic. Also
   check packet sizes: with large send offload, Windows may hand WinDivert
   segments up to 64 KB, and a packet bigger than an app's burst can never
   pass.

## What is ByteLeash?

It's a Windows app that runs in the background, monitors network traffic, and by
default throttles upload traffic per app to a very slow rate unless explicitly
allowed. It's not exactly a security product; instead it slows down apps that
shouldn't be sending much data out to the internet. For example, an app that
needs to talk to its license server shouldn't need a constant connection, and
when it does connect it should only get a small burst followed by a trickle of
upload bandwidth. Downloads are generally unrestricted. Later versions should
have a graphical interface that shows all internet connections and the traffic
in and out.

## Problem it solves

It keeps software manufacturers on a leash: they don't get unlimited upload
capability, but their apps can still work with licensing servers, etc. For
example, with a 1 Gbps upload connection, an app could quietly stream audio or
screen captures out. Apps that don't really need the internet should be limited
to a short burst, then roughly 30 bytes per second, up to a daily cap. That's
not enough bandwidth for screen grabs. There are existing tools, but I want one
that works the way I want it to work.

## Who it's for

Home users, maybe small businesses, but no central administration is planned.
It should be free and lightweight.

## Core features

- Can be launched as a startup app
- Inspects and throttles upload traffic by default
- Tray icon where you can view settings for each app and allow apps higher
  upload limits
- Graphical view of all traffic leaving the machine: destinations (hostnames via
  SNI where available, otherwise IPs), amount of traffic, connection length /
  total time connected, and time of connections
- Maybe a max connection time, so long-lived connections get closed. This could
  prevent long uploads and further reduce an app's ability to do nefarious
  things behind the scenes.
- Counters for pure ACK / empty packets per app and across all apps, so
  unusual patterns (e.g. an app sending far more empty packets than its
  downloads explain) are visible even though they aren't throttled.

## Throttling model

Each unapproved app gets:

1. **Initial burst**: e.g. 64 KB, enough for a TCP + TLS handshake and a quick
   license check to complete without timing out.
2. **Throttled rate**: after the burst, ~30 B/s sustained (~2.6 MB/day if it ran
   flat out). 30 B/s is the default; the rate is configurable per app, higher
   or lower.
3. **Daily limit**: a hard cap per app, e.g. 3 MB/day.

Approved apps get higher or unlimited limits, set from the tray app.

Defaults:

- Local network (LAN) traffic is exempt by default, so NAS backups, printing,
  casting, etc. keep working.
- Downloads are not throttled.

## Architecture

Two parts:

- **Background service**: starts at boot (ideally before apps get network
  access), intercepts outbound packets, maps them to the process that sent them,
  and enforces the burst / rate / daily limits. Stores per-app settings and
  usage.
- **Tray app**: lightweight UI only. Talks to the service to show traffic and
  change per-app limits. Not required for throttling to work.

## Platform notes

### Windows (first)
- The actual program should only be three files, the DLL preferably in the program folder, the actual program and then a file that keeps customizations to rules, etc. (need to investigate the best method for easy distribution)
- Service launches at startup, preferably before the network is used
- Tray icon that can be opened to see the app
- Optimized for minimal resources, no bloatware
- Packet interception options:
  - **WinDivert** (leading candidate): third-party, already-signed driver;
    can intercept packets, identify the owning process, and delay them for
    throttling. Ships with the app; no separate install for the user.
    Dual-licensed LGPLv3 / GPLv2.
  - Windows Filtering Platform callout driver: most control, but requires
    driver signing (EV certificate + Microsoft attestation).
  - Built-in Windows QoS policies (`New-NetQosPolicy`): no driver, good for a
    quick prototype, but unclear whether they support "throttle all except
    allowed" or very low rates. Needs testing.


### Linux

-

### macOS

-

### Firewall edition

- A network firewall can't see which app sent a packet, so limits would be per
  device or per destination instead of per app.

## Decisions

- C++20, built with CMake and MSVC (Visual Studio Community, free). The C++
  runtime is linked statically (`/MT`) so users don't need the Visual C++
  Redistributable and no extra DLLs ship. Unit tests use doctest. All tooling
  is free: VS Community is free for open-source projects, CMake is
  BSD-licensed, doctest is MIT.
- GUI, no command line. Should be super simple to use and unobtrusive.
- Background service + tray app split
- Inspecting inside SSL/TLS traffic is out of scope (SNI gives hostnames without
  decryption)
- Upload budget counts payload bytes only. TCP/IP headers and pure ACKs / empty
  packets are not charged, so downloads keep working for throttled apps. Empty
  packets are still counted separately for visibility (see docs/behavior.md).
- Post-burst enforcement (v1): over-budget packets are dropped, not queued
  ("soft block"). Dropped packets aren't charged. TCP backs off on its own;
  small sends still trickle through, large uploads fail. Later: an optional
  per-app "working trickle" mode using MSS clamping, paced release and
  retransmit detection so connections survive at very low rates.
- Burst size, throttled rate and daily cap are all configurable per app, up
  or down. Defaults: **64 KB burst, 30 B/s, 3 MB/day**. At default settings
  the throttle alone limits an app to ~2.6 MB/day (burst + rate × 24 h), so
  the cap never triggers; it's a safety net for apps whose rate is raised.
- Burst and rate are one token bucket per app: capacity = burst size, refill =
  the app's throttled rate. Idle time refills the burst (64 KB at 30 B/s ≈ 36
  min). The bucket is shared across all of the app's connections, so opening
  more connections doesn't earn more burst. The daily cap still bounds totals.
- Core engine API: the platform layer passes in "now" as a monotonic
  instant (for the token bucket) plus the local date (for the midnight
  reset), so core has no clock or time-zone code and tests control time.
  Apps are keyed by an opaque UTF-8 identity string. An app shows
  **Throttled** from its first over-budget drop until its burst has fully
  refilled. Raising an app's burst adds the difference to its bucket at once.
- The throttle is the primary control; the daily cap is a safety net.
- Hitting the daily cap blocks the app's upload payload until the daily reset
  (empty packets still pass). The first time each app hits its cap that day,
  the tray shows a low-key notice with a "give it more today" option.
- Daily usage resets at local midnight (system local time, not UTC/GMT).
- The tray app shows every app and its current status (e.g. bursting,
  throttled, capped/blocked), so capped apps are visible at a glance.
- App identity (**provisional**, revisit after the prototype shows real
  traffic):
  - Normal apps: full exe path, with version-numbered folders
    (e.g. `app-1.0.9031`) matched as a wildcard so updates keep the same entry.
  - Service hosts (`svchost.exe`): identified by service name, e.g.
    `svchost.exe [DiagTrack]` (PID → hosted services lookup).
  - Kernel traffic: one "System" entry.
  - Code-signing publisher is displayed for information only, not used for
    identity. Publisher-based matching can be added later if paths cause
    trouble (e.g. reinstall to another drive creates a new entry).
- The tray app lists only apps that communicate on the network, grouped like
  Task Manager's Processes tab: **Apps** and **System services** shown in
  separate sections.
- Apps that go quiet move to a **Dormant** section lower in the list. Entries
  are cleared at local midnight or on reboot, whichever comes first. (Per-app
  settings are kept; only the list entry clears.) An app is dormant after
  no open connections and no traffic for 5 minutes (default; adjustable in
  settings). Apps holding a connection open never go dormant.
- Two modes (decided 2026-10-21). ByteLeash isn't a firewall, so it doesn't
  start out deny-all:
  - **Learning mode:** the default on first run. Nothing is limited; all
    traffic is counted and shown, so you can see which apps talk and how
    much. No time limit: learning lasts until you end it. While in learning
    mode, a tray notice at each logon reminds you. You can switch back to
    learning mode any time.
  - **Leash mode:** chosen when you end learning. You pick the apps to
    unleash (unlimited); every other app is leashed with the default limits.
    The built-in system-service exemptions stay unlimited.
  - No suggested apps on the review screen; that would mean maintaining a
    list of apps.
  - **Dry run during learning:** each app also runs a shadow budget with
    the limits it would get in Leash mode, to show what would have
    happened. Two counters, kept since learning started (not reset at
    midnight): **would have been throttled N times** (one per transition
    into Throttled, not per dropped packet: unthrottled traffic would
    otherwise inflate the count, since real TCP backs off after a few
    drops) and **would have hit its daily cap on N days**. The GUI shows
    counts above 999 as "999+"; the counters themselves are exact.
- New, unknown apps in Leash mode are leashed straight away with default
  limits, and a tray notification tells you (on by default). You can open
  the app's entry and unleash it. The tray icon also shows a "new apps"
  badge with a count, and new entries stay highlighted in the list until
  viewed. In learning mode new apps just appear in the list. (Revised
  2026-10-21; previously new apps were throttled silently with toasts off.)
- Distribution is one folder with four runtime files (docs/readmes not
  counted; goal is a tiny, unbloated directory):
  - `ByteLeash.exe`: one exe, two modes. Runs as the service when started by
    the Service Control Manager, as the tray app when launched by the user. On
    first run it installs itself as a service (one UAC prompt); no separate
    installer.
  - `WinDivert.dll` and `WinDivert64.sys`: WinDivert library and signed
    driver.
  - `ByteLeash.json` settings file next to the exe. Only the service (SYSTEM)
    writes it; the tray sends changes to the service. Daily usage is held in
    memory and saved to the same file every few minutes and at shutdown, so a
    reboot doesn't reset usage. Parsed with nlohmann/json (header-only, MIT)
    in `core/` so other editions can reuse it. Saves are atomic: write a temp
    file, then rename over the old one, so a crash or power cut can't corrupt
    the rules.
  - Plus one documentation file, `LICENSE.txt` (doesn't count toward the
    runtime files). It's a combined notice file: each shipped component gets
    its own clearly labeled section with its name, version, copyright holder,
    project URL, the license it's used under, and the full license text or
    notice that license requires:
    - **ByteLeash:** GPLv3 full text, plus where to get the source code.
    - **WinDivert** (`WinDivert.dll`, `WinDivert64.sys`): notice that ByteLeash
      uses WinDivert under the **LGPLv3** option (its GPLv2 option isn't
      compatible with GPLv3), plus the LGPLv3 full text. LGPLv3 also requires
      the GPLv3 text, which the ByteLeash section already includes.
    - **nlohmann/json:** MIT copyright notice and permission text.
    - Not included because they don't ship: doctest (tests only). The
      statically linked MSVC runtime needs no notice.
  - The About box shows the same content. Any new third-party component that
    ships must be added to `LICENSE.txt`.
- Boot-time coverage (v1): service start type is Automatic (not delayed), so
  it's running before user logon and covers apps that start at logon. Traffic
  from early-boot Windows services before the service starts isn't throttled.
  Persistent WFP "block until ready" filters are deferred: **revisit after
  v1**, and only with a safety fallback so a failed service can't leave the
  PC without internet.
- v1 protocol scope:
  - **IPv6:** supported, same as IPv4. LAN exemption includes IPv6 link-local
    (`fe80::/10`) and unique-local (`fc00::/7`) ranges.
  - **UDP / QUIC:** throttled like TCP (payload counted, over-budget packets
    dropped). QUIC destinations show as IPs in v1; QUIC SNI parsing later.
  - **VPNs:** per-app throttling applies to traffic before it enters the
    tunnel. The VPN client re-sends that traffic encrypted, so it must be
    approved (unlimited) to avoid breaking the VPN and double counting.
    Documented for users; test with WireGuard and OpenVPN. Auto-detecting
    VPN clients later. (Note: VPNs built into Windows may send the encrypted
    traffic as System rather than a separate client process; verify in
    testing.)
- UI toolkit: plain Win32 + Direct2D. No extra DLLs, native look. The app
  list uses the native ListView with group headers (Apps / System services /
  Dormant), like Task Manager. Direct2D draws traffic graphs. Dark mode needs
  extra work; decide v1 scope when building the UI.
- Tray interaction:
  - **Left-click:** compact flyout above the tray icon (like Windows' volume /
    network flyouts; closes when it loses focus). Shows overall status, counts
    of new and capped apps, the top 3–5 current uploaders with status, and two
    buttons: **Open ByteLeash** and **Pause protection**.
  - **Double-click:** opens the full window directly.
  - **Right-click:** menu with Open ByteLeash, Pause/Resume protection, Exit
    tray.
  - The full window and the flyout belong to the tray process. Closing the
    full window returns to the tray; the icon stays. **Exit tray** removes the
    icon, but the service keeps throttling. The tray starts at logon and can
    be relaunched from the Start menu / exe.
- Pause protection: choose a duration (15 min, 1 hour, until restart, until
  I resume); timed pauses resume automatically. "Until I resume" serves as
  disable; there's no separate disable control. While
  paused the tray icon changes to a clear "paused" look, traffic is still
  counted and shown but not charged to budgets or caps, and the service keeps
  running.
- System services default policy: a short built-in exemption list; every
  other service is throttled with normal defaults. All are user-changeable.
  - **Unlimited by default:** Windows Update (`wuauserv`), `BITS`, Defender
    (`WinDefend`, `WdNisSvc`), DNS client (`Dnscache`), DHCP (`Dhcp`), time
    sync (`W32Time`), network connectivity check (`NlaSvc`/NCSI), `System`.
  - **Throttled by default:** telemetry (`DiagTrack`), Delivery Optimization
    (`DoSvc`, peer-to-peer update uploads), and everything else.
  - BITS is exempt because Windows Update depends on it, but its counters and
    destinations stay visible so misuse as an upload bypass is noticeable.

## Open questions

See **Current status → Open items** at the top of this document.

## Known limitations

- Apps can bypass per-app limits by handing uploads to Windows system services
  (svchost, BITS, Windows Update) or by leaking data through DNS queries.
  Acceptable since this isn't a security product, but worth documenting.
- Because only payload is charged, an app could leak small amounts of data
  through header fields (e.g. IP ID, TCP options, timestamps) or through the
  timing/volume of empty packets. Mitigated only by visibility: the empty-packet
  counters make abnormal patterns noticeable.

## Technical concerns (design review 2026-10-15)

Issues raised before writing code. Each one feeds into a question in the
design Q&A below; the resolution gets recorded under Decisions.

1. **Upload ACKs.** Every download sends small TCP ACK packets upstream. If
   those count against the 30 B/s limit, throttled apps can't download either,
   which breaks "downloads are unrestricted". Pure ACKs (no payload) likely
   need to be exempt, and counting payload only avoids headers eating the
   budget.
2. **TCP behavior at 30 B/s.** A single full-size segment (~1460 B) takes ~50 s
   to send at 30 B/s. The sender's TCP stack will time out, retransmit, and
   eventually reset the connection, so "trickle" will in practice behave like
   "burst, then mostly blocked". Options: accept that as a soft block, clamp
   MSS so segments are small, and/or avoid charging retransmits to the budget.
3. **Packet-to-process mapping.** WinDivert's NETWORK layer doesn't carry a
   process ID. The FLOW/SOCKET layers do, so the service must track flows
   (5-tuple → PID) and then map PID → app identity (exe path, file hash, or
   signing publisher). A hash changes on every app update.
4. **"Three files" goal.** Service + tray + `WinDivert.dll` + `WinDivert64.sys`
   + config is already five. One exe running in service or tray mode gets
   closer; the driver file can't be avoided.
5. **Boot-time gap.** The service won't be running during early boot. Full
   coverage would need persistent/boot-time WFP filters that block until the
   service is up. Optional, but it's what "before apps get network access"
   implies.
6. **Core/platform split.** The throttling engine (burst, rate, daily cap,
   rule matching) is pure logic and belongs in `core/` as portable C++ with
   unit tests. It can be built and tested before any driver work.

## Design Q&A (in progress)

Questions are answered one at a time; answers move into Decisions.

1. ~~What counts against an app's upload budget?~~ **Answered:** payload only;
   pure ACKs / empty packets exempt but counted. See Decisions.
2. ~~Below-MTU trickle?~~ **Answered:** soft block (drop) for v1; optional
   per-app working-trickle mode later. Rate is configurable per app.
3. ~~When does the burst reset?~~ **Answered:** token bucket per app; burst
   refills continuously at the app's throttled rate.
4. ~~What happens when an app hits its daily cap?~~ **Answered:** block uploads
   until local midnight, one tray notice per app per day with "give it more
   today"; tray shows all apps' status.
5. ~~How is an app identified?~~ **Answered (provisional):** exe path with
   version-folder wildcards; svchost by service name; one "System" entry;
   publisher shown for info only.
6. ~~What happens when a new, unknown app first uploads?~~ **Answered:**
   throttled silently; tray badge + highlighted entries; toasts optional.
   **Revised 2026-10-21:** Learning mode first; in Leash mode new apps are
   leashed with a tray notification on by default.
7. ~~Default values; are burst and cap per-app configurable?~~ **Answered:**
   all three configurable per app; defaults 64 KB / 30 B/s / 3 MB/day.
8. ~~One exe with service/tray modes?~~ **Answered:** yes; four runtime files,
   settings next to the exe, usage saved periodically to the same file.
9. ~~Boot-time protection via persistent WFP filters?~~ **Answered:** not in
   v1; Automatic service start covers logon apps; revisit after v1.
10. ~~Scope for v1: IPv6, QUIC/UDP, VPNs?~~ **Answered:** all in v1; QUIC
    hostnames later; VPN client must be approved.
11. ~~Build system and C++ standard?~~ **Answered:** CMake + MSVC (Visual
    Studio Community) + C++20, static runtime, doctest.
12. ~~Default policy for Windows system services?~~ **Answered:** short
    built-in exemption list; everything else (incl. DiagTrack, DoSvc)
    throttled.

## Ideas parking lot

-
