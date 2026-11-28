cmake_minimum_required(VERSION 3.24)

project(CosmicPlatformer
    VERSION 2.103.0
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

if(MINGW)
    # Self-contained Windows executables with MinGW: no libstdc++ / libgcc / winpthread DLLs
    # (applies to the targets below, not to the SDL libraries configured above).
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

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

# Cosmic Online HTTP client: WinHTTP (part of Windows, no DLL to ship) or libcurl elsewhere.
# Without it the game still builds and runs; the online screens report that the network
# is not available in this build.
option(COSMIC_ONLINE_HTTP "Build the HTTP client of Cosmic Online" ON)
if(WIN32)
    target_link_libraries(cosmic_game PUBLIC crypt32)  # DPAPI protection of the saved session
endif()
if(COSMIC_ONLINE_HTTP)
    if(WIN32)
        target_compile_definitions(cosmic_game PRIVATE COSMIC_HTTP_WINHTTP)
        target_link_libraries(cosmic_game PUBLIC winhttp)
    else()
        find_package(CURL)
        if(CURL_FOUND)
            target_compile_definitions(cosmic_game PRIVATE COSMIC_HTTP_CURL)
            target_link_libraries(cosmic_game PUBLIC CURL::libcurl)
        else()
            message(WARNING "libcurl introuvable : Cosmic Online est compilé sans client HTTP (installez libcurl4-openssl-dev).")
        endif()
    endif()
endif()

# ------------------------------------------------------------- machine's runs (TAS)
# The programs that keep savestates of the simulation (the offline search and the tests) build
# each run in an arena heap: they replace operator new with tools/tas/HeapHooks.cpp (docs/TAS.md).
# The game never links it.
add_library(cosmic_heap_hooks OBJECT ${CMAKE_SOURCE_DIR}/tools/tas/HeapHooks.cpp)
target_link_libraries(cosmic_heap_hooks PRIVATE cosmic_engine)
cosmic_set_warnings(cosmic_heap_hooks)

# cosmic_tas: the search of the machine's runs. Installed next to the game (cmake/Install.cmake):
# the game launches it as a child process on an administrator's computer (ADMINISTRATION › TAS,
# docs/TAS.md); it never runs on a player's computer by itself.
option(COSMIC_BUILD_TAS "Build cosmic_tas, the search of the machine's runs" ON)
if(COSMIC_BUILD_TAS)
    file(GLOB COSMIC_TAS_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/tas/*.cpp ${CMAKE_SOURCE_DIR}/tools/tas/*.h)
    list(FILTER COSMIC_TAS_SOURCES EXCLUDE REGEX "HeapHooks\\.cpp$")
    find_package(Threads REQUIRED)
    add_executable(cosmic_tas ${COSMIC_TAS_SOURCES})
    target_link_libraries(cosmic_tas PRIVATE cosmic_game cosmic_heap_hooks Threads::Threads)
    cosmic_set_warnings(cosmic_tas)
    set_target_properties(cosmic_tas PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH OFF)
endif()

# ------------------------------------------------------------------------- executable
set(COSMIC_EXE_SOURCES main.cpp)
if(WIN32)
    list(APPEND COSMIC_EXE_SOURCES installer/CosmicPlatformer.rc)
endif()
add_executable(CosmicPlatformer WIN32 ${COSMIC_EXE_SOURCES})
target_link_libraries(CosmicPlatformer PRIVATE cosmic_game)
cosmic_set_warnings(CosmicPlatformer)
# The installed game finds its shared libraries next to the executable.
set_target_properties(CosmicPlatformer PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_WITH_INSTALL_RPATH OFF)

# Note: from the build tree the game finds assets/ and data/ through COSMIC_SOURCE_DIR;
# installed, they sit next to the executable (see engine/core/Paths.cpp).

# ------------------------------------------------------------------------------ tests
if(COSMIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------- install
include(cmake/Install.cmake)
