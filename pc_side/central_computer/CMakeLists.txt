# cc_core: the LNC-facing modules genuinely reused by oop_fleet's
# CentralComputer class (see oop_fleet/central_computer.h's note) — a real
# shared library target here, rather than the Makefile's approach of
# recompiling these same .cpp files a second time under oop_fleet/.
add_library(cc_core STATIC
    lnc_link_client.cpp
    management_command.cpp
    log.cpp
    data_collection.cpp
)
target_include_directories(cc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cc_core PUBLIC protocol Threads::Threads)

add_executable(central_computer main.cpp comm_gs.cpp)
target_link_libraries(central_computer PRIVATE cc_core)
# Baked in as an absolute path so it resolves correctly regardless of which
# directory central_computer happens to be launched from — see main.cpp's note.
get_filename_component(CC_DEFAULT_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data" ABSOLUTE)
target_compile_definitions(central_computer PRIVATE DEFAULT_DATA_DIR="${CC_DEFAULT_DATA_DIR}")
