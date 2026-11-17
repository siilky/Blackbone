set(BLACKBONE_TRACE "" CACHE STRING "Trace output: OFF, FILE, or CONSOLE (console and file); empty selects CONSOLE in Debug and OFF otherwise")
set_property(CACHE BLACKBONE_TRACE PROPERTY STRINGS "" OFF FILE CONSOLE)

add_library(BlackBone
    ../3rd_party/AsmJit/base/assembler.cpp
    ../3rd_party/AsmJit/base/codegen.cpp
    ../3rd_party/AsmJit/base/constpool.cpp
    ../3rd_party/AsmJit/base/containers.cpp
    ../3rd_party/AsmJit/base/cpuinfo.cpp
    ../3rd_party/AsmJit/base/cputicks.cpp
    ../3rd_party/AsmJit/base/error.cpp
    ../3rd_party/AsmJit/base/globals.cpp
    ../3rd_party/AsmJit/base/operand.cpp
    ../3rd_party/AsmJit/base/runtime.cpp
    ../3rd_party/AsmJit/base/string.cpp
    ../3rd_party/AsmJit/base/vmem.cpp
    ../3rd_party/AsmJit/base/zone.cpp
    ../3rd_party/AsmJit/x86/x86assembler.cpp
    ../3rd_party/AsmJit/x86/x86cpuinfo.cpp
    ../3rd_party/AsmJit/x86/x86inst.cpp
    ../3rd_party/AsmJit/x86/x86operand.cpp
    ../3rd_party/AsmJit/x86/x86operand_regs.cpp
    Asm/AsmHelper32.cpp
    Asm/AsmHelper64.cpp
    Asm/LDasm.c
    DriverControl/DriverControl.cpp
    LocalHook/LocalHookBase.cpp
    LocalHook/TraceHook.cpp
    ManualMap/MExcept.cpp
    ManualMap/MMap.cpp
    ManualMap/Native/NtLoader.cpp
    Misc/InitOnce.cpp
    Misc/NameResolve.cpp
    Misc/Utils.cpp
    Patterns/PatternSearch.cpp
    PE/ImageNET.cpp
    PE/PEImage.cpp
    Process/MemBlock.cpp
    Process/Process.cpp
    Process/ProcessCore.cpp
    Process/ProcessMemory.cpp
    Process/ProcessModules.cpp
    Process/RPC/RemoteExec.cpp
    Process/RPC/RemoteHook.cpp
    Process/RPC/RemoteLocalHook.cpp
    Process/RPC/RemoteMemory.cpp
    Process/Threads/Thread.cpp
    Process/Threads/Threads.cpp
    Subsystem/NativeSubsystem.cpp
    Symbols/PDBHelper.cpp
    Symbols/SymbolData.cpp
    Symbols/SymbolLoader.cpp)

target_include_directories(BlackBone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# /Gw puts each global variable in its own COMDAT, so /OPT:REF drops data that no linked code
# references, such as the AsmJit instruction tables in a consumer that never assembles code.
target_compile_options(BlackBone PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/wd4100 /wd4996 /Gw>)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_sources(BlackBone PRIVATE Syscalls/Syscall64.asm)
    target_link_libraries(BlackBone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/DIA/lib/amd64/diaguids.lib)
    set(DIA_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../DIA/x64)
else()
    target_sources(BlackBone PRIVATE
        ../3rd_party/rewolf-wow64ext/src/wow64ext.cpp
        Subsystem/Wow64Subsystem.cpp
        Subsystem/x86Subsystem.cpp
        Syscalls/Syscall32.asm)
    target_compile_options(BlackBone PRIVATE $<$<COMPILE_LANGUAGE:ASM_MASM>:/safeseh>)
    target_link_libraries(BlackBone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../3rd_party/DIA/lib/diaguids.lib)
    set(DIA_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../DIA/Win32)
endif()
target_link_libraries(BlackBone PRIVATE ole32 shlwapi)

# AsmJit's config.h derives its own linkage from these macros and does not default to static,
# so the static build defines BLACKBONE_STATIC explicitly for the library and its consumers.
# Exports.def exports the assembly syscall_stub, which the Syscall.h templates call from the consumer.
if(BUILD_SHARED_LIBS)
    target_sources(BlackBone PRIVATE DllMain.cpp Exports.def)
    target_compile_definitions(BlackBone PRIVATE BLACKBONE_EXPORTS INTERFACE BLACKBONE_IMPORTS)
else()
    target_compile_definitions(BlackBone PUBLIC BLACKBONE_STATIC)
endif()

# Public headers depend on these macros (Trace.hpp),
# so consumers must compile with the same values as the library.
if(BLACKBONE_TRACE STREQUAL "")
    target_compile_definitions(BlackBone PUBLIC $<IF:$<CONFIG:Debug>,CONSOLE_TRACE,BLACKBONE_NO_TRACE>)
elseif(BLACKBONE_TRACE STREQUAL "OFF")
    target_compile_definitions(BlackBone PUBLIC BLACKBONE_NO_TRACE)
elseif(BLACKBONE_TRACE STREQUAL "CONSOLE")
    target_compile_definitions(BlackBone PUBLIC CONSOLE_TRACE)
elseif(NOT BLACKBONE_TRACE STREQUAL "FILE")
    message(FATAL_ERROR "BLACKBONE_TRACE must be empty, OFF, FILE, or CONSOLE, got '${BLACKBONE_TRACE}'")
endif()

# PDBHelper loads msdia140.dll at run time from the executable directory.
add_custom_command(TARGET BlackBone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/$<CONFIG>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DIA_RUNTIME_DIR}/msdia140.dll ${DIA_RUNTIME_DIR}/symsrv.dll ${CMAKE_BINARY_DIR}/$<CONFIG>)
