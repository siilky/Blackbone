# Blackbone

### Windows memory library

## Features

- **x86 and x64 support**

**Process interaction**
 - Manage PEB32/PEB64
 - Manage process through WOW64 barrier

**Process Memory**
 - Allocate and free virtual memory
 - Change memory protection
 - Read/Write virtual memory 

**Process modules**
 - Enumerate all (32/64 bit) modules loaded. Enumerate modules using Loader list/Section objects/PE headers methods.
 - Get exported function address
 - Get the main module
 - Unlink module from loader lists
 - Inject and eject modules (including pure IL images)
 - Inject 64bit modules into WOW64 processes
 - Manually map native PE images

**Threads**
 - Enumerate threads
 - Create and terminate threads. Support for cross-session thread creation.
 - Get thread exit code
 - Get main thread
 - Manage TEB32/TEB64
 - Join threads
 - Suspend and resume threads
 - Set/Remove hardware breakpoints

**Pattern search**
 - Search for arbitrary pattern in local or remote process
 
**Remote code execution**
 - Execute functions in remote process
 - Assemble own code and execute it remotely
 - Support for cdecl/stdcall/thiscall/fastcall conventions
 - Support for arguments passed by value, pointer or reference, including structures
 - FPU types are supported
 - Execute code in new thread or any existing one
 
**Remote hooking**
 - Hook functions in remote process using int3 or hardware breakpoints
 - Hook functions upon return
 
**Manual map features**
 - x86 and x64 image support
 - Mapping into any arbitrary unprotected process
 - Section mapping with proper memory protection flags
 - Image relocations (only 2 types supported. I haven't seen a single PE image with some other relocation types)
 - Imports and Delayed imports are resolved
 - Bound import is resolved as a side effect, I think
 - Module exports
 - Loading of forwarded export images
 - Api schema name redirection
 - SxS redirection and isolation
 - Activation context support
 - Dll path resolving similar to native load order
 - TLS callbacks. Only for one thread and only with PROCESS_ATTACH/PROCESS_DETACH reasons.
 - Static TLS
 - Exception handling support (SEH and C++)
 - Adding module to some native loader structures(for basic module api support: GetModuleHandle, GetProcAdress, etc.)
 - Security cookie initialization
 - C++/CLI images are supported
 - Image unloading 
 - Increase reference counter for import libraries in case of manual import mapping
 - Cyclic dependencies are handled properly
 
**Driver features**
- Allocate/free/protect user memory
- Read/write user and kernel memory
- Disable permanent DEP for WOW64 processes
- Change process protection flag
- Change handle access rights
- Remap process memory
- Hiding allocated user-mode memory
- User-mode dll injection and manual mapping
- Manual mapping of drivers

## Requirements

- Windows 10 or later, for building and at run time ([ADR 0002](docs/adr/0002-windows-10-minimum.md))
- Visual Studio 2017 15.7 or higher with the C++ desktop workload, including the C++ unit test framework (`VC/Auxiliary/VS/UnitTest`), which the native tests build against
- CMake 3.21 or higher and Ninja
- Windows SDK 10.0.17134 or higher
- WDK 10.0.17134 or higher (driver only)

## Building

The library, samples, and tests build with CMake from a Visual Studio developer shell; its target architecture (x64 or x86) selects the build. From the repository root:

```
cmake -S src --preset static-mt
cmake --build build/x64-static-mt --config Release
```

`scripts/build.ps1` (PowerShell 7) wraps these steps: it activates the developer shell, builds one or more architectures and presets, and with `-Test` runs the native tests. For example, `./scripts/build.ps1 -Arch x64,x86 -Preset static-mt,static-md,dll -Test` builds and tests every combination. Run it from a shell without an active developer environment to build several architectures; `Get-Help ./scripts/build.ps1 -Detailed` lists its parameters.

Presets choose the library linkage and the C runtime; tests and samples use the same settings as the library:

| Preset | Library | CRT |
|---|---|---|
| `static-mt` | static | static (`/MT`, `/MTd`) |
| `static-md` | static | DLL (`/MD`, `/MDd`) |
| `dll` | DLL | DLL (`/MD`, `/MDd`) |

Outputs go to `build/<arch>-<preset>/<config>/`. The same choice is available without presets through `BUILD_SHARED_LIBS` and `CMAKE_MSVC_RUNTIME_LIBRARY`; the tests find the `Testing/` helpers only when the build tree is a direct subdirectory of `build/`. A DLL with the static CRT is rejected, because the library API passes STL objects between modules.

All targets compile with `/sdl`. Release uses whole-program optimization (`/GL`, `/LTCG`); a static library built with it links only with the same MSVC version, so a consumer on another toolset configures with `-DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF` (`scripts/build.ps1 -NoLtcg`).

Options:

- `BLACKBONE_TRACE`: `OFF`, `FILE`, or `CONSOLE` (console and file). Empty, the default, selects `CONSOLE` in Debug and `OFF` otherwise.
- `BLACKBONE_TEST_SKIP_DRIVER`: mark the tests that need the BlackBone driver as ignored.

The driver builds separately with `src/BlackBoneDrv/BlackBoneDrv.sln`.

## License
Blackbone is licensed under the MIT License. Dependencies are under their respective licenses.

![Library](https://github.com/DarthTon/Blackbone/workflows/Library/badge.svg?branch=master) ![Driver](https://github.com/DarthTon/Blackbone/workflows/Driver/badge.svg?branch=master)
