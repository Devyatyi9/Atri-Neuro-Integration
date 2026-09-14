# Tools

The most important workflows are grouped by purpose:

```powershell
# Format and static analysis
.\Tools\quality\format.ps1
.\Tools\quality\format.ps1 -Check
.\Tools\quality\clang-tidy.ps1

# Runtime smoke checks
.\Tools\runtime\neuro_tcp_client.ps1 -Code "Neuro.getVersion()"
.\Tools\runtime\wait-for-title.ps1

# Reverse engineering and local indexes
.\Tools\research\decompile_tjs.ps1
```

- `quality/` contains formatting and static-analysis checks.
- `runtime/` contains game deployment and Neuro TCP smoke-test tools.
- `research/` contains local reverse-engineering and scenario indexing tools.
- `tjs/` contains captured and restored TJS scripts.
- `tjscheck.exe` validates TJS syntax before runtime testing.
