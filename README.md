# SuperMarioAdvance4Recomp

Static recompilation target for **Super Mario Advance 4: Super Mario Bros. 3**
(USA/Australia, AX4E revision 1) using a dedicated `gbarecomp` worktree. ROM,
BIOS, generated sources, build outputs, saves, logs, and captures remain local
and ignored.

The acceptance gate runs the passive attract sequence and a deterministic
bounded-input fuzz route with interpreter, cached overlay, and self-healing
fallback disabled:

```powershell
pwsh tools/test-attract-fuzz.ps1
```

Passing requires zero dispatch misses, interpreted instructions, unmapped
accesses, and unhandled I/O, plus a non-empty final-frame capture.
