cmake_minimum_required(VERSION 3.10)
project(orb_cli)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug or Release" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG" CACHE STRING "Flags used by the C++ compiler during Debug builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Flags used by the C++ compiler during Release builds." FORCE)

# ------------------------------------------------------------------
# Boost setup (static)
# ------------------------------------------------------------------
set(BOOST_ROOT "/opt/3rd/boost/bin")
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS 
    system
    filesystem
    json
    beast
)

# ------------------------------------------------------------------
# POCO setup (static)
# ------------------------------------------------------------------
set(POCO_ROOT "/opt/3rd/poco/bin")
set(POCO_INCLUDE_DIR "${POCO_ROOT}/include")
set(POCO_LIB_DIR "${POCO_ROOT}/lib")

set(POCO_LIBS
    PocoFoundation
    PocoUtil
    PocoNet
)

set(POCO_LIBRARIES "")
foreach(LIB ${POCO_LIBS})
    find_library(POCO_${LIB}_LIB
        NAMES ${LIB} lib${LIB}.a
        HINTS ${POCO_LIB_DIR}
        NO_DEFAULT_PATH
        REQUIRED
    )
    list(APPEND POCO_LIBRARIES ${POCO_${LIB}_LIB})
endforeach()

# ------------------------------------------------------------------
# ------------------------------------------------------------------
# apilib from app (orb_client + lunaricorn_api)
# ------------------------------------------------------------------
add_subdirectory(/opt/app/apilib apilib)

# ------------------------------------------------------------------
# zlib
# ------------------------------------------------------------------
find_package(ZLIB REQUIRED)

add_executable(orb_cli
    main.cpp
    http_test.cpp
)

target_include_directories(orb_cli PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${POCO_INCLUDE_DIR}
    /opt/app/
    /opt/app/apilib
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(orb_cli
    orb_client
    lunaricorn_api
    Boost::json
    Boost::system
    Boost::filesystem
    Boost::beast
    ${POCO_LIBRARIES}
    ${ZLIB_LIBRARIES}
)

if(UNIX)
    target_link_libraries(orb_cli pthread dl)
endif()