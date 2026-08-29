find_package(SQLiteCpp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(auth_service
    main.cpp
    UserRepository.cpp
    PasswordHasher.cpp
)

target_link_libraries(auth_service PRIVATE common SQLiteCpp OpenSSL::Crypto)