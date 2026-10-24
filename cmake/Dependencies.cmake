# Third-party dependencies.
#
#   Qt 6.5+      - found via CMAKE_PREFIX_PATH (official installer or aqtinstall), see README.
#   RNNoise 0.2  - BSD-3-Clause. Release tarball includes the trained model weights.
#   GoogleTest   - BSD-3-Clause. From vcpkg (manifest) if available, otherwise FetchContent.
#
# For fully offline / reproducible builds, download the archives once and point
# RNNOISE_SOURCE_DIR / FETCHCONTENT_SOURCE_DIR_GOOGLETEST at the extracted directories.

include(FetchContent)

set(RNNOISE_URL "https://github.com/xiph/rnnoise/releases/download/v0.2/rnnoise-0.2.tar.gz"
    CACHE STRING "RNNoise release archive (must contain src/rnnoise_data.c)")
set(RNNOISE_URL_HASH "" CACHE STRING "Optional SHA256=... hash to pin the RNNoise archive")
set(RNNOISE_SOURCE_DIR "" CACHE PATH "Use an already-extracted RNNoise source tree instead of downloading")

if(VOICESTUDIO_WITH_RNNOISE)
    if(RNNOISE_SOURCE_DIR)
        set(_rnnoise_dir "${RNNOISE_SOURCE_DIR}")
    else()
        set(_hash_args "")
        if(RNNOISE_URL_HASH)
            set(_hash_args URL_HASH ${RNNOISE_URL_HASH})
        endif()
        # SOURCE_SUBDIR points at a directory that does not exist so MakeAvailable only downloads
        # and extracts; the library target is defined below.
        FetchContent_Declare(rnnoise_src
            URL ${RNNOISE_URL} ${_hash_args}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR voicestudio-no-cmake)
        FetchContent_MakeAvailable(rnnoise_src)
        set(_rnnoise_dir "${rnnoise_src_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${_rnnoise_dir}/include/rnnoise.h")
        message(FATAL_ERROR "RNNoise sources not found in ${_rnnoise_dir} (expected include/rnnoise.h). "
                            "Set RNNOISE_SOURCE_DIR or configure with -DVOICESTUDIO_WITH_RNNOISE=OFF.")
    endif()
    if(NOT EXISTS "${_rnnoise_dir}/src/rnnoise_data.c" AND NOT EXISTS "${_rnnoise_dir}/src/rnn_data.c")
        message(FATAL_ERROR "RNNoise model data (src/rnnoise_data.c) is missing in ${_rnnoise_dir}. "
                            "Use the release tarball, or run the project's download_model script first.")
    endif()

    # Portable C sources only (no x86 runtime CPU dispatch). Training/dump tools are excluded.
    file(GLOB _rnnoise_sources "${_rnnoise_dir}/src/*.c")
    list(FILTER _rnnoise_sources EXCLUDE REGEX ".*(dump_|write_weights|_train|test).*\\.c$")

    add_library(rnnoise STATIC ${_rnnoise_sources})
    # rnnoise_compat supplies os_support.h, which the 0.2 archive omits but MSVC builds need.
    target_include_directories(rnnoise PUBLIC "${_rnnoise_dir}/include"
        PRIVATE "${_rnnoise_dir}/src" "${CMAKE_CURRENT_LIST_DIR}/rnnoise_compat")
    target_compile_definitions(rnnoise PRIVATE _USE_MATH_DEFINES RNNOISE_BUILD _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        # Third-party code: silence warnings, keep the build log readable.
        target_compile_options(rnnoise PRIVATE /W0)
    endif()
    message(STATUS "RNNoise: ${_rnnoise_dir}")
endif()

if(VOICESTUDIO_BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()
