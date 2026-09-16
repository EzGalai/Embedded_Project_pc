# cc_core (defined in ../central_computer/CMakeLists.txt) is genuinely
# reused here for CentralComputer's LNC connectivity — see central_computer.h's
# note. add_subdirectory order in the top-level CMakeLists.txt puts
# central_computer/ before oop_fleet/, so this target already exists.
add_executable(oop_fleet
    main.cpp
    submarine.cpp
    research_submarine.cpp
    combat_submarine.cpp
    fleet.cpp
    central_computer.cpp
)
target_include_directories(oop_fleet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../central_computer)
target_link_libraries(oop_fleet PRIVATE cc_core)
# Baked in as an absolute path so it resolves correctly regardless of which
# directory oop_fleet happens to be launched from — see main.cpp's note.
get_filename_component(OOP_DEFAULT_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data" ABSOLUTE)
target_compile_definitions(oop_fleet PRIVATE DEFAULT_DATA_DIR="${OOP_DEFAULT_DATA_DIR}")
