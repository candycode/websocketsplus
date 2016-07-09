cmake_minimum_required(VERSION 3.5)
project(websocketsplus)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(WS_SOURCES src/WebSocketService.cpp src/mimetypes.cpp src/http.cpp)

include_directories(/usr/local/libwebsockets/include)
link_directories(/usr/local/libwebsockets/lib)
link_libraries(websockets)
add_executable(example src/examples/example.cpp ${WS_SOURCES})