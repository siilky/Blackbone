# Repository Guidelines

## Project Structure & Module Organization

- `src/BlackBone/` contains the C++ user-mode library, organized by process, memory, RPC, hooks, PE mapping, and symbols.
- `src/BlackBoneDrv/` contains the C kernel driver and its separate solution.
- `src/BlackBoneTest/` contains native C++ tests; `Testing/` supplies prebuilt helper executables and DLLs.
- `src/Samples/` demonstrates library usage. `src/PythonicBlackBone/` contains a ctypes-based Python wrapper and a manual memory-access example.
- `src/3rd_party/` holds bundled dependencies. `.github/workflows/` defines library/test and driver builds. Generated library outputs belong under `build/`.

## Build, Test, and Development Commands

Use a Visual Studio developer shell with MSBuild and VSTest available. Current non-XP projects select the v142 toolset; install the Windows SDK, Spectre libraries, and ATL components listed in `README.md`. Driver builds additionally require the WDK.

Run from the repository root:

```powershell
MSBuild.exe BlackBone.sln /p:CI=true /p:Platform=x64 /p:Configuration=Release
vstest.console.exe "build/x64/Release/BlackBoneTest.dll"
MSBuild.exe src/BlackBoneDrv/BlackBoneDrv.sln /p:Platform=x64 /p:Configuration=Win10Release
```

These build the library solution, run native tests, and build the driver separately. Library CI covers Win32 and x64 with `Release` and `Release(DLL)`; repeat relevant combinations. Launch `build/x64/Release/Samples.exe` for the C++ examples after reviewing their process operations. CMake files under `src/` cover the library and samples, not the native test suite.

## Code Navigation

Serena's C++ server reads `compile_commands.json` at the repository root, which Git ignores. It comes from the CMake files under `src/`, so it covers the library and samples; `src/BlackBoneTest/` is absent, and symbol references miss test call sites; search the tests with Grep. Those CMake files list sources by hand, so a source file added only to a `.vcxproj` is missing from the database. After adding or removing a source file, regenerate the database in a developer shell (see [Build and test](docs/agents/workflow.md#build-and-test)):

```powershell
cmake -G Ninja -S src -B build/compdb -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
Copy-Item build/compdb/compile_commands.json .
```

## Coding Style & Naming Conventions

Match surrounding code: four-space indentation, braces on separate lines, and spaced C++ arguments such as `Attach( pid )`. The library uses C++17, namespace `blackbone`, PascalCase types, and underscore-prefixed private members. Preserve local method naming. Keep English comments focused on non-obvious invariants. No repository formatter or linter configuration is present; avoid unrelated reformatting.

## Testing Guidelines

Use Microsoft’s C++ Unit Test Framework: add `Test<Feature>.cpp` files with `TEST_CLASS` and descriptive `TEST_METHOD` cases. Keep helpers in `Testing/` available; `Common.h` resolves their paths. Cover affected architecture and WOW64 paths. No numeric coverage threshold is configured. Treat the Python memory-access script as a manual example requiring a valid PID and address.

## Commit & Pull Request Guidelines

Commit directly to `master`; create a branch only when the user asks for one. History uses short, descriptive subjects without a mandatory prefix. Prefer action-oriented messages such as `Fix remote execution across WOW64`. Keep changes focused. In PRs, explain the behavior change, link relevant issues, and report tested architectures/configurations and any skipped checks.

## Documentation

Before creating or changing documentation, and when completing an implementation or review, follow [the documentation policy](docs/README.md).

## Agent skills

### Agent skill configuration

Before implementation, review, or delegation, read [the workflow configuration](docs/agents/workflow.md).

- Diagnosis (`diagnosing-bugs`), review (`code-review`), and audits stay read-only; continue to changes only when the user authorizes them.
- TDD is optional, including for delegated workers; use `tdd` when test-first work is requested or agreed.
- `to-spec`, `to-tickets`, `triage`, and `wayfinder` run only when the user chooses them; a small change goes straight to implementation.
- `research` and `prototype` report findings; production integration needs the user's go-ahead.
- Commit, push, and external publication require a user request, including when a skill's own steps include them.
- Ponytail governs implementation simplicity: reuse existing boundaries and add no speculative abstractions. This repository's contract clarification, agreed test coverage, review, verification, and reporting rules take precedence over Ponytail's defaults on proceeding without questions, test counts, and response length.
- After the contract is settled, use [delegated implementation](docs/agents/delegation.md) when bounded task packets can carry the work: two or more independent workstreams with exclusive file ownership, or one sizeable well-specified lane such as bulk tests or a mechanical migration. Small or inseparable changes stay with the root session. An explicit user request also selects it.

### Issue tracker

Issues and specs live as local markdown files under `.scratch/`. See `docs/agents/issue-tracker.md`.

### Domain docs

Single-context: root `GLOSSARY.md` and `docs/adr/`. See `docs/agents/domain.md`.
