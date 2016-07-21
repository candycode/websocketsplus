cmake_minimum_required(VERSION 3.5)
project(websocketsplus)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(WS_SOURCES src/WebSocketService.cpp src/mimetypes.cpp src/http.cpp)

include_directories(/usr/local/libwebsockets2/include)
link_directories(/usr/local/libwebsockets2/lib)
link_libraries(websockets)
add_executable(example src/examples/example.cpp ${WS_SOURCES})
add_executable(example-streaming
               src/examples/example-streaming.cpp ${WS_SOURCES})
include_directories(src)
add_executable(reqrep src/examples/patterns/req-rep/sync-req-rep.cpp ${WS_SOURCES})
add_executable(reqrep-async src/examples/patterns/req-rep/async-req-rep.cpp ${WS_SOURCES})