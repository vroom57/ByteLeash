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
