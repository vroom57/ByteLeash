# ByteLeash Behavior

How ByteLeash treats network traffic. This describes the intended behavior of
the application; design discussion lives in [ideas.md](ideas.md).

## Traffic accounting

### What counts against an app's upload budget

Only **payload bytes** sent by the app count toward its burst, throttled rate
and daily cap. Specifically:

| Outbound packet                                | Charged to budget? | Counted? |
|------------------------------------------------|--------------------|----------|
| TCP segment carrying data                      | Payload bytes only | Yes      |
| UDP datagram                                   | Payload bytes only | Yes      |
| Pure TCP ACK (no payload)                      | No                 | Yes, as empty packet |
| Other empty TCP packets (SYN, FIN, RST, keepalive) | No             | Yes, as empty packet |
| IP and TCP/UDP headers on any packet           | No                 | Header bytes tallied with the packet |

Why: every download sends ACKs back upstream. If those were charged, a throttled
app couldn't download anything, which breaks the rule that downloads are
unrestricted. Charging payload only also means the configured numbers
(e.g. 30 B/s, 3 MB/day) mean what they say: the data an app actually sends.

### Empty-packet counters

Empty packets aren't throttled, but they are counted so unusual behavior is
visible. ByteLeash keeps these counters:

- **Per app:** empty packet count and their header bytes, alongside payload
  bytes sent and bytes received.
- **Across all apps:** the same totals summed, so a system-wide pattern shows up
  even when no single app looks unusual.

The counters show in the tray app's traffic view. A normal app's empty-packet
count roughly follows its download activity (ACKs for received data). An app
sending many empty packets while receiving little is worth a look.

## Default limits

Every app gets three limits. All three can be changed per app, higher or
lower, from the tray app.

| Limit          | Default   | What it does                                  |
|----------------|-----------|-----------------------------------------------|
| Burst          | 64 KB     | Upload allowed at full speed before throttling |
| Throttled rate | 30 B/s    | Sustained upload rate once the burst is used  |
| Daily cap      | 3 MB      | Hard limit per day; resets at local midnight  |

With the defaults, the throttle by itself limits an app to about 2.6 MB a day
(64 KB + 30 B/s × 24 h), so the daily cap is never reached. The cap matters
for apps whose rate has been raised, catching anything that sends far more
than expected.

## Burst

Each app has one upload budget shaped like a tank (a token bucket):

- **Capacity** is the app's burst size (default 64 KB). The tank starts full.
- **Sending** payload drains it.
- **Refill** happens continuously at the app's throttled rate, whether or not
  the app is sending. At 30 B/s a fully drained 64 KB burst refills in about
  36 minutes; apps with a higher rate refill faster.

The tank is shared by all of the app's connections, so opening more
connections doesn't earn more burst. An app that checks in occasionally (e.g.
an hourly license check) gets its full burst every time; an app that sends
continuously stays drained and is held to its rate. The daily cap applies on
top of this.

## Throttling after the burst

Once an app has used its burst, its uploads are limited to its throttled rate.
The default is 30 B/s; the rate can be set per app, higher or lower.

### Enforcement: drop, don't queue

A packet that would exceed the app's budget is dropped, not held in a queue.
Dropped packets aren't charged. The app's TCP stack notices the loss, backs off
and retries, which paces it naturally.

What that means in practice:

- **At very low rates (like the 30 B/s default)**, a full-size packet
  (~1460 B) needs ~50 s of budget, longer than Windows TCP keeps retrying.
  Small sends (a short license check, a heartbeat) still get through slowly;
  large uploads stall and the connection is eventually dropped. This is a
  **soft block** and is the intended effect.
- **At higher configured rates**, the same mechanism acts as an ordinary
  bandwidth limit: TCP adapts to the loss and settles near the allowed rate.

### Planned: working-trickle mode

A later, optional per-app mode keeps connections alive at very low rates by
shrinking packet size during the TCP handshake (MSS clamping), releasing
packets on schedule, and not charging retransmitted data twice. Not in v1.

## Daily cap

The throttle is the main control. The daily cap is a safety net that catches an
app that manages to send more than expected over a day.

- **Reset:** usage counters reset at **local midnight** (the PC's local time,
  not UTC/GMT).
- **When an app hits its cap:** its upload payload is blocked until the next
  reset. Empty packets (ACKs etc.) still pass, so transfers already in progress
  can finish receiving, but the app can't start new requests, which in practice
  takes it offline for the rest of the day.
- **Notice:** the first time each app hits its cap on a given day, the tray
  shows a low-key notice with a **"give it more today"** option. No repeat
  notices for that app until the next day.

## What counts as an "app"

Limits and counters are tracked per app. An app is identified by
(provisional; may change after real-world testing):

- **Normal programs:** the full path of the exe. Folders named with a
  version number (e.g. `...\Discord\app-1.0.9031\Discord.exe`) are treated as
  a wildcard, so an app that updates into a new folder keeps its settings.
  Moving or reinstalling an app to a different location creates a new entry.
- **Windows services** hosted in `svchost.exe`: the service name, e.g.
  `svchost.exe [DiagTrack]`, so each service appears separately instead of
  all collapsing into "svchost".
- **Kernel traffic** (e.g. file sharing): a single **System** entry.

Each exe is its own entry, so a product's updater or crash reporter appears
separately from the main program. The tray shows the exe's code-signing
publisher for information; it isn't used to identify the app.

## Tray icon and windows

ByteLeash's protection runs in a background service. The tray icon is just a
way to see and control it.

