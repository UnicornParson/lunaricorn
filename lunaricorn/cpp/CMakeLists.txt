cmake_minimum_required(VERSION 3.10)

project(lunaricorn_api VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT BOOST_ROOT)
    set(BOOST_ROOT "/opt/3rd/boost/bin" CACHE PATH "Boost installation root")
endif()
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS system filesystem json)

if(NOT POCO_ROOT)
    set(POCO_ROOT "/opt/3rd/poco/bin" CACHE PATH "POCO installation root")
endif()
set(POCO_INCLUDE_DIR "${POCO_ROOT}/include")
set(POCO_LIB_DIR "${POCO_ROOT}/lib")

set(POCO_LIBS
    PocoFoundation
    PocoUtil
    PocoNet
    PocoData
    PocoDataPostgreSQL
    PocoJSON
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

add_library(lunaricorn_api STATIC
    maintenance.cpp
    config.cpp
    lunaricorn.cpp
    mlog.cpp
)

target_include_directories(lunaricorn_api
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${POCO_INCLUDE_DIR}
)

target_link_libraries(lunaricorn_api
    PUBLIC
    Boost::json
    Boost::system
    Boost::filesystem
    ${POCO_LIBRARIES}
)

if(UNIX)
    target_link_libraries(lunaricorn_api
        PUBLIC
            pthread
            dl
    )
endif()

target_compile_features(lunaricorn_api PUBLIC cxx_std_20)