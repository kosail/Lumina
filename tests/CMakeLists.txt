# ---------------------------------------------------------------------------
# Host unit tests (doctest) and on-device benchmark targets.
#
# This directory is added by the top-level CMakeLists only when LUMINA_BUILD_TESTS
# or LUMINA_BUILD_BENCH is ON, so a normal build stays minimal (AGENTS.md §9).
# ---------------------------------------------------------------------------

if(LUMINA_BUILD_TESTS)
    # doctest is a single, header-only MIT library. FetchContent downloads a PINNED
    # release tag so the build is reproducible; update the tag deliberately, after
    # checking the upstream releases page (INV-001). The first configure needs
    # network access.
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.3)
    FetchContent_MakeAvailable(doctest)

    # One executable for all host tests: test_main.cpp defines doctest's main().
    add_executable(lumina_tests
        test_main.cpp
        test_bounded_queue.cpp
        test_config.cpp
        test_config_env.cpp
        test_distance.cpp
        test_arbiter.cpp
        test_scene.cpp
        test_pipeline.cpp
        test_pipeline_face.cpp
        test_face_store.cpp
        test_face_greeter.cpp
        test_image_list.cpp
        test_proximity.cpp
        test_sink_watchdog.cpp
        test_status.cpp
        test_yolo11_decode.cpp
        test_describer.cpp
        test_caching_tts.cpp)
    # mocks live under tests/ and are included as "mocks/...", so tests/ must be on
    # the include path in addition to src/ (which lumina_core exposes publicly).
    target_include_directories(lumina_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(lumina_tests PRIVATE lumina_core doctest::doctest)

    # Agent tests need lumina_agent_core, which only exists when the agent is built
    # (LUMINA_BUILD_AGENT, default ON). Guarded so disabling the agent still builds.
    if(TARGET lumina_agent_core)
        target_sources(lumina_tests PRIVATE test_agent.cpp)
        target_link_libraries(lumina_tests PRIVATE lumina_agent_core)
    endif()

    add_test(NAME lumina_tests COMMAND lumina_tests)
endif()

if(LUMINA_BUILD_BENCH AND LUMINA_ENABLE_NCNN)
    # Benchmark targets run on the Pi and report FPS/RSS/temperature (INV-050).
    # They need the NCNN detector, hence the LUMINA_ENABLE_NCNN requirement.
    add_executable(lumina_bench_fps bench_fps.cpp)
    target_link_libraries(lumina_bench_fps PRIVATE lumina_core)
endif()

if(LUMINA_BUILD_BENCH AND LUMINA_ENABLE_AUDIO)
    # Alert latency benchmark (NFR-02, INV-051): real CachingTts + AlsaSink, so it
    # needs the audio path (libpiper + ALSA) rather than NCNN.
    add_executable(lumina_bench_latency bench_latency.cpp)
    target_link_libraries(lumina_bench_latency PRIVATE lumina_core)
endif()
