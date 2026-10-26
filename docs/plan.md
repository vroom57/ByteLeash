# ByteLeash Plan

The order of work and when each step counts as done. Design decisions live
in [ideas.md](ideas.md), user-facing behavior in [behavior.md](behavior.md).
Update this file as steps finish or the direction changes.

## Current direction (decided 2026-10-26): SRUM viewer

The Leash work (burst / trickle / cap enforcement) grew larger than
expected, so it's **paused** and kept below under Future development. The
project now takes an intermediate step: a **standalone viewer** for the
per-app network usage Windows already records in SRUM (System Resource
Usage Monitor): bytes sent and received per app, per hour, for about the
last 30–60 days. No tray icon, no service, nothing blocked.

Why: Windows records this data but shows no per-app upload / download
report (Settings → Data usage gives one combined number; Task Manager and
Resource Monitor show live rates only). Seeing the data visually should show which
apps send what, and give ideas for **alerting**, which may be more useful
than enforcement. **After the viewer, decide where to pivot** (alerting,
back to Leash, or something else).

Technical notes and open questions: ideas.md, "SRUM viewer".

### V1: Read SRUM (feasibility)

- Copy the live database (`C:\Windows\System32\sru\SRUDB.dat`, kept open
  by Windows) via a volume shadow copy, e.g. `esentutl /y ... /vss`, as
  admin.
- Open the copy with Windows' own ESE database engine (`esent.dll`, part of
  Windows, so no new dependency) and read the network usage table plus the
  ID map (app paths, user SIDs).
- Console tool that prints / exports per-app bytes sent and received per
  hour (CSV), in `platforms/windows/`.

**Done when:** the tool's per-app totals on this PC (sent + received)
agree with Settings → Data usage.

### V2: Viewer window

- Standalone Win32 window (no tray), runs as admin (one UAC prompt per
  start).
- App list with **upload and download totals** for a chosen period (last
  24 hours, 7 days, 30 days, all), sortable; apps grouped as in ideas.md
  (svchost services shown by service name if SRUM records them that way).
- Per-app hourly chart (Direct2D) of sent / received; filter by network
  adapter / Wi-Fi network if useful.
- Refresh (re-copy SRUM) and CSV export.

**Done when:** you use it on your own PCs to look at what your apps send.

### V3: Review and pivot

- Write down what the data showed: surprising uploaders, patterns over
  time, apps that upload more than they download.
