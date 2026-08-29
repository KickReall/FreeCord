find_package(SQLiteCpp CONFIG REQUIRED)

add_executable(message_service
    main.cpp
    MessageRepository.cpp
)

target_link_libraries(message_service PRIVATE common SQLiteCpp)