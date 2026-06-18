cmake_minimum_required(VERSION 3.20)

project(WarLand
    VERSION 0.1.0
    DESCRIPTION "God game de grande echelle - moteur custom C++/OpenGL"
    LANGUAGES CXX C)

# ----------------------------------------------------------------------------
# Options globales
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sortie des binaires dans build/bin pour retrouver shaders/ et assets/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------------------
# Dependances (telechargees et compilees automatiquement via FetchContent)
# ----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- GLFW : fenetre, contexte OpenGL, input ---
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW ON)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# --- GLAD : loader des fonctions OpenGL modernes (genere pour GL 4.5 core) ---
FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    GIT_SHALLOW ON
    SOURCE_SUBDIR cmake)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl_core_45 REPRODUCIBLE API gl:core=4.5)

# --- GLM : math 3D (header-only) ---
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(glm)

# --- EnTT : Entity Component System (header-only) ---
FetchContent_Declare(entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.2
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(entt)

# --- nlohmann/json : serialisation des sauvegardes (header-only) ---
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(json)

# --- Dear ImGui : UI en jeu et outils de debug (sans CMake officiel) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ----------------------------------------------------------------------------
# Cible principale : WarLand
# ----------------------------------------------------------------------------
file(GLOB_RECURSE WARLAND_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.cpp)

add_executable(WarLand ${WARLAND_SOURCES})

target_include_directories(WarLand PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(WarLand PRIVATE
    glfw
    glad_gl_core_45
    glm::glm
    EnTT::EnTT
    nlohmann_json::nlohmann_json
    imgui)

if(WIN32)
    target_link_libraries(WarLand PRIVATE winmm) # timeBeginPeriod : timer precis
endif()

# Chemin absolu vers les ressources, injecte a la compilation
target_compile_definitions(WarLand PRIVATE
    WARLAND_ASSETS_DIR="${CMAKE_SOURCE_DIR}")

if(MSVC)
    target_compile_options(WarLand PRIVATE /W4 /permissive-)
else()
    target_compile_options(WarLand PRIVATE -Wall -Wextra)
endif()
