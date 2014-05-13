#!/bin/bash
#required:
# OpenSceneGraph
# TurboJPEG
# libwebsockets
echo "Building osg-stream.exe - stream an OpenGL context over WebSockets..."
OSG_INCLUDES="/usr/local/osg/include"; \
OSG_LIB_DIR="/usr/local/osg/lib";\
TJ_INCLUDES="/opt/libjpeg-turbo/include"; \
TJ_LIB_DIR="/opt/libjpeg-turbo/lib"; \
WS_INCLUDES="/usr/local/libwebsockets/include"; \
WS_LIB_DIR="/usr/local/libwebsockets/lib"; \
CC=g++; \
SOURCE_PATH=/Users/uvaretto/projects/websocketsplus; \
OUT_PATH=/Users/uvaretto/projects/websocketsplus/tmp-build; \
$CC -std=c++11  -stdlib=libc++ $SOURCE_PATH/src/examples/osg/osg-stream.cpp \
$SOURCE_PATH/src/mimetypes.cpp $SOURCE_PATH/src/http.cpp \
$SOURCE_PATH/src/WebSocketService.cpp \
-I $OSG_INCLUDES -I $TJ_INCLUDES -I $WS_INCLUDES \
-L $OSG_LIB_DIR -L $TJ_LIB_DIR -L $WS_LIB_DIR \
-lOpenThreads -losgDB -losgGA -losgManipulator -losg -losgUtil -losgViewer \
-losgText -framework OpenGL -lturbojpeg -lwebsockets -O3 -pthread \
-o $OUT_PATH/osg-stream.exe
