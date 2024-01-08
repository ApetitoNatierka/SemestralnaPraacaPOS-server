cmake_minimum_required(VERSION 3.18)
project(server C)

set(CMAKE_C_STANDARD 11)

add_executable(server main.c pos_sockets/active_socket.c pos_sockets/passive_socket.c  pos_sockets/char_buffer.c)

target_link_libraries(server pthread)