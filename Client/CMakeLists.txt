cmake_minimum_required(VERSION 3.16)

project(Chart_Projcet LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

#find_package(Qt5 COMPONENTS Widgets REQUIRED Network)
# find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)  Qt5
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)


if(ANDROID)
  add_library(Chart_Projcet SHARED
    main.cpp
    mymain.cpp
    mymain.h
    mymain.ui
  )
else()
  add_executable(Chart_Projcet
    main.cpp
    mymain.cpp
    mymain.h
    mymain.ui
    logindialog.cpp
    logindialog.h
  )
endif()

# 链接库从Qt5::xxx改为Qt6::xxx
# target_link_libraries(Chart_Projcet PRIVATE Qt5::Widgets Qt5::Network)
target_link_libraries(Chart_Projcet PRIVATE Qt6::Widgets Qt6::Network)
