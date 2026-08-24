cmake_minimum_required(VERSION 3.20)
project(FreeCord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Общая библиотека: framing протокола, структуры сообщений, сетевые утилиты
add_subdirectory(common)

# Сервисы
add_subdirectory(services/auth)
add_subdirectory(services/room)
add_subdirectory(services/message)
add_subdirectory(services/gateway)