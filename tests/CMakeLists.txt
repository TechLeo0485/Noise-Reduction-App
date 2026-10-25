include(GoogleTest)

# Hardware-independent tests: run in CI and on every build.
add_executable(vs_unit_tests
    TestUtils.h
    unit/test_ring_buffer.cpp
    unit/test_sample_conversion.cpp
    unit/test_biquad.cpp
    unit/test_noise_gate.cpp
    unit/test_dynamics.cpp
    unit/test_resampler.cpp
    unit/test_adaptive_reader.cpp
    unit/test_parameters.cpp
    unit/test_pitch_shifter.cpp
    unit/test_hardware.cpp
    unit/test_presets.cpp
    unit/test_stats.cpp
    audio/test_processing_chain.cpp
    audio/test_suppressors.cpp
    audio/test_voice_isolator.cpp
    unit/test_voxlock_stream.cpp
    integration/test_settings_store.cpp
    performance/test_chain_performance.cpp)
target_include_directories(vs_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vs_unit_tests PRIVATE vs_core vs_settings GTest::gtest_main)
gtest_discover_tests(vs_unit_tests DISCOVERY_MODE PRE_TEST)

# Tests that open real Windows audio endpoints. Run explicitly on a machine with audio
# hardware:  ctest --preset hardware
add_executable(vs_hardware_tests
    TestUtils.h
    integration/test_devices_hardware.cpp)
target_include_directories(vs_hardware_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vs_hardware_tests PRIVATE vs_core GTest::gtest_main ole32)
gtest_discover_tests(vs_hardware_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS hardware)

# The settings tests use Qt6Core; copy its DLL next to the test binary so test discovery and
# execution work without Qt on PATH.
add_custom_command(TARGET vs_unit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt6::Core> $<TARGET_FILE_DIR:vs_unit_tests>)
