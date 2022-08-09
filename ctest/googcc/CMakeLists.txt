project(GoogCC)
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
MESSAGE(STATUS "#########################")
MESSAGE(STATUS ${PROJECT_NAME})
ADD_DEFINITIONS(-std=c++11 )

INCLUDE_DIRECTORIES(
        /usr/local/include
        /Users/chensong/Work/mediasoup/cmediasoup/deps/libwebrtc/libwebrtc
        /Users/chensong/Work/mediasoup/cmediasoup/deps/libwebrtc/deps/abseil-cpp
        /Users/chensong/Work/mediasoup/cmediasoup/deps/libwebrtc/deps/abseil-cpp/abseil-cpp
        #					${EXTRA_INC_DIR}/include
        #					${EXTRA_INC_DIR}/include/mysql
        #					${PROJECT_SOURCE_DIR}/clib
        #					${PROJECT_SOURCE_DIR}/common
        #					${PROJECT_SOURCE_DIR}/common/cmsg
        #					${PROJECT_SOURCE_DIR}/common/cmessage
        #					#${PROJECT_SOURCE_DIR}/
        ${PROJECT_SOURCE_DIR}
)
#/Users/chensong/Work/mediasoup/cmediasoup/out/Release
file(GLOB ALL_C_SRCS
        ./*.cpp
        ./*.h
        )

LINK_DIRECTORIES(
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib64
        /Users/chensong/Work/mediasoup/cmediasoup/out/Release
        #/usr/lib64/mysql/
        #/usr/lib/x86_64-linux-gnu
        #${PROJECT_SOURCE_DIR}/linux/lib
)
set(ALL_LIB  	pthread
        libwebrtc.a
        libabseil.a
        )
add_executable(${PROJECT_NAME} ${ALL_C_SRCS})
target_link_libraries(${PROJECT_NAME} ${ALL_LIB})