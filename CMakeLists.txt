cmake_minimum_required(VERSION 3.20)
project(FreeCord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Общая библиотека: framing протокола, структуры сообщений, сетевые утилиты
add_subdirectory(common)

# config.json читается сервисами из их рабочей директории при запуске —
# копируем его в корень build-директории (туда же, где документированный
# запуск ожидает найти *.exe и куда ложатся *.db).
add_custom_target(copy_config ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.json
        ${CMAKE_BINARY_DIR}/config.json
    COMMENT "Copying config.json to build directory"
)

# Сервисы
add_subdirectory(services/auth)
add_subdirectory(services/room)
add_subdirectory(services/message)
add_subdirectory(services/gateway)
add_subdirectory(tools/test_client)