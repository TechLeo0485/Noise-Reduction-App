cmake_minimum_required(VERSION 3.25)

project(VoiceStudio
    VERSION 0.3.0
    DESCRIPTION "Ultra-low-latency local noise suppression and voice processing for Windows"
    LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "VoiceStudio targets Windows 10/11 (WASAPI) only.")
endif()

option(VOICESTUDIO_WITH_RNNOISE "Build with RNNoise neural noise suppression" ON)
option(VOICESTUDIO_WITH_VOXLOCK "Build Strict Voice Isolation (VoxLock INT8 Full on ONNX Runtime)" ON)
option(VOICESTUDIO_BUILD_APP "Build the Qt desktop application" ON)
option(VOICESTUDIO_BUILD_TESTS "Build unit / integration / performance tests" ON)
option(VOICESTUDIO_BUILD_TOOLS "Build command-line benchmark and latency tools" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)

# ---- Common compile settings -------------------------------------------------------------
add_library(vs_compile_options INTERFACE)
target_compile_definitions(vs_compile_options INTERFACE
    UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 _CRT_SECURE_NO_WARNINGS
    VOICESTUDIO_VERSION="${PROJECT_VERSION}")
if(MSVC)
    # /fp:precise (the default) is kept on purpose: the NaN/Inf output guard relies on std::isfinite.
    target_compile_options(vs_compile_options INTERFACE
        /W4 /wd4324 /permissive- /utf-8 /Zc:__cplusplus /MP
        $<$<CONFIG:Release,RelWithDebInfo>:/GL>)
    target_link_options(vs_compile_options INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>)
endif()

# ---- Core engine (Qt-free) -----------------------------------------------------------------
add_library(vs_core STATIC
    src/core/Parameters.cpp
    src/core/RealtimeThread.cpp
    src/dsp/Biquad.cpp
    src/dsp/NoiseGate.cpp
    src/dsp/Compressor.cpp
    src/dsp/Limiter.cpp
    src/dsp/Equalizer.cpp
    src/dsp/PitchShifter.cpp
    src/dsp/VoiceStabilizer.cpp
    src/dsp/DspNoiseSuppressor.cpp
    src/ai/rnnoise/RnnoiseSuppressor.cpp
    src/ai/voxlock/VoxLockStream.cpp
    src/ai/voxlock/VoxLockEnrollment.cpp
    src/audio/isolation/VoiceIsolator.cpp
    src/audio/VoiceProcessingChain.cpp
    src/audio/AudioEngine.cpp
    src/audio/EngineSupervisor.cpp
    src/audio/buffers/Resampler.cpp
    src/audio/buffers/SampleConversion.cpp
    src/audio/buffers/AdaptiveRingReader.cpp
    src/audio/monitoring/StatsCollector.cpp
    src/audio/wasapi/WasapiCommon.cpp
    src/audio/wasapi/WasapiCaptureStream.cpp
    src/audio/wasapi/WasapiRenderStream.cpp
    src/devices/DeviceManager.cpp
    src/devices/VirtualMicDriver.cpp
    src/hardware/HardwareInfo.cpp
    src/diagnostics/Logger.cpp
    src/diagnostics/ProcessMetrics.cpp
    src/diagnostics/LoopbackLatencyTester.cpp
    src/diagnostics/VoicePlayback.cpp
    src/settings/Presets.cpp)
target_include_directories(vs_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vs_core
    PUBLIC vs_compile_options
    PRIVATE ole32 avrt dxgi psapi shell32 uuid setupapi cfgmgr32)
if(VOICESTUDIO_WITH_RNNOISE)
    target_link_libraries(vs_core PRIVATE rnnoise)
    target_compile_definitions(vs_core PRIVATE VOICESTUDIO_HAS_RNNOISE=1)
endif()
# Signalsmith Stretch is header-only and heavily templated: only PitchShifter.cpp includes it.
target_link_libraries(vs_core PRIVATE signalsmith_stretch)
if(MSVC)
    set_source_files_properties(src/dsp/PitchShifter.cpp PROPERTIES COMPILE_OPTIONS "/bigobj")
