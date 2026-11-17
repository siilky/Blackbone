# Windows 10 is the minimum supported version

Status: accepted.

## Context

The library, its tests, and the driver carried code paths for Windows XP, Vista, 7, 8, and 8.1: separate loader structures and inverted function table layouts, older API set schemas, calling conventions that changed in 8.1, a Windows 7 patch of `LdrpFindOrMapDll` located by a byte pattern, an XP build option, and per-version driver configurations with their own kernel offsets. None of these paths can be tested in this repository, and the symbol sources rework replaces the hand-written byte patterns that the Windows 7 patch depends on.

## Decision

1. Windows 10 is the oldest supported version for the library, the tests, the samples, and the driver.
2. Code that runs only on older versions is deleted, not kept behind version checks; a condition that is constant on Windows 10 is simplified. Behavior on Windows 10 and later does not change.
3. The XP build option `BLACKBONE_XP_BUILD` and its `XP_BUILD` define are removed.
4. The driver keeps one configuration set and the file name `BlackBoneDrv10.sys`, so existing deployments of that file keep working.

## Consequences

- Distinctions between Windows 10 releases (RS1 and later builds) remain where the code needs them.
- Injection from a WOW64 process into its 64-bit part on Windows 7 loses the `LdrpFindOrMapDll` patch together with the rest of Windows 7 support.
