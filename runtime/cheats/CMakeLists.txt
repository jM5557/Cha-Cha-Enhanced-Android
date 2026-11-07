# Cheats: Cemu-Enhanced / Citra cheat files, Gateway codes, patched code through the interpreter (cheats.h).
add_library(mh3u_cheats STATIC cheats.cpp cheats.h)
target_include_directories(mh3u_cheats PUBLIC .)
target_link_libraries(mh3u_cheats PUBLIC mh3u_ppc_context mh3u_recomp_support)

# the bundled MH3U US cheat file (assets/cheats), written to <data>/cheats on the first start when there is none
set(bundled_cheats ${MH3U_REPO_ROOT}/assets/cheats/0005000010118300.txt)
set(bundled_cpp ${CMAKE_CURRENT_BINARY_DIR}/bundled_cheats.cpp)
add_custom_command(
  OUTPUT ${bundled_cpp}
  COMMAND ${CMAKE_COMMAND} -DIN=${bundled_cheats} -DOUT=${bundled_cpp} -P ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  DEPENDS ${bundled_cheats} ${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake
  COMMENT "Embedding the bundled cheat file")
target_sources(mh3u_cheats PRIVATE ${bundled_cpp})

# the recompiler's self-test functions as the "game": a cheat patches one, the interpreter runs it
add_executable(cheats_test cheats_test.cpp)
target_link_libraries(cheats_test PRIVATE mh3u_cheats mh3u_selftest_funcs mh3u_recomp_core)
add_test(NAME cheats COMMAND cheats_test ${bundled_cheats})
