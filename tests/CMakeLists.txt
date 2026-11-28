file(GLOB COSMIC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
# Optional extra test sources (work in progress kept outside the repository).
set(COSMIC_EXTRA_TESTS_DIR "" CACHE PATH "Additional folder of test sources compiled into cosmic_tests")
if(COSMIC_EXTRA_TESTS_DIR)
    file(GLOB COSMIC_EXTRA_TEST_SOURCES CONFIGURE_DEPENDS ${COSMIC_EXTRA_TESTS_DIR}/*.cpp)
    list(APPEND COSMIC_TEST_SOURCES ${COSMIC_EXTRA_TEST_SOURCES})
endif()
add_executable(cosmic_tests ${COSMIC_TEST_SOURCES})
# The search of the machine's runs without its program (test_tas_search.cpp: its rounds, its guidance).
target_sources(cosmic_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/tas/Search.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Course.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Output.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Planner.cpp)
# The portfolio of strategies (test_tas_portfolio.cpp: Go-Explore, timing polish, the bandit, branch-and-bound).
target_sources(cosmic_tests PRIVATE ${CMAKE_SOURCE_DIR}/tools/tas/Sim.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Bound.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Explore.cpp
                                    ${CMAKE_SOURCE_DIR}/tools/tas/Timing.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Portfolio.cpp
                                    ${CMAKE_SOURCE_DIR}/tools/tas/Phase.cpp ${CMAKE_SOURCE_DIR}/tools/tas/Branch.cpp)
# Arena heaps (savestates of the machine's search, docs/TAS.md): the tests redirect operator new too.
target_link_libraries(cosmic_tests PRIVATE cosmic_game cosmic_heap_hooks)
if(WIN32)
    target_link_libraries(cosmic_tests PRIVATE ws2_32)  # local socket server of test_online_net.cpp
endif()
cosmic_set_warnings(cosmic_tests)
add_test(NAME cosmic_tests COMMAND cosmic_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(cosmic_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy")
