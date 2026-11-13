# Third-party dependencies for Cosmic Platformer.
#
# By default every library is downloaded once at configure time and built from source,
# so the project builds identically on Windows (MSVC / MinGW) and Linux without any
# pre-installed SDL packages. The resulting game is fully offline: the DLLs are shipped
# next to the executable.
#
# Set COSMIC_USE_SYSTEM_DEPS=ON to use already-installed packages (find_package) instead.

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

option(COSMIC_USE_SYSTEM_DEPS "Use installed SDL3/SDL3_image/SDL3_ttf/SDL3_mixer/nlohmann_json" OFF)

if(COSMIC_USE_SYSTEM_DEPS)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)
    find_package(SDL3_ttf REQUIRED CONFIG)
    find_package(SDL3_mixer REQUIRED CONFIG)
    find_package(nlohmann_json 3.11 REQUIRED CONFIG)
else()
    # ---------------------------------------------------------------- SDL3
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)

    # ---------------------------------------------------------- SDL3_image
    # stb_image backend: PNG/JPG without external libraries. PNG saving is built in.
    set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
    foreach(fmt AVIF JXL TIF WEBP SVG ANI GIF LBM PCX PNM QOI XCF XPM XV)
        set(SDLIMAGE_${fmt} OFF CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_Declare(SDL3_image
        URL https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-3.4.8.tar.gz
        URL_HASH SHA256=e8223b424bc7541cf84ffff5cf7e4f24ec3711c60462cb1bd6dc1d7bd7f25477
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3_image)

    # ------------------------------------------------------------ SDL3_ttf
    # The release tarball does not contain FreeType, so the git tag is used together
    # with the vendored FreeType submodule. HarfBuzz / PlutoSVG are not needed.
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "external/freetype"
        GIT_PROGRESS FALSE)
    FetchContent_MakeAvailable(SDL3_ttf)

    # ---------------------------------------------------------- SDL3_mixer
    # Built-in decoders only (WAV, stb_vorbis OGG, dr_flac, dr_mp3): no external codecs.
    set(SDLMIXER_VENDORED OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_INSTALL OFF CACHE BOOL "" FORCE)
    foreach(opt GME MOD OPUS WAVPACK MIDI FLAC_LIBFLAC MP3_MPG123 VORBIS_VORBISFILE VORBIS_TREMOR)
        set(SDLMIXER_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(SDLMIXER_VORBIS_STB ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_mixer
        URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz
        URL_HASH SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3_mixer)

    # -------------------------------------------------------- nlohmann/json
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# -------------------------------------------------------------------- miniz
# ZIP container used by the .cplanet level packages. Always built from the
# amalgamated release (two files), statically.
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
    URL_HASH SHA256=f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no_cmake_build)   # populate only: we build miniz.c ourselves
FetchContent_MakeAvailable(miniz)
add_library(cosmic_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(cosmic_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
set_target_properties(cosmic_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(cosmic_miniz PRIVATE /W0)
else()
    target_compile_options(cosmic_miniz PRIVATE -w)
endif()
