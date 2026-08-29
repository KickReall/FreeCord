add_library(common STATIC
    src/TcpFramer.cpp
    src/PlatformSocket.cpp
)

target_include_directories(common PUBLIC include)
target_compile_features(common PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(common PUBLIC ws2_32)
endif()

find_package(Threads REQUIRED)
target_link_libraries(common PUBLIC Threads::Threads)