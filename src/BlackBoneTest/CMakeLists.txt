# The Microsoft C++ Unit Test Framework ships with Visual Studio next to the compiler, under VC/Auxiliary/VS/UnitTest.
# CppUnitTestCommon.h links its library through #pragma comment with an architecture-relative path.
string(REGEX REPLACE "/Tools/MSVC/.*$" "" VC_DIR "${CMAKE_CXX_COMPILER}")
set(UNITTEST_DIR ${VC_DIR}/Auxiliary/VS/UnitTest)

add_library(BlackBoneTest MODULE
    TestAsmJit.cpp
    TestAsmVariant.cpp
    TestBasic.cpp
    TestDriver.cpp
    TestGuard.cpp
    TestLocalHook.cpp
    TestManualMap.cpp
    TestModules.cpp
    TestMultiPtr.cpp
    TestPatternScan.cpp
    TestRemoteCall.cpp
    TestRemoteHook.cpp
    TestRemoteMemory.cpp
    TestSymbols.cpp
    TestSyscall.cpp)

option(BLACKBONE_TEST_SKIP_DRIVER "Mark the tests that need the BlackBone driver as ignored (CI_BUILD)" OFF)
if(BLACKBONE_TEST_SKIP_DRIVER)
    target_compile_definitions(BlackBoneTest PRIVATE CI_BUILD)
endif()

target_include_directories(BlackBoneTest PRIVATE ${UNITTEST_DIR}/include)
target_link_directories(BlackBoneTest PRIVATE ${UNITTEST_DIR}/lib)
target_link_libraries(BlackBoneTest PRIVATE BlackBone)