- Collect alerting ideas (e.g. "app X uploaded more than N MB today", "new
  app started uploading") in ideas.md.
- **Decide the next direction** and update this plan.

**Done when:** the next direction is chosen and recorded here.

## Future development: Leash mode (paused 2026-10-26)

The original v1 plan, kept as it was when paused. M0 is done; M1 was in
progress (see its status). Resume from M1 if the project pivots back to
enforcement.

### What v1 was going to be (decided 2026-10-26)

- **For:** the author's own home PCs, Windows 11 only. No public release,
  code signing, antivirus reputation work or onboarding polish in v1.
- **Driverless:** ETW to watch traffic, Windows' own WFP user-mode filters
  and/or QoS policies to enforce. No WinDivert (ideas.md, Open items 6).
- **Success:** ByteLeash runs in Leash mode on all home PCs for weeks.
  Unapproved apps can't upload anything meaningful, the apps you rely on keep
  working, and you rarely have to touch it.
- **Must fail safe:** if the service stops, crashes or is removed, the PC
  keeps its internet and no filters or policies are left behind.

### M0: Foundations (done 2026-10-21)

- Design review, behavior doc.
- `core/`: throttling engine (`AppBudget`, `Engine`), Learning/Leash modes
  with dry-run counters, provisional LAN classifier, app-identity rules;
  doctest unit tests.
- `platforms/windows/probe/`: watch-only ETW probe, first run analysed
  (ideas.md, Next steps 5).

### M1: Driverless enforcement test (paused 2026-10-26)

**Where it stopped:** the WFP part is done (on loopback); everything listed
under "Remaining" below is still open. The test program and results stay
in the repo.

**2026-10-26:** test program built, `byteleash-enforce-test.exe`
(`platforms/windows/enforce_test/`, how to run in platforms/windows/README.md).
Covers WFP block (app ID and per-connection), QoS throttle rates and
clean-up. ETW reaction time is measured separately (probe / M3).
**WFP part run 2026-10-26 on loopback:** an app-ID block cuts running
connections instantly and is removed automatically if the process dies;
cut connections don't resume (ideas.md, Next steps 6). Added the same day:
a `job` mode (job object network rate control, a possible driverless
smooth throttle; ideas.md, Open items 7). Remaining: QoS and job throttle
runs (need a second PC), WFP over a real adapter / Memory Integrity on,
ETW reaction time, and the gate design question (Open items 7).

The biggest risk in v1. ETW only reports traffic after the fact; it can't
drop a packet. So enforcement becomes a per-app gate the service opens and
closes, not a per-packet decision. This milestone finds out how well that
works. Small admin test program in `platforms/windows/`.

- **WFP block:** add a user-mode block filter for one app (by app ID). Does
  it cut the app's *existing* connections or only new ones? If only new
  ones, do transport-layer filters on the app's open connections work?
- **Reaction time:** how long from the app sending past its burst (seen in
  ETW) to the block taking effect, and how much gets through in between.
- **QoS throttle:** lowest rate a `New-NetQosPolicy` throttle per app path
  accepts and actually enforces, and whether it applies to existing
  connections.
- **Clean-up:** filters and policies are removed when the test program
  exits or is killed (dynamic WFP sessions), and nothing survives a reboot.
- Check it all with Memory Integrity and Secure Boot on.

**Done when:** the enforcement model for Leash mode is decided and written
into ideas.md (Decisions), and behavior.md is updated where it changes. The
likely outcome: "burst at full speed, then blocked until the burst has
refilled" (a gate instead of a 30 B/s trickle), with a QoS throttle for
apps given a higher rate. The send-size gap from the probe (a single send
bigger than the burst) is settled as part of this.

### M2: Core updates for the enforcement model and settings

- Adapt `Engine` to the M1 model (e.g. gate open/closed per app, bucket
  allowed to go negative, events report "close gate" / "open gate").
- Settings file `ByteLeash.json` (nlohmann/json, atomic save): per-app
  limits, unleashed apps, mode, known-apps list, saved daily usage.
- App identity: decide the renamed-exe case (`app.exe.old.<n>`).
- Add nlohmann/json to `LICENSE.txt` (it ships).

**Done when:** all of the above is unit-tested in `tests/core/`, with no
Windows code involved.

### M3: Service in Learning mode

Watch-only, so it can't break anything; it runs on the main PC from here on
and gathers real data for later milestones.

- `ByteLeash.exe` service mode; installs itself as a service on first run
  (one UAC prompt); Automatic start.
- ETW session from the probe, filtered to the event IDs used; Kernel-Process
  start events so short-lived processes get identified.
- App identity, LAN classifier and engine in Learning mode with dry run;
  usage saved every few minutes and at shutdown.
- A text status dump (file or command) until the tray exists.
- Decide Open item 5 (LAN exemption details) during this milestone, with
  real traffic to look at.

**Done when:** it runs a week on the main PC through reboots, sleep and
updates, with low CPU and memory use, and the dry-run counters look
believable.

### M4: Tray app for Learning mode

- Service ↔ tray channel (named pipe; only the logged-on user's tray and
  admins can connect).
- App list grouped Apps / System services / Dormant, with status, traffic
  totals and dry-run counters.
- Left-click flyout, right-click menu, full window (Win32 + Direct2D).
- Logon reminder while in Learning mode.

**Done when:** Learning mode is useful day to day: you can see which apps
upload, how much, and what Leash mode would have done to them.

### M5: Leash mode

- Enforcement from M1 wired to the engine; built-in system-service
  exemptions.
- End-learning review screen: pick the apps to unleash.
- Per-app limit editing, new-app notifications and badge, daily-cap notice
  with "give it more today".
- Pause protection (15 min, 1 hour, until restart, until I resume).
- Fail-safe: all filters/policies tied to the service's lifetime; a service
  crash or stop restores full internet.
- Exfiltration test app: tries to stream data out at full speed.

**Done when:** the test app is held to its burst and blocked, the apps you
use keep working, and Leash mode runs a week on the main PC without you
having to rescue it.

### M6: Roll out to all home PCs (v1)

- Update procedure: replace the exe, service restarts, settings and usage
  kept.
- Uninstall: removes the service, any filters and QoS policies; then delete
  the folder.
- `LICENSE.txt` complete; README and behavior.md match what shipped.

**Done when:** every home PC runs Leash mode and a few weeks pass with the
v1 success criteria holding.

### After v1

Not planned in detail; candidates from ideas.md:

- Hostnames for apps with their own DNS (Chrome); QUIC.
- Traffic graphs and connection history; dark mode.
- Working-trickle mode; max connection time.
- Boot-time "block until ready" filters (with a safety fallback).
- A public release: Windows 10, code signing, onboarding, antivirus
  reputation.
