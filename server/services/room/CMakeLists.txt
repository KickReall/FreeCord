find_package(SQLiteCpp CONFIG REQUIRED)

add_executable(room_service
    main.cpp
    RoomRepository.cpp
)

target_link_libraries(room_service PRIVATE common SQLiteCpp)