# Engineering workflow

Read this before implementation, review, or delegation. [Agent skill configuration](../../AGENTS.md#agent-skill-configuration) selects the route; this document owns how work runs inside it.

## Build and test

A fresh shell has no CMake, Ninja, MSBuild, VSTest, or compiler on `PATH`. `scripts/build.ps1` activates the Visual Studio developer shell itself; run it as described in [Build, Test, and Development Commands](../../AGENTS.md#build-test-and-development-commands). Any other build, test, or CMake command needs the activation in the same shell process; use `-Arch x86` for 32-bit builds:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
```

The activation prints `'vswhere.exe' is not recognized`; that line is harmless. A second activation in the same process keeps the first architecture, so the preset would configure the wrong build directory: use a new process per architecture, as the script does.

Run one build at a time per build directory: a build rewrites `build/<arch>-<preset>/`, and a test run loads the DLLs from it. Different architectures and presets use separate directories. Run one VSTest process at a time on the machine, whatever its build directory: tests share fixed paths under `%TEMP%` (the `Symbols` stores, for example), so concurrent runs fail each other. Run each build or test as one call whose timeout covers it.

The `Symbols` tests require the fingerprint catalog ([ADR 0004](../adr/0004-ntdll-fingerprint-catalog.md)) to cover the installed x86 and x64 `ntdll.dll` and matching PDBs for them; after a Windows update, [regenerate the catalog](#regenerate-the-symbol-catalog) before treating a `SymbolsMatch` failure as a code defect. The test module allows the network once ([ADR 0003](../adr/0003-symbols-network-on-request.md)), so the default search downloads the PDBs from Microsoft's symbol server into `%TEMP%\Symbols`; an explicit `_NT_SYMBOL_PATH` selects the symbol search environment instead. The library passes the current `_NT_SYMBOL_PATH` to DIA explicitly, but DIA also searches the value the process started with, so changing the variable in a test adds stores to the search without removing the original ones. A PDB-loading failure fails `SymbolsMatch`, which names each missing symbol, so check symbol access before attributing a mapping failure to the code. Symbol compatibility must be checked against the installed `ntdll` files, whose version can differ from the OS build.

Pattern scans use private x64 helpers with known bytes, including when the test host is x86; window-search tests create their own hidden window. The default run with driver-dependent tests skipped does not require Explorer, but still requires access to create processes, remote threads, debug events, and windows. If a restricted execution environment changes those operations, repeat the same focused VSTest invocation in a normal user session before classifying a repository failure. Driver-dependent tests remain opt-in through the build script and still require Explorer.

### Regenerate the symbol catalog

`SymbolCatalogGen` rebuilds `src/BlackBone/Symbols/SymbolCatalogData.cpp` from its inputs only; it does not merge with the existing table, so every build the catalog should keep covering must be an input. It builds in the static presets and finds PDBs only in local stores (`%TEMP%\symbols` and `srv*` stores of `_NT_SYMBOL_PATH`). From the repository root, after building `build/x64-static-mt`:

```powershell
./scripts/Get-NtdllBuilds.ps1 -Store <store>
$env:_NT_SYMBOL_PATH = "srv*<store>"
build/x64-static-mt/Release/SymbolCatalogGen.exe src/BlackBone/Symbols/SymbolCatalogData.cpp <store>\ntdll.dll C:\Windows\System32\ntdll.dll C:\Windows\SysWOW64\ntdll.dll
```

The download is the only step that uses the network; `Get-Help ./scripts/Get-NtdllBuilds.ps1 -Detailed` owns its parameters. In the diff, check the `covers` comments for builds that dropped out, and before committing review the calling convention of each new entry and of each entry that newly covers a release, because equal leading bytes do not prove an equal function body ([ADR 0004](../adr/0004-ntdll-fingerprint-catalog.md), decision 4). For the review, open a store image with `cdb -z <store>\ntdll.dll\<key>\ntdll.dll -y srv*<store>` and disassemble with `u ntdll!<symbol>`: `uf` interleaves blocks of other functions in address order, and cdb drops the rest of a `-c` command list after a symbol it cannot resolve.

## Local environment

Each user describes their own agent setup in `docs/agents/environment.md`, which Git ignores. Read it when present; without it, use the client's equivalents of the generic operations named in these documents. It covers:

- the agent client and how [delegation](delegation.md#roles) roles map onto it: where role definitions live, how a worker is dispatched with a fresh context, model, and effort, how an existing worker is continued, and which model serves each tier;
- code-navigation tools and their known blind spots in this repository;
- installed skills or plugins that perform the activities in [Agent skill configuration](../../AGENTS.md#agent-skill-configuration), and any defaults of theirs that these documents override;
- shell and tool-harness specifics, such as available shells and whether each command starts a new process.

The file adds client mechanics only; repository rules stay in the committed documents.

## Workspace

- Work in the current checkout on its current branch. Create a branch or worktree only when the user asks.
- Write scratch output (redirected command output, probe listings) under a task-named directory in the system temp directory (`%TEMP%\<task>\`). Before reporting completion, remove scratch that no report cites.
- Edit files with the editing tools. A script edits files only for a bulk, uniform migration; state the reason, bound the affected files, and inspect the resulting diff. When a simple edit turns into debugging a script's quoting or matching, finish it with the editing tools.
- `.gitattributes` normalizes line endings; leave them as the editor writes them.

## Session boundaries

When a task needs several change blocks, or decisions that must survive the session, create `.scratch/<feature>/plan.md` before the first block. It holds the user's decisions, a block table (ID, work, status, commit) updated in place, and a `Resume here` section with running agents and the next step. A single-block task needs no plan.

When a block is committed or handed off, propose continuing the next block in a new session from the plan; every turn re-reads the whole conversation, so a long session makes each later step more expensive.

## Review

Before implementation, record the base revision and the existing working-tree changes. A completion review covers the task's final patch: committed, staged, unstaged, and relevant untracked files. This overrides a review skill's default of comparing committed changes only. Preserve and exclude unrelated changes. An audit of named files or an area covers the requested scope in full; it needs no base revision.

For a small localized change, one compact reviewer covers both Standards and Spec, overriding a review skill's default of separate agents; dispatch the `reviewer` role. For a large or cross-cutting change, ask the user which review they want and state each option's agent count. Every review prompt states whether nested review skills and subagents are allowed.

Every review, by any reviewer, applies this checklist:

- Both architectures of every touched path: `USE64` / `USE32` branches, `ptr_t` versus 32-bit types where a remote address crosses process bitness, structure layouts read from a target of the other bitness, and WOW64 handling.
- Consumers in `src/BlackBoneTest/`, found with a text search rather than symbol references alone.
- Test binaries run through `vstest.console.exe` after the developer-shell activation in [Build and test](#build-and-test).

Sort each accepted finding:

- **Fix now**: a reachable defect that is cheap to fix, such as a guard, a one-line check, or a small test. "Pre-existing" alone is no reason to defer it.
- **Ask**: the fix needs a product decision; take it to the user with a recommendation.
- **Risk**: the path is unreachable or unsupported; record it in the report or plan.

A finding that calls existing behavior a defect becomes a contract only after the documents, the tests, and `git log -S` show no deliberate rule for that behavior. This fork inherits most behavior from upstream `DarthTon/Blackbone`; its history counts as deliberate rule too.

Run the repository's required checks on the final state, and report automated checks separately from live verification.

For delegated implementation, follow [the delegation integration rules](delegation.md#integrate-and-verify); its integrated-patch review and final verifier satisfy this section.
