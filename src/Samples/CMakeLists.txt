add_executable(Samples Main.cpp ManualMap.cpp)
target_link_libraries(Samples PRIVATE BlackBone)

add_executable(MinimalSample MinimalSample.cpp)
target_link_libraries(MinimalSample PRIVATE BlackBone)
