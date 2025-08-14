cmake_minimum_required(VERSION 3.20)
project(Warland LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(WARLAND_BUILD_TOOLS "Build editor/tools targets" ON)
option(WARLAND_ENABLE_TESTS "Enable tests" ON)

# Folders
set(WARLAND_SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(WARLAND_EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)

# Dependencies via vcpkg (configure with -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
# Bullet 3
find_package(Bullet CONFIG REQUIRED)
# Lua (CMake built-in FindLua)
find_package(Lua REQUIRED)
# sol2 is header-only
find_path(SOL2_INCLUDE_DIRS "sol/sol.hpp")
# zstd
find_package(zstd CONFIG REQUIRED)
# imgui (linked later when needed)
find_package(imgui CONFIG)

# Sources
file(GLOB_RECURSE WARLAND_HEADERS CONFIGURE_DEPENDS
    ${WARLAND_SRC_DIR}/**/*.h
)
file(GLOB_RECURSE WARLAND_SOURCES CONFIGURE_DEPENDS
    ${WARLAND_SRC_DIR}/**/*.cpp
)
add_executable(Warland ${WARLAND_SRC_DIR}/main.cpp ${WARLAND_SOURCES} ${WARLAND_HEADERS})

# Ensure GL 2D tile renderer sources are picked up (they match glob already if using globbing)
# If not using glob, add these explicitly:
# target_sources(Warland PRIVATE src/Engine/Rendering/GL/TileRenderer2D.cpp src/Engine/Rendering/GL/TileAtlas.cpp src/Engine/Rendering/GL/TileMap.cpp)

# Include dirs
target_include_directories(Warland PRIVATE ${WARLAND_SRC_DIR})
if (SOL2_INCLUDE_DIRS)
    target_include_directories(Warland PRIVATE ${SOL2_INCLUDE_DIRS})
endif()

# Warnings
if(MSVC)
    target_compile_options(Warland PRIVATE /W4 /permissive-)
else()
    target_compile_options(Warland PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Link libs
# Core windowing/render
target_link_libraries(Warland PRIVATE glfw)
if (WIN32)
    target_link_libraries(Warland PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(Warland PRIVATE OpenGL::GL)
endif()
# GL loader
target_link_libraries(Warland PRIVATE glad::glad)
# Utilities
target_link_libraries(Warland PRIVATE glm::glm spdlog::spdlog nlohmann_json::nlohmann_json)
# Audio
target_link_libraries(Warland PRIVATE OpenAL::OpenAL)
# Compression
target_link_libraries(Warland PRIVATE zstd::libzstd)
# Bullet: link a common subset (adjust when physics code lands)
target_link_libraries(Warland PRIVATE Bullet3Common BulletDynamics BulletCollision LinearMath)
# Lua (variables from FindLua)
target_link_libraries(Warland PRIVATE ${LUA_LIBRARIES})
if (imgui_FOUND)
    target_link_libraries(Warland PRIVATE imgui::imgui)
    target_compile_definitions(Warland PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)
endif()

# Tests
if(WARLAND_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Notes:
# - Configure with: -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
# - Installed via vcpkg: glfw3, glm, spdlog, nlohmann-json, glad, openal-soft, bullet3, lua, sol2 (headers), zstd, imgui
