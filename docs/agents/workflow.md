# Engineering workflow

Read this before implementation, review, or delegation. [Agent skill configuration](../../AGENTS.md#agent-skill-configuration) selects the route; this document owns how work runs inside it.

## Build and test

Each PowerShell tool call starts a fresh shell without MSBuild, VSTest, or the compiler on `PATH`. Activate the Visual Studio developer shell in the same call as every build, test, or CMake command:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
```

The activation prints `'vswhere.exe' is not recognized`; that line is harmless. Then run the commands from [Build, Test, and Development Commands](../../AGENTS.md#build-test-and-development-commands) from the repository root.

Run one MSBuild or VSTest process at a time in the checkout. Builds share `obj/` and `build/<platform>/<configuration>/`, and a test run loads the DLLs a concurrent build rewrites. Run each build or test as one foreground call whose timeout covers it.

A compiler error in `atlbase.h`, `atlcomcli.h`, or a missing Spectre library is a missing Visual Studio component (see the prerequisites in `README.md`), not a repository defect: report it as an environment blocker.

## Workspace

- Work in the current checkout on its current branch. Create a branch or worktree only when the user asks.
- Write scratch output (redirected command output, probe listings) under a task-named directory in the system temp directory: `$env:TEMP\<task>\` in PowerShell, `$TEMP/<task>/` in Git Bash. Before reporting completion, remove scratch that no report cites.
- Edit files with the editing tools. A script edits files only for a bulk, uniform migration; state the reason, bound the affected files, and inspect the resulting diff. When a simple edit turns into debugging a script's quoting or matching, finish it with the editing tools.
- `.gitattributes` normalizes line endings; leave them as the editor writes them.

## Session boundaries

When a task needs several change blocks, or decisions that must survive the session, create `.scratch/<feature>/plan.md` before the first block. It holds the user's decisions, a block table (ID, work, status, commit) updated in place, and a `Resume here` section with running agents and the next step. A single-block task needs no plan.

When a block is committed or handed off, propose continuing the next block in a new session from the plan; every turn re-reads the whole conversation, so a long session makes each later step more expensive.

## Review

Before implementation, record the base revision and the existing working-tree changes. A completion review covers the task's final patch: committed, staged, unstaged, and relevant untracked files. This overrides `code-review`'s comparison of committed changes only. Preserve and exclude unrelated changes. An audit of named files or an area covers the requested scope in full; it needs no base revision.

For a small localized change, one compact reviewer covers both Standards and Spec, overriding `code-review`'s two-agent default; dispatch the `reviewer` agent. For a large or cross-cutting change, ask the user which review they want and state each option's agent count. Every review prompt states whether nested review skills and subagents are allowed.

Every review, by any reviewer, applies this checklist:

- Both architectures of every touched path: `USE64` / `USE32` branches, `ptr_t` versus 32-bit types where a remote address crosses process bitness, structure layouts read from a target of the other bitness, and WOW64 handling.
- Consumers in `src/BlackBoneTest/`, found with Grep, because Serena's references miss them.
- Test binaries run through `vstest.console.exe` after the developer-shell activation in [Build and test](#build-and-test).

Sort each accepted finding:

- **Fix now**: a reachable defect that is cheap to fix, such as a guard, a one-line check, or a small test. "Pre-existing" alone is no reason to defer it.
- **Ask**: the fix needs a product decision; take it to the user with a recommendation.
- **Risk**: the path is unreachable or unsupported; record it in the report or plan.

A finding that calls existing behavior a defect becomes a contract only after the documents, the tests, and `git log -S` show no deliberate rule for that behavior. This fork inherits most behavior from upstream `DarthTon/Blackbone`; its history counts as deliberate rule too.

Run the repository's required checks on the final state, and report automated checks separately from live verification.

For delegated implementation, follow [the delegation integration rules](delegation.md#integrate-and-verify); its integrated-patch review and final verifier satisfy this section.
