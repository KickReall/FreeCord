add_executable(gateway_service
    main.cpp
    SessionManager.cpp
)

target_link_libraries(gateway_service PRIVATE common)