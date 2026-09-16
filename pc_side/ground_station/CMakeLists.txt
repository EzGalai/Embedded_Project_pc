add_executable(ground_station main.cpp gs_queries.cpp)
target_link_libraries(ground_station PRIVATE protocol)
