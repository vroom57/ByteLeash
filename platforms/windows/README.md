# ByteLeash for Windows

Windows-specific code, build configuration and installer files.

**Status:** In development

## probe/

`byteleash-probe.exe` is a watch-only prototype (not part of the shipped
app). It uses Windows' own ETW events, with no driver, to record which apps
send traffic, how much and where (hostnames from DNS-Client events), and runs
the core engine in learning mode to show what Leash mode would have done.
Nothing is blocked or changed.

Run it from an administrator terminal:

```
byteleash-probe --seconds 120 --out report.txt [--csv sends.csv]
```

`--seconds 0` runs until Ctrl+C. Findings from runs are recorded in
docs/ideas.md (Next steps).

## enforce_test/

`byteleash-enforce-test.exe` tries driverless ways to enforce Leash mode
(docs/plan.md, M1); not part of the shipped app. It uploads filler data from
this PC to a sink on a second PC on the home network, applies a WFP block
filter or a Windows QoS throttle partway through, removes it again, and
reports how many bytes the sink acknowledged each second. A second PC is
needed because QoS throttles don't apply to loopback traffic.

**WFP only, one PC:** `enforce_test\run-wfp-loopback.cmd` (administrator
terminal) runs its own sink on loopback and the four WFP tests. Results of
the first run: `docs/test-results/2026-10-26-wfp-loopback/`.

**Full test, two PCs.** On the second PC (normal terminal; allow it through
Windows Firewall on private networks when asked):

```
byteleash-enforce-test sink --port 5201
```

On the test PC, from an administrator terminal:

```
byteleash-enforce-test wfp --target <sink IP>:5201 --mode app --out wfp-app.txt
byteleash-enforce-test wfp --target <sink IP>:5201 --mode connection --out wfp-conn.txt
byteleash-enforce-test qos --target <sink IP>:5201 --out qos.txt
byteleash-enforce-test qos --target <sink IP>:5201 --reconnect --out qos-new.txt
byteleash-enforce-test job --target <sink IP>:5201 --out job.txt
byteleash-enforce-test job --target <sink IP>:5201 --reconnect --out job-new.txt
byteleash-enforce-test cleanup
```

- `wfp --mode app` blocks this exe by app ID at the ALE connect layers: does
  that cut the running upload, or only new connections? `--mode connection`
  blocks the one connection at the transport layer. `--block 60` tests a
  longer block (does the connection survive and resume?).
- `qos` steps through throttle rates in bits/s (default
  `240,1600,8000,80000,800000`; 240 bit/s = 30 B/s), using a non-persistent
  policy (`--store ActiveStore`; `localhost` is persistent).
- `job` puts the test process in a job object and steps through job
  network rate limits in **bytes/s** (default `30,200,1000,10000,100000`),
  using `JobObjectNetRateControlInformation`. The limit can be changed at
  runtime and ends with the process, so there's nothing to clean up.
- Clean-up check: during a long `wfp --block 120` run, kill it from another
  admin terminal (`taskkill /f /im byteleash-enforce-test.exe`), then run
  `cleanup`. WFP filters should be gone already (dynamic session); a killed
  `qos` run leaves its policy until `cleanup` or a reboot.
