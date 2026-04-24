cmake_minimum_required(VERSION 3.10.0)

project(bolide-powerd
    VERSION 0.1.0
    DESCRIPTION "Power management daemon for AsteroidOS")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core DBus REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

include(GNUInstallDirs)

option(BUILD_TESTS "Build unit tests" OFF)

add_subdirectory(src)

if(BUILD_TESTS)
    find_package(Qt5 COMPONENTS Test REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()
