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

# ---- Strict Voice Isolation: ONNX Runtime + VoxLock INT8 Full ---------------------------------
#
#   ONNX Runtime 1.24.4 - MIT. Official DirectML build (NuGet Microsoft.ML.OnnxRuntime.DirectML): the CPU engine
#                        plus the DirectML engine for GPUs (NVIDIA, AMD, Intel). onnxruntime.dll ships next to the exe.
#                        (1.24.4 is the newest DirectML build; its CPU speed equals 1.31 for VoxLock, measured.)
#   DirectML 1.15.4      - Microsoft DirectML redistributable (NuGet Microsoft.AI.DirectML), DirectML.dll next to the exe.
#                        Windows' own System32 copy is older; VoxLockRuntime.cpp loads this one by full path.
#   VoxLock            - Apache-2.0 (arbazsiddiqui/voxlock). enroll.onnx + chunk_int8_full.onnx,
#                        pinned to a Hugging Face revision and verified by SHA-256.
#   CAM++ (3D-Speaker)   - Apache-2.0. Speaker verification for the voice check (speech_campplus_sv_en_voxceleb_16k,
#                        ONNX export from the sherpa-onnx "speaker-recongition-models" release), verified by SHA-256.
#
# All are fetched at configure time only. The application never downloads anything at runtime.
# Offline builds: set ONNXRUNTIME_ROOT / DIRECTML_ROOT (extracted packages) and VOXLOCK_MODEL_DIR (the three .onnx files).
# ONNXRUNTIME_ROOT may also point at a plain CPU package (include/, lib/); GPUs are then listed as unavailable.
if(VOICESTUDIO_WITH_VOXLOCK)
    set(ONNXRUNTIME_URL "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg"
        CACHE STRING "ONNX Runtime DirectML package (NuGet) for Windows x64")
    set(ONNXRUNTIME_URL_HASH "SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681"
        CACHE STRING "Hash of the ONNX Runtime package")
    set(ONNXRUNTIME_ROOT "" CACHE PATH "Use an already-extracted ONNX Runtime package instead of downloading")
    set(DIRECTML_URL "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg"
        CACHE STRING "DirectML redistributable package (NuGet)")
    set(DIRECTML_URL_HASH "SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9"
        CACHE STRING "Hash of the DirectML package")
    set(DIRECTML_ROOT "" CACHE PATH "Use an already-extracted DirectML package instead of downloading")

    if(ONNXRUNTIME_ROOT)
        set(_ort_dir "${ONNXRUNTIME_ROOT}")
    else()
        FetchContent_Declare(onnxruntime_pkg
            URL ${ONNXRUNTIME_URL} URL_HASH ${ONNXRUNTIME_URL_HASH} DOWNLOAD_NAME onnxruntime-directml.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_SUBDIR voicestudio-no-cmake)
        FetchContent_MakeAvailable(onnxruntime_pkg)
        set(_ort_dir "${onnxruntime_pkg_SOURCE_DIR}")
    endif()
    # NuGet layout (build/native/include, runtimes/win-x64/native) or the plain release zip (include, lib).
    if(EXISTS "${_ort_dir}/build/native/include/onnxruntime_cxx_api.h")
        set(_ort_include "${_ort_dir}/build/native/include")
        set(VOICESTUDIO_ORT_DLL "${_ort_dir}/runtimes/win-x64/native/onnxruntime.dll")
    else()
        set(_ort_include "${_ort_dir}/include")
        set(VOICESTUDIO_ORT_DLL "${_ort_dir}/lib/onnxruntime.dll")
    endif()
    if(NOT EXISTS "${_ort_include}/onnxruntime_cxx_api.h" OR NOT EXISTS "${VOICESTUDIO_ORT_DLL}")
        message(FATAL_ERROR "ONNX Runtime not found in ${_ort_dir}. Set ONNXRUNTIME_ROOT or configure with "
                            "-DVOICESTUDIO_WITH_VOXLOCK=OFF.")
    endif()
    # Headers only: the runtime is loaded explicitly with LoadLibrary (see VoxLockRuntime.cpp), so a missing
    # or broken DLL disables isolation instead of preventing the application from starting.
    add_library(onnxruntime_headers INTERFACE)
    target_include_directories(onnxruntime_headers SYSTEM INTERFACE "${_ort_include}")
    message(STATUS "ONNX Runtime: ${_ort_dir}")

    # DirectML: only with an ONNX Runtime build that has the DirectML engine (dml_provider_factory.h).
    set(VOICESTUDIO_DML_DLL "")
    if(EXISTS "${_ort_include}/dml_provider_factory.h")
        if(DIRECTML_ROOT)
            set(_dml_dir "${DIRECTML_ROOT}")
        else()
            FetchContent_Declare(directml_pkg
                URL ${DIRECTML_URL} URL_HASH ${DIRECTML_URL_HASH} DOWNLOAD_NAME directml.zip
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                SOURCE_SUBDIR voicestudio-no-cmake)
            FetchContent_MakeAvailable(directml_pkg)
            set(_dml_dir "${directml_pkg_SOURCE_DIR}")
        endif()
        if(EXISTS "${_dml_dir}/bin/x64-win/DirectML.dll")
            set(VOICESTUDIO_DML_DLL "${_dml_dir}/bin/x64-win/DirectML.dll")
            target_compile_definitions(onnxruntime_headers INTERFACE VOICESTUDIO_HAS_DIRECTML)
            target_include_directories(onnxruntime_headers SYSTEM INTERFACE "${_dml_dir}/include") # DirectML.h
            message(STATUS "DirectML: ${_dml_dir}")
        else()
            message(WARNING "DirectML.dll not found in ${_dml_dir}: GPU voice isolation disabled")
        endif()
    endif()

    set(VOXLOCK_REVISION "a50f4e1d109301d4b6f61dec57ebb1a61e7d8f03" CACHE STRING "Hugging Face revision of arbazsiddiqui/voxlock")
    set(VOXLOCK_MODEL_DIR "" CACHE PATH "Directory holding the VoxLock and CAM++ .onnx files (skips the download)")
    set(_voxlock_hf "https://huggingface.co/arbazsiddiqui/voxlock/resolve/${VOXLOCK_REVISION}/onnx")
    set(_campplus_url "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models")
    set(_voxlock_files
        "enroll.onnx|86645cba23f207def0b31a6e74a615d3b7a813b4fdf957c53b3af2905ba2ffd3|${_voxlock_hf}"
        "chunk_int8_full.onnx|7b57c1bfd5acd5b33768a9dff09d9af487d45374b3ba0b991e541ff121eaa337|${_voxlock_hf}"
        "3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx|357a834f702b80161e5b981182c038e18553c1f2ca752ed6cec2052365d4129b|${_campplus_url}")
    set(VOICESTUDIO_VOXLOCK_DIR "${CMAKE_BINARY_DIR}/_deps/voxlock-models")
    file(MAKE_DIRECTORY "${VOICESTUDIO_VOXLOCK_DIR}")
    set(VOICESTUDIO_VOXLOCK_FILES "")
    foreach(_entry IN LISTS _voxlock_files)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _sha)
        list(GET _parts 2 _url)
        set(_dest "${VOICESTUDIO_VOXLOCK_DIR}/${_name}")
        set(_have FALSE)
        if(EXISTS "${_dest}")
            file(SHA256 "${_dest}" _actual)
            if(_actual STREQUAL _sha)
                set(_have TRUE)
            endif()
        endif()
        if(NOT _have AND VOXLOCK_MODEL_DIR AND EXISTS "${VOXLOCK_MODEL_DIR}/${_name}")
            file(COPY_FILE "${VOXLOCK_MODEL_DIR}/${_name}" "${_dest}")
            file(SHA256 "${_dest}" _actual)
            if(NOT _actual STREQUAL _sha)
                file(REMOVE "${_dest}")
                message(FATAL_ERROR "${VOXLOCK_MODEL_DIR}/${_name} does not match the pinned model (SHA-256 ${_sha})")
            endif()
            set(_have TRUE)
        endif()
        if(NOT _have)
            message(STATUS "Downloading ${_name} ...")
            file(DOWNLOAD "${_url}/${_name}" "${_dest}"
                 EXPECTED_HASH SHA256=${_sha} INACTIVITY_TIMEOUT 60 STATUS _status)
            list(GET _status 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE "${_dest}")
                message(FATAL_ERROR "Downloading ${_name} failed: ${_status}. Set VOXLOCK_MODEL_DIR to "
                                    "a directory with the files, or configure with -DVOICESTUDIO_WITH_VOXLOCK=OFF.")
            endif()
        endif()
        list(APPEND VOICESTUDIO_VOXLOCK_FILES "${_dest}")
    endforeach()
    message(STATUS "VoxLock models: ${VOICESTUDIO_VOXLOCK_DIR}")
