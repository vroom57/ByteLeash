# WFP enforcement test, loopback (2026-10-26)

Part of plan M1 (driverless enforcement test). Findings are recorded in
[ideas.md](../../ideas.md) (Next steps 6, Open items 7); this folder keeps
the raw output.

## Setup

- Dev PC, Windows 11 build 26200.9168, Memory Integrity off.
- `byteleash-enforce-test.exe` (debug build) uploading to a sink on the
  same PC at `127.0.0.1:5201`. The sink was a renamed copy (`bl-sink.exe`)
  so the app-ID block didn't match it.
- Run with what is now
  `platforms/windows/enforce_test/run-wfp-loopback.cmd`, from an elevated
  prompt (one UAC approval).
- "acked" counts come from the sender's `SIO_TCP_INFO` (bytes the sink
  acknowledged), sampled every 100 ms, printed every second.

## Files

| File             | Test |
|------------------|------|
| `wfp-app.txt`    | Block this app by app ID at the ALE connect layers for 10 s, then remove |
| `wfp-conn.txt`   | Block only the running connection at the outbound transport layer for 10 s |
| `wfp-app-60.txt` | App block for 60 s, then 20 s unblocked: does the connection recover? |
| `wfp-kill.txt`   | App block for 120 s, process killed after ~10 s (output ends there) |
| `kill.txt`       | `taskkill /f` output for the kill test |
| `cleanup.txt`    | `cleanup` after the kill: leftover filters / QoS policies |

Filter counts from `netsh wfp show filters` (full dumps not kept: 1.3 MB
each, all of the PC's filters): **2** filters named "ByteLeash test" while
the killed run was blocking (IPv4 + IPv6), **0** three seconds after the
kill.

## Results

| Test | During block | After unblock | New connection while blocked |
|------|--------------|---------------|------------------------------|
| App block, 10 s | Upload (~90–130 MB/s) stopped before the first 100 ms sample; connection gone | Old connection stays dead | Refused at once (Winsock 10013) |
| App block, 60 s | Same | Same | Same |
| Connection block, 10 s | Data stopped, TCP kept retransmitting | No resume; reset (10054) ~9 s later | Allowed (block was per connection) |
| Kill mid-block | Filters present (2) | Filters gone after kill (0); `cleanup` found nothing | n/a |

Filter add took 0.5–0.7 ms in every run.

## Caveats

- Loopback skips the network adapter; repeat over a real adapter (second
  PC as sink) to confirm.
- Memory Integrity was off; repeat with it on.
- When the connection is already gone at the start of a phase, the report
  says "no upload connection at phase start" without the send error code.
- QoS throttles weren't tested: they don't apply to loopback.
