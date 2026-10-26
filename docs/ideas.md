# ByteLeash: Ideas & Working Notes

> Working document. Rough notes are fine here; once ideas firm up they can move
> into a proper project description or design docs.

## Current status (updated 2026-10-26)

**Current direction (decided 2026-10-26): a SRUM viewer.** The Leash work
grew larger than expected and is **paused**. The intermediate step is a
standalone app that shows the per-app upload / download totals Windows
already records in SRUM (see "SRUM viewer" below). After it, decide where
to pivot; seeing the data may suggest alerting features. Order of work:
[plan.md](plan.md).

Paused Leash work, kept for future development: design review complete
(see Design Q&A below); core throttling engine written and unit-tested;
watch-only ETW probe works without a driver (Next steps 5); driverless
enforcement test (Next steps 6) half done: the WFP part ran on loopback and
works, QoS and job throttling still need a second PC. WinDivert is not used
for now (item 6). Open items 5 (LAN exemption) and 7 (WFP gate / overshoot
/ driverless throttling) belong to the Leash work and wait with it. Items
below are numbered as before.

### Open items

1. ~~Dormant threshold~~ **Decided:** 5 minutes with no open connections and
   no traffic; adjustable in settings.
2. ~~Settings file format~~ **Decided:** JSON (`ByteLeash.json`), parsed with
   nlohmann/json in `core/`; atomic save (temp file + rename).
3. ~~License notices~~ **Decided:** ship one combined `LICENSE.txt` next to
   the exe, with each component clearly identified; About box shows the same.
4. ~~Tray UI toolkit~~ **Decided:** plain Win32 + Direct2D, with a compact
   tray flyout and a full window. Pause/disable protection from the flyout.
5. **LAN exemption details: discuss in a follow-up conversation.**
   `core/` has a provisional classifier (`byteleash/address.h`) so work can
   continue. Currently local: IPv4 loopback, 10/8, 172.16/12, 192.168/16,
   link-local 169.254/16, multicast 224/4, broadcast; IPv6 loopback,
   link-local fe80::/10, unique-local fc00::/7, multicast ff00::/8, and
   IPv4-mapped addresses by their IPv4 rule. Questions:
   - **Global IPv6 on the LAN:** devices on your own network often have
     public IPv6 addresses (e.g. a NAS at 2001:…). These count as internet
     today. Detecting "on-link" prefixes needs the Windows routing table
     (platform code, not core). Worth doing?
   - **CGNAT 100.64/10:** currently not local. It's usually the ISP side,
     but Tailscale uses it for its VPN; treat it like any VPN?
   - **Multicast:** all multicast is exempt (mDNS, SSDP, casting). Fine, or
     only link-local scopes?
   - **Should LAN traffic be shown** in the traffic view (counted but never
     charged) or hidden entirely?
   - **User-defined local ranges** (e.g. a second subnet or a routed home
     lab): needed in v1?
6. ~~WinDivert as the shipping dependency~~ **Decided 2026-10-26:** not
   using WinDivert at this time, because it looks unsupported. ByteLeash
   goes driverless: ETW for watching, and Windows' own mechanisms (WFP
   user-mode filters, QoS policies) for enforcement, pending Next steps 6.
   Revisit only if the driverless path can't enforce Leash mode. Background:
   from the maintainer
   (basil00/WinDivert issue #395, 2025-09): 2.2.2 is the long-term version;
   he's "not very active"; even security fixes "would be difficult, due to
   the driver signing process", which depends on finding third parties to
   sign. Risks for ByteLeash: no security fixes, **no ARM64** (Windows on
   ARM laptops can't run it), antivirus false positives (Defender on the
   zip; BitDefender reported quarantining `WinDivert64.sys`). Fine for the
   prototype. Alternatives to weigh: driverless monitoring (Windows' own
   per-app traffic stats) for learning mode, Windows QoS policies for
   enforcement, or ByteLeash's own WFP driver (needs EV cert + Microsoft
   attestation).

