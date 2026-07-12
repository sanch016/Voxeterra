cmake_minimum_required(VERSION 3.20)
project(Voxeterra VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── FetchContent ───────────────────────────────────────────────
include(FetchContent)

# raylib
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

# FastNoiseLite
FetchContent_Declare(
    fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(raylib glm fastnoiselite)

# ── Исполняемый файл ──────────────────────────────────────────
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Camera.cpp
    src/Player.cpp
    src/World.cpp
    src/Chunk.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    glm::glm
)

# FastNoiseLite — header-only
FetchContent_GetProperties(fastnoiselite SOURCE_DIR FNL_SOURCE_DIR)
target_include_directories(${PROJECT_NAME} PRIVATE ${FNL_SOURCE_DIR}/Cpp)

# Windows-specific libs for raylib
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
endif()
