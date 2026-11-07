add_library(mh3u_core STATIC guest_memory.cpp guest_memory.h)
target_include_directories(mh3u_core PUBLIC .)

add_executable(guest_memory_test guest_memory_test.cpp)
target_link_libraries(guest_memory_test PRIVATE mh3u_core)
add_test(NAME core_guest_memory COMMAND guest_memory_test)