7. **Leash enforcement with a WFP gate: discuss.** (From the 2026-10-26
   WFP test, Next steps 6.) A driverless block is all-or-nothing per app:
   it cuts the app's connections and blocks **downloads too** while it's
   on, unlike the "downloads unrestricted" rule. A full-refill gate (64 KB
   at 30 B/s ≈ 36 min) would take an app fully offline for half an hour
   after each burst. Possible shape: "debt" gating. The bucket may go
   negative (ETW reports sends after the fact), and the block lasts only
   until the bucket is back above zero or a small threshold. At 30 B/s
   that's tens of seconds per KB, so the app gets short windows to
   reconnect and send a little: a coarse trickle. Questions: is losing
   downloads while gated acceptable? What threshold reopens the gate? Does
   QoS (if it works at low rates) replace the gate for apps given a
   higher rate?
   - **Overshoot (raised 2026-10-26):** the gate closes only after ETW
     reports the sends, so a fast uploader can send far more than its
     burst first (at 1 Gbit/s, ~1 s of ETW delay is ~100 MB). The debt
     model and daily cap punish it afterwards, but the data is already
     out. Measure ETW's delivery delay (M1). Possible fix: a permanent QoS
     ceiling on every leashed app (e.g. 1 Mbit/s) so the leak before the
     gate closes stays small (~100 KB); depends on the QoS test.
   - Summary of the driverless model: rate = average enforced by on/off
     blocks (no smooth 30 B/s); daily cap = block until midnight (works
     as designed).
   - **Driverless smooth throttling, candidates (raised 2026-10-26,
     untested):** Windows' own signed drivers can pace upload traffic,
     controlled from user mode:
     - **Job object network rate control**
       (`JobObjectNetRateControlInformation`, Windows 10 1607+): max
       outgoing bytes/s per job; the service puts an app's process in a
       job and changes the limit at runtime (full speed during the burst,
       the throttled rate after). Upload only, so downloads keep working.
       Most promising. Unknowns: lowest enforced rate, UDP/QUIC, TCP
       behavior at very low rates, svchost services, processes already in
       a job. `job` mode added to the enforcement test 2026-10-26; run it
       with the QoS test (second PC).
     - **QoS policies** (already in the test): the same packet scheduler,
       matched by exe path, slower to change.
     - **Wintun** (WireGuard's signed virtual adapter): full per-packet
       control via a user-mode local "VPN", but heavy (own network stack),
       conflicts with real VPNs, risky if it crashes. Last resort.
     - Not viable: capture drivers (Npcap: can't hold or drop), WFP
       without a callout driver (allow/block only), system proxy settings
       (apps ignore them).
     If job or QoS throttling works at low rates, it becomes the main
     enforcement and the WFP block is used for the daily cap.

### Next steps

1. ~~Commit the design docs.~~ Done.
2. ~~Install Visual Studio Community with the "Desktop development with C++"
   workload (includes CMake).~~ Done 2026-10-21: VS Community 2026 (18.11),
   MSVC 19.51, CMake 4.3.1; C++20 + `/MT` smoke test builds and runs.
3. ~~CMake project skeleton, then the `core/` throttling engine (token
   bucket, daily cap, payload-only accounting) with doctest unit tests.~~
   Done 2026-10-21: `byteleash_core` library (`AppBudget`, `Engine`) and
   `core_tests`; build with `scripts\build.cmd` or open the folder in VS.
   Since added: Learning/Leash modes with dry-run counters, and a
   provisional LAN address classifier (see Open items 5). Not yet in core:
   settings file, "known apps" list for new-app notifications.
4. **Shelved 2026-10-26** (WinDivert not used, see Open items 6; the ETW
   probe in step 5 covered the watching part). WinDivert prototype: log
   upload packets with owning app and destination,
   to validate the provisional app-identity rule against real traffic. Also
   check packet sizes: with large send offload, Windows may hand WinDivert
   segments up to 64 KB, and a packet bigger than an app's burst can never
   pass. WinDivert isn't installed yet; plan: pin release 2.2.2 (latest,
   2022), download the official zip at build time with a pinned hash (like
   doctest) and copy `WinDivert.dll` / `WinDivert64.sys` next to the exe.
   Also check: driver loads with Memory Integrity (HVCI) and Secure Boot on,
   and whether antivirus products flag it.
   **Confirmed 2026-10-21:** Microsoft Defender quarantined the official
   `WinDivert-2.2.2-A.zip` on download (`Misleading:Win32/Malgent!AMTB`,
   severity high). Scanned individually, every file in the zip is clean,
   including the shipped two (`WinDivert.dll`, `WinDivert64.sys`), so the
   zip as a whole (likely the sample programs) triggered it.
   `WinDivert64.sys` is Microsoft attestation-signed (2022-09-20), so
   Windows will load it; its EV co-signature is from a third-party company
   (成都密思听科技有限公司), not the WinDivert author. See Open items 6.
5. ~~ETW probe (driverless alternative to step 4).~~ Done 2026-10-21:
   `platforms/windows/probe/` (`byteleash-probe.exe`, watch-only, needs
   admin). First 2-minute run on the dev PC:
   - **Reliable:** 38,891 Kernel-Network events, 0 lost; all send/receive
     events (IDs 10/11 TCPv4, 26/27 TCPv6, 42/43 UDPv4, 58/59 UDPv6)
     decoded via TDH; ports correct (443/80 on top).
   - **Identity rules hold up:** svchost split by service (wuauserv, DoSvc,
     DiagTrack, BITS, Dnscache, LicenseManager, …); version wildcards work
     for `EdgeWebView\Application\*\`, Store apps
     (`Microsoft.WindowsStore_*_x64__…`) and Defender (`Platform\*-0\`).
   - **Hostnames via DNS-Client events: partial.** Edge and Windows
     components mostly named; Chrome destinations mostly not (it uses its
     own resolver / DNS over HTTPS). Likely need SNI or reverse lookups for
     those, or accept IPs.
   - **Short-lived processes:** 2 PIDs had exited before lookup. Fix:
     subscribe to Microsoft-Windows-Kernel-Process start events to record
     image paths as processes start.
   - **Exe file names that change:** e.g. an updater leaving
     `claude.exe.old.<number>` running; the version wildcard only touches
     folders, so each such rename is a new app. Decide whether to extend
     the rule.
   - **Send sizes:** ETW reports send operations, not packets: up to 27 KB
     (wuauserv) and 16 KB (browsers), none over 64 KB in this run. A single
     send larger than an app's burst can never pass in the current engine;
     with ETW-based enforcement that needs a rule (e.g. allow the bucket to
     go negative).
   - **No empty-packet (ACK) data**, as expected: those counters would be
     lost with ETW.
   - **Dry run:** both browsers, the Store and Windows Update would each
     have been throttled once in 2 minutes at default limits (the probe
     applied defaults to every app, ignoring the built-in exemption list).
     DiagTrack sent 19 KB, DoSvc 53 KB while downloading 94.5 MB.
   - **Overhead:** event 18 ("protocol copied data") was the most frequent
     (~80k in 2 min) and is unused; filter by event ID in the real service.
6. **Driverless enforcement test (next).** Small test program, admin:
   - Does a user-mode WFP block filter for one app cut its *existing*
     connections, or only new ones? (ALE layers decide at connect time;
     transport-layer filters by 5-tuple may be needed.)
   - Lowest rate a Windows QoS throttle policy
     (`New-NetQosPolicy -AppPathNameMatchCondition
     -ThrottleRateActionBitsPerSecond`) accepts and actually enforces.
   - Outcome decides how Leash mode is enforced without a driver (WinDivert
     is out for now, see Open items 6).
   - **WFP results 2026-10-26** (`byteleash-enforce-test`, loopback to a sink
     on the same PC, Windows build 26200, Memory Integrity off; raw output
     in `docs/test-results/2026-10-26-wfp-loopback/`):
     - **App-ID block at the ALE connect layers cuts running connections
       at once.** Filter added in under 1 ms; a 100+ MB/s upload stopped
       before the next 100 ms sample, and the connection was gone (Windows
       re-authorizes existing connections when a filter is added). New
       connections fail immediately (Winsock 10013, access denied).
     - **Cut connections don't come back.** After the filter is removed
       (10 s or 60 s later), the old connection stays dead; new connections
       work at once. Apps have to reconnect.
     - **Per-connection block at the transport layer** also stops data at
       once, but the connection lingers (TCP retransmits), doesn't resume
       after the filter is removed, and was reset ~9 s later. No advantage
       over the app block; not worth pursuing.
     - **Fail-safe confirmed:** with a dynamic WFP session, killing the
       process (`taskkill /f`) removed its filters (2 present during the
       run, 0 after); nothing to clean up.
     - **Still to test:** QoS throttles (need a sink on a second PC, since
       QoS ignores loopback); the same WFP runs over a real network
       adapter and with Memory Integrity on; ETW reaction time (how much an
       app sends before the block lands).
     - Consequence: see Open items 7.

## SRUM viewer (current direction, 2026-10-26)

A standalone Windows app (no tray icon, no service) that lists every app
with its **cumulative upload and download**, read from SRUM, the System
Resource Usage Monitor that Windows runs all the time. Steps in plan.md
(V1–V3).

### What exists already

- **Settings → Network & internet → Advanced network settings → Data
  usage:** per-app totals for 30 days per adapter, but one combined number
  (no up / down split).
- **Task Manager → App history:** combined network column, Store apps by
  default; not a report. **Resource Monitor → Network:** live per-process
  send / receive rates, no totals.
- Conclusion: the data exists in Windows, but Windows doesn't show it split
  into up and down. ByteLeash builds a viewer that does (GPLv3).

### Technical notes (to verify in V1)

- Database: `C:\Windows\System32\sru\SRUDB.dat`, an ESE ("JET Blue")
  database, held open by Windows (Diagnostic Policy Service). Read a copy
  made through a volume shadow copy (`esentutl /y <file> /vss /d <copy>`
  is built into Windows); needs admin. The copy may need ESE recovery
  before it opens cleanly (srum-dump has notes on this).
- Read it with Windows' own ESE API (`esent.dll`), so no third-party
  dependency and nothing to add to LICENSE.txt.
- Network data usage table (per srum-dump and public write-ups, verify):
  `{973F5D5C-1D90-4944-BE8E-24B94231A174}`, columns include `TimeStamp`,
  `AppId`, `UserId`, `InterfaceLuid`, `L2ProfileId`, `BytesSent`,
  `BytesRecvd`. `AppId` / `UserId` are indexes into `SruDbIdMapTable`
  (app path or name, user SID).
- Granularity is about **one row per app per hour**; Windows writes to the
  database on its own schedule (roughly hourly), so the last hour or so
  isn't there yet. Retention is roughly 30–60 days.
- `InterfaceLuid` / `L2ProfileId` tell which adapter and which Wi-Fi
  network, which could split home vs. other networks.

### Open questions

- How does SRUM name svchost services, Store apps and system traffic?
  Can ByteLeash's app-identity rules (core/) group them the same way?
- Does SRUM count LAN traffic separately or at all? (Matters for Open
  item 5 later.)
- Is the hourly lag acceptable, or should a live ETW layer (from the
  probe) fill in the last hour later?
- Alerting ideas: collect them in V3, after using the viewer.

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

**v1 audience (decided 2026-10-26):** the author's own home PCs, running
Windows 11. A public release for general home users comes later and adds its
own work (onboarding, code signing, antivirus reputation, support). v1 still
follows the design here, but doesn't need to polish for strangers.

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
- Packet interception options (**decided 2026-10-26:** driverless; ETW to
  watch, WFP user-mode filters / QoS policies to enforce; see Open items 6):
  - **WinDivert** (not used at this time; looks unsupported): third-party, already-signed driver;
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
  counted; goal is a tiny, unbloated directory). **Update 2026-10-26:** with
  WinDivert out, the two WinDivert files below drop, leaving two runtime
  files (`ByteLeash.exe`, `ByteLeash.json`):
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
