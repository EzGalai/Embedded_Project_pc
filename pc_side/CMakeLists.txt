# pc_side/CMakeLists.txt — top-level PC-side build (PROJECT_PLAN.md §7/§8).
# Builds lnc_bridge, central_computer, ground_station, and oop_fleet.
#
# Existing per-directory Makefiles keep working too, for quick single-target
# edit/rebuild cycles during development — this is the primary, documented
# build for the project as a whole (see §8's run commands).

cmake_minimum_required(VERSION 3.16)
project(SubmarineMonitoringSystem C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

# Shared TLV codec (common/protocol.c) — one static library, linked by every
# executable below, instead of each recompiling it separately (the Makefiles'
# approach, kept there for simplicity).
add_library(protocol STATIC ${CMAKE_CURRENT_SOURCE_DIR}/../common/protocol.c)
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common)

find_package(Threads REQUIRED)

add_subdirectory(lnc_bridge)
add_subdirectory(central_computer)
add_subdirectory(ground_station)
add_subdirectory(oop_fleet)
