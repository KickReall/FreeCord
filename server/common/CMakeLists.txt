add_library(common STATIC
    src/TcpFramer.cpp
    src/PlatformSocket.cpp
    src/Config.cpp
    src/SqlFile.cpp
    src/MigrationRunner.cpp
    src/Transport.cpp
    src/TlsContext.cpp
    src/TlsTransport.cpp
)

target_include_directories(common PUBLIC include)
target_compile_features(common PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(common PUBLIC ws2_32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(common PUBLIC Threads::Threads)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)

find_package(SQLiteCpp CONFIG REQUIRED)
target_link_libraries(common PUBLIC SQLiteCpp)

find_package(OpenSSL REQUIRED)
target_link_libraries(common PUBLIC OpenSSL::SSL OpenSSL::Crypto)