endif()
if(VOICESTUDIO_WITH_VOXLOCK)
    target_sources(vs_core PRIVATE
        src/ai/voxlock/OnnxModelPatch.cpp
        src/ai/voxlock/VoxLockRuntime.cpp)
    target_link_libraries(vs_core PRIVATE onnxruntime_headers)
    target_compile_definitions(vs_core PRIVATE VOICESTUDIO_HAS_VOXLOCK=1)
endif()

# Copies onnxruntime.dll and the VoxLock models next to an executable (models/voxlock/), which is
# where VoiceIsolator looks for them at runtime.
function(voicestudio_deploy_voxlock target)
    if(NOT VOICESTUDIO_WITH_VOXLOCK)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VOICESTUDIO_ORT_DLL}" ${VOICESTUDIO_DML_DLL} "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/models/voxlock"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VOICESTUDIO_VOXLOCK_FILES} "$<TARGET_FILE_DIR:${target}>/models/voxlock"
        VERBATIM)
endfunction()

# ---- Qt-dependent pieces ----------------------------------------------------------------------
if(VOICESTUDIO_BUILD_APP OR VOICESTUDIO_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
endif()

if(VOICESTUDIO_BUILD_APP OR VOICESTUDIO_BUILD_TESTS)
    add_library(vs_settings STATIC src/settings/SettingsStore.cpp)
    target_link_libraries(vs_settings PUBLIC vs_core Qt6::Core)
endif()

if(VOICESTUDIO_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Widgets Qml Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(VoiceStudio WIN32
        src/app/main.cpp
        src/app/EngineController.cpp
        src/app/EngineController.h
        src/app/TrayIcon.cpp
        src/app/TrayIcon.h
        src/app/CrashHandler.cpp
        src/app/VoiceStudio.rc)
    # Window and tray icon (the .rc embeds the same file as the exe icon). Icons come from logo.png via
    # scripts/make-icons.ps1; the resource compiler does not track the .ico, so name it as a dependency.
    set_source_files_properties(src/app/VoiceStudio.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/icons/voicestudio.ico")
    qt_add_resources(VoiceStudio "app_icons" PREFIX "/icons" BASE ui/icons FILES ui/icons/voicestudio.ico ui/icons/voicestudio.png)
    # The Help page renders the user guide from the binary.
    qt_add_resources(VoiceStudio "user_guide" PREFIX "/docs" BASE docs FILES docs/user-guide.md)

    # QML files live in ui/ subdirectories; let Qt generate a qmldir for each (Qt 6.8+ policy).
    if(QT_KNOWN_POLICY_QTP0004)
        qt_policy(SET QTP0004 NEW)
    endif()
    set_source_files_properties(ui/themes/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(VoiceStudio
        URI VoiceStudio
        VERSION 1.0
        QML_FILES
            ui/Main.qml
            ui/themes/Theme.qml
            ui/components/Card.qml
            ui/components/CardGrid.qml
            ui/components/LatencyMeter.qml
            ui/components/LookPicker.qml
            ui/components/StabilizerReadout.qml
            ui/components/StatusPill.qml
            ui/components/ChecklistItem.qml
            ui/components/ChipRow.qml
            ui/components/Expander.qml
            ui/components/LinkText.qml
            ui/components/LevelMeter.qml
            ui/components/ModeTile.qml
            ui/components/NavButton.qml
            ui/components/NoiseLevelPicker.qml
            ui/components/ParamSlider.qml
            ui/components/ParamSwitch.qml
            ui/components/ProgressBarThin.qml
            ui/components/ScreenPage.qml
            ui/components/StatRow.qml
            ui/components/VsButton.qml
            ui/components/VsComboBox.qml
            ui/components/VsSlider.qml
            ui/components/VsSwitch.qml
            ui/components/VoiceIsolationCard.qml
            ui/components/VoiceRecorderCard.qml
            ui/components/DeviceTile.qml
            ui/screens/HomeScreen.qml
            ui/screens/VoiceScreen.qml
            ui/screens/CleanupScreen.qml
            ui/screens/SetupScreen.qml
            ui/screens/HelpScreen.qml)

    target_link_libraries(VoiceStudio PRIVATE
        vs_core vs_settings Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 dbghelp dwmapi)
    voicestudio_deploy_voxlock(VoiceStudio)

    # Copy the Qt DLLs, plugins and QML modules next to the exe after every build so it runs
    # straight from build/<preset>/bin (the install step deploys separately, see below).
    find_program(VOICESTUDIO_WINDEPLOYQT windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH)
    if(VOICESTUDIO_WINDEPLOYQT)
        add_custom_command(TARGET VoiceStudio POST_BUILD
            COMMAND "${VOICESTUDIO_WINDEPLOYQT}" $<IF:$<CONFIG:Debug>,--debug,--release>
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/ui" --no-translations --no-compiler-runtime
                    "$<TARGET_FILE:VoiceStudio>"
            COMMENT "Deploying Qt runtime next to VoiceStudio.exe"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found; run it manually before starting VoiceStudio.exe from the build tree")
    endif()

    install(TARGETS VoiceStudio RUNTIME DESTINATION .)
    # The installer runs vc_redist itself (only when needed), and the DirectX shader compiler (dxcompiler/dxil)
    # is only used by the Direct3D 12 backend: keep both out of the app folder (~38 MB).
    qt_generate_deploy_qml_app_script(
        TARGET VoiceStudio
        OUTPUT_SCRIPT VOICESTUDIO_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_TOOL_OPTIONS --no-compiler-runtime --no-system-dxc-compiler)
    # Qt's deploy script would put the Qt DLLs in bin/ while VoiceStudio.exe installs to the root, so the
    # installed app could not start. Deploy everything next to the exe instead.
    install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
    install(SCRIPT ${VOICESTUDIO_DEPLOY_SCRIPT})
    install(FILES LICENSE THIRD_PARTY_LICENSES.md README.md DESTINATION . OPTIONAL)

    # VoiceStudio Virtual Microphone: the device installer, and the signed driver package when it has been built
    # (scripts/build-driver.ps1 writes it to build/driver-package; see driver/README.md).
    add_executable(vs_vmic tools/vs_vmic.cpp src/devices/VirtualMicDriver.cpp)
    target_include_directories(vs_vmic PRIVATE src)
    # Every command changes or reads machine-wide driver state: always ask for administrator rights.
    target_link_options(vs_vmic PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
    target_link_libraries(vs_vmic PRIVATE user32 advapi32)
    target_link_libraries(vs_vmic PRIVATE setupapi newdev cfgmgr32)
    install(TARGETS vs_vmic RUNTIME DESTINATION driver)
    set(VOICESTUDIO_DRIVER_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/driver-package" CACHE PATH
        "Signed VoiceStudio Virtual Microphone package (vsvirtualmic.sys/.inf/.cat + certificate)")
    if(EXISTS "${VOICESTUDIO_DRIVER_PACKAGE_DIR}/vsvirtualmic.inf")
        install(DIRECTORY "${VOICESTUDIO_DRIVER_PACKAGE_DIR}/" DESTINATION driver)
    else()
        message(STATUS "VoiceStudio Virtual Microphone package not built; the installer will not include it")
    endif()
    if(VOICESTUDIO_WITH_VOXLOCK)
        install(FILES "${VOICESTUDIO_ORT_DLL}" ${VOICESTUDIO_DML_DLL} DESTINATION .)
        install(FILES ${VOICESTUDIO_VOXLOCK_FILES} DESTINATION models/voxlock)
    endif()
endif()

# ---- Tools ----------------------------------------------------------------------------------
if(VOICESTUDIO_BUILD_TOOLS)
    add_executable(vs_bench tools/vs_bench.cpp)
    target_link_libraries(vs_bench PRIVATE vs_core)
    add_executable(vs_latency tools/vs_latency.cpp)
    target_link_libraries(vs_latency PRIVATE vs_core ole32)
    add_executable(vs_soak tools/vs_soak.cpp)
    target_link_libraries(vs_soak PRIVATE vs_core ole32)
    install(TARGETS vs_bench vs_latency vs_soak RUNTIME DESTINATION tools)
    if(VOICESTUDIO_WITH_VOXLOCK)
        add_executable(vs_isolation tools/vs_isolation.cpp)
        target_link_libraries(vs_isolation PRIVATE vs_core winmm)
        voicestudio_deploy_voxlock(vs_isolation)
    endif()
endif()

# ---- Tests ----------------------------------------------------------------------------------
if(VOICESTUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
