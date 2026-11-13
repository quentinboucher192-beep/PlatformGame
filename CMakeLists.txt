cmake_minimum_required(VERSION 3.24)

project(CosmicPlatformer
    VERSION 1.0.0
    DESCRIPTION "Cosmic Platformer - 2D platformer with integrated editors"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Static MSVC runtime: the installed game needs no Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Every runtime artefact (game, tests, SDL DLLs) lands in one folder so the game can
# be launched directly from the build tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(COSMIC_BUILD_TESTS "Build the unit tests" ON)

include(cmake/Dependencies.cmake)
include(cmake/CompilerWarnings.cmake)

# ----------------------------------------------------------------------------- engine
file(GLOB_RECURSE COSMIC_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/engine/*.cpp ${CMAKE_SOURCE_DIR}/engine/*.h)
add_library(cosmic_engine STATIC ${COSMIC_ENGINE_SOURCES})
target_include_directories(cosmic_engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(cosmic_engine PUBLIC
    SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf SDL3_mixer::SDL3_mixer
    nlohmann_json::nlohmann_json cosmic_miniz)
target_compile_definitions(cosmic_engine PUBLIC
    COSMIC_VERSION_STRING="${PROJECT_VERSION}"
    COSMIC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    COSMIC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    COSMIC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    COSMIC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
cosmic_set_warnings(cosmic_engine)

# ------------------------------------------------------------------------------- game
# Gameplay, UI, editors, packaging and saves.
file(GLOB_RECURSE COSMIC_GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/game/*.cpp ${CMAKE_SOURCE_DIR}/game/*.h
    ${CMAKE_SOURCE_DIR}/ui/*.cpp ${CMAKE_SOURCE_DIR}/ui/*.h
    ${CMAKE_SOURCE_DIR}/editors/*.cpp ${CMAKE_SOURCE_DIR}/editors/*.h
    ${CMAKE_SOURCE_DIR}/packaging/*.cpp ${CMAKE_SOURCE_DIR}/packaging/*.h
    ${CMAKE_SOURCE_DIR}/saves/*.cpp ${CMAKE_SOURCE_DIR}/saves/*.h)
add_library(cosmic_game STATIC ${COSMIC_GAME_SOURCES})
target_link_libraries(cosmic_game PUBLIC cosmic_engine)
cosmic_set_warnings(cosmic_game)

# ------------------------------------------------------------------------- executable
set(COSMIC_EXE_SOURCES main.cpp)
if(WIN32)
    list(APPEND COSMIC_EXE_SOURCES installer/CosmicPlatformer.rc)
endif()
add_executable(CosmicPlatformer WIN32 ${COSMIC_EXE_SOURCES})
target_link_libraries(CosmicPlatformer PRIVATE cosmic_game)
cosmic_set_warnings(CosmicPlatformer)

# Make assets/ and data/ reachable from the build folder (the game looks next to the
# executable first, then in COSMIC_SOURCE_DIR as a developer fallback).
add_custom_command(TARGET CosmicPlatformer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Cosmic Platformer built: $<TARGET_FILE:CosmicPlatformer>")

# ------------------------------------------------------------------------------ tests
if(COSMIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------- install
include(cmake/Install.cmake)