- **Click the tray icon** to open a small panel showing the overall status,
  new and capped apps, and the apps uploading the most right now. From there,
  **Open ByteLeash** shows the full window, and **Pause protection**
  temporarily stops throttling.
- **Double-click** the icon to open the full window directly.
- **Right-click** for a menu: Open ByteLeash, Pause/Resume protection, Exit
  tray.
- **Closing the full window** sends it back to the tray; the icon stays.
- **Exit tray** removes the icon, but protection keeps running in the
  background. The tray comes back at next logon, or launch ByteLeash again.

### Pausing protection

Pausing stops all throttling for a chosen time: 15 minutes, 1 hour, until
restart, or until you resume. Timed pauses resume on their own. While paused,
the tray icon shows a "paused" look so it's obvious, and traffic is still
shown but doesn't count against any app's limits.

## Tray app: app list and status

The full window lists only apps that communicate on the network, in two
sections, similar to Task Manager's Processes tab:

- **Apps:** ordinary programs.
- **System services:** Windows services and the System entry.
- **Dormant:** apps from either section that have gone quiet: no open
  connections and no traffic for 5 minutes (adjustable in settings). They
  move back up as soon as they communicate again. Apps that keep a
  connection open (chat apps, game launchers) stay in their main section,
  since they can send at any moment.

An app stays listed until local midnight or the next reboot, whichever comes
first, then drops off until it communicates again. Its settings (limits,
approvals) are kept regardless.

Each entry shows its current status, so a blocked app is easy to spot:

| Status    | Meaning                                              |
|-----------|------------------------------------------------------|
| Bursting  | Sending within its burst budget                      |
| Throttled | Burst used up; held to its throttled rate until the burst has fully refilled |
| Capped    | Hit its daily cap; uploads blocked until midnight    |
| Approved  | Has custom (higher or unlimited) limits              |

## Learning mode and Leash mode

ByteLeash has two modes.

- **Learning mode** is where ByteLeash starts after install. Nothing is
  limited, but all traffic is counted and shown, so you can see which apps
  talk to the internet and how much they send. Learning has no time limit;
  it lasts until you end it. While you're in learning mode, ByteLeash
  reminds you when you log on.
- **Leash mode** starts when you end learning. You choose the apps to
  **unleash** (no limits); every other app is leashed with the default
  limits. Windows' essential services stay unlimited either way (see
  [Windows system services](#windows-system-services)).

You can switch back to learning mode at any time, for example after
installing new software, and end it again when you're ready.

### What would have happened

While learning, ByteLeash also works out what Leash mode would have done to
each app with its limits, so you can decide which apps to unleash. Each app
shows, counted since learning started:

- **Would have been throttled N times:** how often the app would have used
  up its burst and been slowed to its throttled rate. A single large upload
  counts once, not once per packet.
- **Would have hit its daily cap on N days.**

Counts above 999 show as **999+**.

## New apps

In learning mode, apps ByteLeash hasn't seen before simply appear in the
list.

In Leash mode, a new app is leashed straight away with the default limits:

- A tray notification tells you a new app was leashed. Open its entry to
  unleash it if you want.
- The tray icon shows a small badge with the number of new apps.
- New entries are highlighted in the list until you've viewed them.

## Windows system services

Windows services are throttled like apps, except for a short list that
Windows needs to stay updated, protected and online. Any of these defaults can
be changed in the tray app.

| Unlimited by default                                     | Why                                   |
|----------------------------------------------------------|---------------------------------------|
| Windows Update (`wuauserv`) and `BITS`                   | Needed to check for and fetch updates |
| Microsoft Defender (`WinDefend`, `WdNisSvc`)             | Cloud protection and definitions      |
| DNS client, DHCP, time sync (`W32Time`)                  | Basic networking                      |
| Network connectivity check (`NlaSvc`)                    | Windows' "connected to internet" test |
| System                                                   | Kernel traffic, e.g. file sharing     |

Everything else gets the normal defaults, including:

- **Telemetry** (`DiagTrack`, Connected User Experiences and Telemetry).
- **Delivery Optimization** (`DoSvc`), which uploads updates you've
  downloaded to other PCs. Windows Update keeps working with it throttled.

BITS is unlimited because Windows Update relies on it. Other programs can
also hand uploads to BITS, so its traffic and destinations stay visible in the
tray; unusual BITS uploads are worth a look.

## Protocols and VPNs

- **IPv4 and IPv6** are treated the same. Local network traffic is exempt on
  both: private addresses (10.x, 172.16–31.x, 192.168.x), link-local,
  loopback, multicast and broadcast, plus IPv6 link-local and unique-local
  addresses. (Provisional; may change.)
- **TCP and UDP** are both throttled. This includes QUIC (HTTP/3), used by
  modern browsers and many apps. In this version, QUIC connections show the
  destination IP address rather than a hostname.
- **VPNs:** ByteLeash sees each app's traffic before it enters the VPN
  tunnel, so per-app limits still work. The VPN program then sends all of
  that traffic again, encrypted, as its own upload. **Set your VPN program to
  unlimited** in the tray app, or the VPN will be throttled and stop working.

## Known limitations

- Because headers and empty packets aren't charged, an app could in principle
  leak small amounts of data through header fields (IP ID, TCP options,
  timestamps) or through the timing and number of empty packets. ByteLeash
  doesn't block this; the empty-packet counters are what make it noticeable.
- ByteLeash starts automatically at boot, before you log in, so apps that
  start at logon are covered. Traffic sent by Windows itself early in boot,
  before ByteLeash is running, isn't throttled.
