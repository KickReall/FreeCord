add_library(common STATIC
    src/TcpFramer.cpp
)

target_include_directories(common PUBLIC include)
target_compile_features(common PUBLIC cxx_std_20)
target_link_libraries(common PUBLIC ws2_32)