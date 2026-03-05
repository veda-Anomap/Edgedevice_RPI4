cmake_minimum_required(VERSION 3.10)
project(stm_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(stm_bridge
    src/main.cpp
    src/uart_port.cpp
    src/stm32_proto.cpp
    src/server_client.cpp
    src/bridge.cpp
)

target_include_directories(stm_bridge PRIVATE src)
target_link_libraries(stm_bridge PRIVATE pthread)