endif()

# ---- Voice character: Signalsmith Stretch (formant-preserving pitch shift) ---------------------
#
#   Signalsmith Stretch 1.4.0 - MIT. Header-only.
#   Signalsmith Linear 0.6.4  - MIT. Header-only; the FFT/STFT used by Stretch.
#
# Only the headers are used (SOURCE_SUBDIR skips their own CMake files, which would fetch from git).
# Offline builds: set SIGNALSMITH_STRETCH_DIR / SIGNALSMITH_LINEAR_DIR to the extracted archives.
set(SIGNALSMITH_STRETCH_DIR "" CACHE PATH "Use an already-extracted signalsmith-stretch 1.4.0 source tree")
set(SIGNALSMITH_LINEAR_DIR "" CACHE PATH "Use an already-extracted signalsmith linear 0.6.4 source tree")
if(SIGNALSMITH_STRETCH_DIR)
    set(_stretch_dir "${SIGNALSMITH_STRETCH_DIR}")
else()
    FetchContent_Declare(signalsmith_stretch_src
        URL https://github.com/Signalsmith-Audio/signalsmith-stretch/archive/refs/tags/1.4.0.tar.gz
        URL_HASH SHA256=077235709ecf2a358545e3ca7bdf32859f70f5c9afb4493e078bd753c9aff284
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR voicestudio-no-cmake)
    FetchContent_MakeAvailable(signalsmith_stretch_src)
    set(_stretch_dir "${signalsmith_stretch_src_SOURCE_DIR}")
