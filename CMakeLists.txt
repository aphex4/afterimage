cmake_minimum_required(VERSION 3.22)

project(AFTERIMAGE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_PATH STREQUAL "" AND EXISTS "$ENV{HOME}/dev/Spawnclone/JUCE/CMakeLists.txt")
    set(JUCE_PATH "$ENV{HOME}/dev/Spawnclone/JUCE")
endif()

if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(STATUS "AFTERIMAGE: using local JUCE at ${JUCE_PATH}")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    message(STATUS "AFTERIMAGE: fetching JUCE 8.0.6 via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Licensing / tool options
# ---------------------------------------------------------------------------
option(AFTERIMAGE_ENABLE_LICENSING "Enable offline signed-license system" ON)
option(AFTERIMAGE_BUILD_LICENSE_TOOL "Build AfterimageLicenseTool (not in plugin)" OFF)
option(AFTERIMAGE_USE_TEST_LICENSE_KEY "Embed test public key (tests / debug only)" OFF)

if(AFTERIMAGE_USE_TEST_LICENSE_KEY AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "AFTERIMAGE_USE_TEST_LICENSE_KEY is ON for a Release build — do not ship this binary.")
endif()

# ---------------------------------------------------------------------------
# Shared sources
# ---------------------------------------------------------------------------
set(AFTERIMAGE_DSP_SOURCES
    Source/DSP/SpectralEngine.cpp
    Source/DSP/STFTProcessor.cpp
    Source/DSP/SpectralHistoryBuffer.cpp
    Source/DSP/SpectralModes.cpp)

set(AFTERIMAGE_MONOCYPHER_SOURCES
    Source/Licensing/ThirdParty/monocypher/monocypher.c
    Source/Licensing/ThirdParty/monocypher/monocypher-ed25519.c)

set(AFTERIMAGE_LICENSING_SOURCES
    Source/Licensing/LicenseDocument.cpp
    Source/Licensing/LicenseVerifier.cpp
    Source/Licensing/LicenseStorage.cpp
    Source/Licensing/MachineIdentity.cpp
    Source/Licensing/LicenseManager.cpp
    ${AFTERIMAGE_MONOCYPHER_SOURCES})

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(AFTERIMAGE
    COMPANY_NAME            "AdamAudio"
    PLUGIN_MANUFACTURER_CODE Adam
    PLUGIN_CODE             AfIm
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "AFTERIMAGE"
    BUNDLE_ID               "com.adamaudio.afterimage"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    # Keep FALSE so DAW hosts retain primary keyboard focus. ModeSelector still
    # accepts keys when it has focus inside the Standalone editor.
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES         "Fx Distortion Modulation"
    DESCRIPTION             "Spectral memory processor. Every sound leaves a ghost.")

juce_generate_juce_header(AFTERIMAGE)

set(AFTERIMAGE_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${AFTERIMAGE_DSP_SOURCES}
    Source/UI/AfterimageFonts.cpp
    Source/UI/AfterimageLookAndFeel.cpp
    Source/UI/AfterimageKnob.cpp
    Source/UI/BypassButton.cpp
    Source/UI/FreezeButton.cpp
    Source/UI/MemoryWellComponent.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/ModeSelector.cpp
    Source/UI/LicensePanel.cpp)

if(AFTERIMAGE_ENABLE_LICENSING)
    list(APPEND AFTERIMAGE_PLUGIN_SOURCES ${AFTERIMAGE_LICENSING_SOURCES})
endif()

target_sources(AFTERIMAGE PRIVATE ${AFTERIMAGE_PLUGIN_SOURCES})

target_include_directories(AFTERIMAGE PRIVATE Source Source/Licensing)

target_compile_definitions(AFTERIMAGE
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0
    PRIVATE
        $<$<BOOL:${AFTERIMAGE_ENABLE_LICENSING}>:AFTERIMAGE_ENABLE_LICENSING=1>
        $<$<BOOL:${AFTERIMAGE_USE_TEST_LICENSE_KEY}>:AFTERIMAGE_USE_TEST_LICENSE_KEY=1>)

target_link_libraries(AFTERIMAGE
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_graphics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Validation + licensing tests (no plugin client)
# ---------------------------------------------------------------------------
add_executable(AFTERIMAGE_Tests
    Tests/ValidationTests.cpp
    Tests/EffectStrengthMeasure.cpp
    Tests/EffectStrengthTests.cpp
    Tests/LicensingTests.cpp
    ${AFTERIMAGE_DSP_SOURCES}
    ${AFTERIMAGE_LICENSING_SOURCES})

target_include_directories(AFTERIMAGE_Tests PRIVATE Source Source/Licensing Tests)

target_compile_definitions(AFTERIMAGE_Tests
    PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        AFTERIMAGE_ENABLE_LICENSING=1
        AFTERIMAGE_USE_TEST_LICENSE_KEY=1
        AFTERIMAGE_LICENSE_TOOL=1
        AFTERIMAGE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tests/Fixtures")

target_link_libraries(AFTERIMAGE_Tests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events)

add_test(NAME AFTERIMAGE_Validation COMMAND AFTERIMAGE_Tests)

# ---------------------------------------------------------------------------
# Developer license tool (never linked into the plugin)
# ---------------------------------------------------------------------------
if(AFTERIMAGE_BUILD_LICENSE_TOOL)
    add_executable(AfterimageLicenseTool
        Tools/AfterimageLicenseTool/main.cpp
        Source/Licensing/LicenseDocument.cpp
        Source/Licensing/LicenseVerifier.cpp
        ${AFTERIMAGE_MONOCYPHER_SOURCES})

    target_include_directories(AfterimageLicenseTool PRIVATE Source Source/Licensing)

    set_source_files_properties(
        ${AFTERIMAGE_MONOCYPHER_SOURCES}
        PROPERTIES LANGUAGE C)

    target_compile_definitions(AfterimageLicenseTool
        PRIVATE
            JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            AFTERIMAGE_LICENSE_TOOL=1
            AFTERIMAGE_USE_TEST_LICENSE_KEY=1)

    target_link_libraries(AfterimageLicenseTool
        PRIVATE
            juce::juce_core
            juce::juce_data_structures
            juce::juce_events)
endif()

# Mark Monocypher as C for all targets that compile it
set_source_files_properties(
    ${AFTERIMAGE_MONOCYPHER_SOURCES}
    PROPERTIES LANGUAGE C)
