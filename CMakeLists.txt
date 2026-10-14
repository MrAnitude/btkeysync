cmake_minimum_required(VERSION 3.16)
project(btkeysync CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- find hivex (no CMake config ships with it, so locate it manually) ---
find_path(HIVEX_INCLUDE_DIR hivex.h)
find_library(HIVEX_LIBRARY hivex)

if(NOT HIVEX_INCLUDE_DIR OR NOT HIVEX_LIBRARY)
    message(FATAL_ERROR "hivex not found — install libhivex-dev")
endif()

# --- the executable, built from all your sources ---
add_executable(btkeysync
    src/main.cpp
    src/HiveReader.cpp
    src/BlueZInfo.cpp
    src/LinkKey.cpp
)

# --- where to find your own headers (the includes/ folder) ---
target_include_directories(btkeysync PRIVATE
    includes
    ${HIVEX_INCLUDE_DIR}
)

# --- link hivex (this is the -lhivex equivalent) ---
target_link_libraries(btkeysync PRIVATE ${HIVEX_LIBRARY})
