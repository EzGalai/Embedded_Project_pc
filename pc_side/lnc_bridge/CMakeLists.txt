add_executable(lnc_bridge
    main.cpp
    transport_serial.cpp
    transport_serial_backed.cpp
    bridge_server.cpp
)
target_link_libraries(lnc_bridge PRIVATE protocol)
