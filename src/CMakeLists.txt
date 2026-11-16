cmake_minimum_required(VERSION 3.21)
project(BlackBone LANGUAGES C CXX ASM_MASM)

# The library API passes STL objects across the module boundary; with the static CRT
# every module owns its own heap, so freeing them in another module corrupts memory.
# The value may be a generator expression, so only a value naming no DLL runtime at all is rejected.
if(BUILD_SHARED_LIBS AND DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
    message(FATAL_ERROR "BUILD_SHARED_LIBS requires the DLL CRT, got CMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE)
add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/W3> $<$<COMPILE_LANGUAGE:C,CXX>:/sdl>)

# Whole-program optimization (/GL, /LTCG) in Release; a static library built this way links
# only with the same MSVC version, so consumers on another toolset turn it off.
if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# One directory per configuration for all binaries: tests load BlackBone.dll and msdia140.dll
# from their own directory, and Common.h finds Testing/ three directories above it,
# so the build tree must be <root>/build/<name> (see CMakePresets.json).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)

add_subdirectory(BlackBone)
add_subdirectory(Samples)
add_subdirectory(BlackBoneTest)
