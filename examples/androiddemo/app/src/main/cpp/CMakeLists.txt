cmake_minimum_required(VERSION 3.22.1)
project("yubydemo")

set(YUBY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")

add_library(yuby SHARED IMPORTED)
set_target_properties(yuby PROPERTIES
        IMPORTED_LOCATION "${YUBY_ROOT}/android/${ANDROID_ABI}/libyuby.so")

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
        "${YUBY_ROOT}/io/yubyandroid.cpp")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${YUBY_ROOT}/android"
        "${YUBY_ROOT}/io")

target_link_libraries(${CMAKE_PROJECT_NAME} yuby android log dl)
