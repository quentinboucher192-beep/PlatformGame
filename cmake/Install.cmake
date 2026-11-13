# Installation layout (used by the Windows installer and by `cmake --install`):
#
#   <prefix>/CosmicPlatformer(.exe)
#   <prefix>/SDL3*.dll / libSDL3*.so
#   <prefix>/assets/...
#   <prefix>/data/...
#   <prefix>/docs/...
#   <prefix>/licenses/...

install(TARGETS CosmicPlatformer RUNTIME DESTINATION .)

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
