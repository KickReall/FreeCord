add_library(common STATIC
    src/placeholder.cpp
)

target_include_directories(common PUBLIC include)
target_compile_features(common PUBLIC cxx_std_20)