# Minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name
project(maintenance)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static linking for all libraries
set(BUILD_SHARED_LIBS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug or Release" FORCE)
endif()

# Override default flags for Debug and Release configurations
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG" CACHE STRING "Flags used by the C++ compiler during Debug builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Flags used by the C++ compiler during Release builds." FORCE)
# ------------------------------------------------------------------
# Boost setup (static)
# ------------------------------------------------------------------
# Path to Boost installation (adjust if your structure differs)
set(BOOST_ROOT "/opt/3rd/boost/bin")
# Tell CMake to search for static Boost libraries
set(Boost_USE_STATIC_LIBS ON)
# Find Boost components (add or remove as needed)
find_package(Boost REQUIRED COMPONENTS system filesystem json)

# ------------------------------------------------------------------
# POCO setup (static)
# ------------------------------------------------------------------
# POCO installation directory
set(POCO_ROOT "/opt/3rd/poco/bin")
# POCO include directory
set(POCO_INCLUDE_DIR "${POCO_ROOT}/include")
# POCO library directory (adjust if your libs are elsewhere)
set(POCO_LIB_DIR "${POCO_ROOT}/lib")

# api
add_subdirectory(/opt/app/apilib/)

# List of required POCO libraries (add more if needed, e.g., PocoNet, PocoUtil)
set(POCO_LIBS
    PocoFoundation
    PocoUtil
    PocoNet
    PocoData
    PocoDataPostgreSQL
)

# Create a list of full paths to static POCO libraries
set(POCO_LIBRARIES "")
foreach(LIB ${POCO_LIBS})
    # On Linux static libraries are named lib<name>.a
    find_library(POCO_${LIB}_LIB
        NAMES ${LIB} lib${LIB}.a
        HINTS ${POCO_LIB_DIR}
        NO_DEFAULT_PATH
        REQUIRED
    )
    list(APPEND POCO_LIBRARIES ${POCO_${LIB}_LIB})
endforeach()

# ------------------------------------------------------------------
# Application
# ------------------------------------------------------------------
# Add executable from main.cpp
add_executable(maintenance
    main.cpp
    storage.cpp
    endpoint.cpp
    pusher.cpp
    )

# Include directories
target_include_directories(maintenance PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${POCO_INCLUDE_DIR}
    /opt/app/apilib/
)

# Link libraries (static)
target_link_libraries(maintenance
    ${Boost_LIBRARIES}
    ${POCO_LIBRARIES}
    lunaricorn_api
)

# On Linux, static linking may require pthread and dl
if(UNIX)
    target_link_libraries(maintenance pthread dl)
endif()