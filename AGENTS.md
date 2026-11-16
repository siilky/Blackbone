# Repository Guidelines

## Project Structure & Module Organization

- `src/BlackBone/` contains the C++ user-mode library, organized by process, memory, RPC, hooks, PE mapping, and symbols.
- `src/BlackBoneDrv/` contains the C kernel driver and its separate solution.
- `src/BlackBoneTest/` contains native C++ tests; `Testing/` supplies prebuilt helper executables and DLLs.
- `src/Samples/` demonstrates library usage. `src/PythonicBlackBone/` contains a ctypes-based Python wrapper and a manual memory-access example.
- `src/3rd_party/` holds bundled dependencies. `.github/workflows/` defines library/test and driver builds. `scripts/` holds build tooling. Generated library outputs belong under `build/`.

## Build, Test, and Development Commands

The library, samples, and native tests build with CMake; the driver keeps its MSBuild solution. Install the components listed in [Requirements](README.md#requirements). Driver builds additionally require the WDK.

Run from the repository root in PowerShell 7:

```powershell
./scripts/build.ps1 -Test
./scripts/build.ps1 -Arch x64,x86 -Preset static-mt,static-md,dll -Test
MSBuild.exe src/BlackBoneDrv/BlackBoneDrv.sln /p:Platform=x64 /p:Configuration=Win10Release
```

The script builds `Release` and runs the native tests with the driver-dependent tests ignored; its help (`Get-Help ./scripts/build.ps1 -Detailed`) owns the parameters. On a test-host crash, `/Blame` records the running test under `build/<arch>-<preset>/TestResults/`. MSBuild for the driver needs the developer shell activated as in [Build and test](docs/agents/workflow.md#build-and-test). [Building](README.md#building) owns the presets and options. Library CI runs the script for x86 and x64 with every preset; repeat relevant combinations. Launch `build/x64-static-mt/Release/Samples.exe` for the C++ examples after reviewing their process operations.

## Code Navigation

Serena's C++ server reads `compile_commands.json` at the repository root, which Git ignores. It comes from the CMake files under `src/` and covers the library, samples, and tests. Those CMake files list sources by hand: add a new source file to its `CMakeLists.txt`, then regenerate the database in a developer shell (see [Build and test](docs/agents/workflow.md#build-and-test)):

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