endif()
if(SIGNALSMITH_LINEAR_DIR)
    set(_linear_dir "${SIGNALSMITH_LINEAR_DIR}")
else()
    FetchContent_Declare(signalsmith_linear_src
        URL https://github.com/Signalsmith-Audio/linear/archive/refs/tags/0.6.4.tar.gz
        URL_HASH SHA256=2cb10d84b96c626255ab46c4aa4c80f64ae532be487063cff6a08903c1445a56
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR voicestudio-no-cmake)
    FetchContent_MakeAvailable(signalsmith_linear_src)
    set(_linear_dir "${signalsmith_linear_src_SOURCE_DIR}")
endif()
if(NOT EXISTS "${_stretch_dir}/include/signalsmith-stretch/signalsmith-stretch.h" OR
   NOT EXISTS "${_linear_dir}/include/signalsmith-linear/stft.h")
    message(FATAL_ERROR "Signalsmith Stretch / Linear headers not found (${_stretch_dir}, ${_linear_dir}).")
endif()
add_library(signalsmith_stretch INTERFACE)
target_include_directories(signalsmith_stretch SYSTEM INTERFACE
    "${_stretch_dir}/include" "${_linear_dir}/include")
message(STATUS "Signalsmith Stretch: ${_stretch_dir}")

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
