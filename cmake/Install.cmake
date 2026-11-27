# Installation layout (used by the Windows installer and by `cmake --install`):
#
#   <prefix>/CosmicPlatformer(.exe)
#   <prefix>/cosmic_tas(.exe)        (ADMINISTRATION › TAS, when COSMIC_BUILD_TAS is on)
#   <prefix>/SDL3*.dll / libSDL3*.so
#   <prefix>/assets/...
#   <prefix>/data/...
#   <prefix>/docs/...
#   <prefix>/licenses/...

install(TARGETS CosmicPlatformer RUNTIME DESTINATION .)

# The search of the machine's runs (docs/TAS.md « Entraînement depuis ADMINISTRATION »): next to
# the game, which launches it as a child process for an administrator (ADMINISTRATION › TAS, a page
# only an administrator's session opens). It holds no secret and talks to no server: harmless for
# every other player. The game itself never links its search nor its operator new.
if(TARGET cosmic_tas)
    install(TARGETS cosmic_tas RUNTIME DESTINATION .)
endif()

if(NOT COSMIC_USE_SYSTEM_DEPS)
    install(TARGETS SDL3-shared SDL3_image-shared SDL3_ttf-shared SDL3_mixer-shared
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/data/ DESTINATION data)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/ DESTINATION docs)
install(FILES ${CMAKE_SOURCE_DIR}/README.md DESTINATION docs)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION licenses OPTIONAL)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/licenses/ DESTINATION licenses OPTIONAL)
