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
# Shared DSP sources (plugin + tests)
# ---------------------------------------------------------------------------
set(AFTERIMAGE_DSP_SOURCES
    Source/DSP/SpectralEngine.cpp
    Source/DSP/STFTProcessor.cpp
    Source/DSP/SpectralHistoryBuffer.cpp
    Source/DSP/SpectralModes.cpp)

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
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES         "Fx Distortion Modulation"
    DESCRIPTION             "Spectral memory processor — every sound leaves a ghost.")

juce_generate_juce_header(AFTERIMAGE)

target_sources(AFTERIMAGE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${AFTERIMAGE_DSP_SOURCES}
    Source/UI/AfterimageLookAndFeel.cpp
    Source/UI/AfterimageKnob.cpp
    Source/UI/MemoryWellComponent.cpp
    Source/UI/SpectrumDisplay.cpp
    Source/UI/ModeSelector.cpp)

target_include_directories(AFTERIMAGE PRIVATE Source)

target_compile_definitions(AFTERIMAGE
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0)

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
# Validation tests (no plugin client)
# ---------------------------------------------------------------------------
add_executable(AFTERIMAGE_Tests
    Tests/ValidationTests.cpp
    ${AFTERIMAGE_DSP_SOURCES})

target_include_directories(AFTERIMAGE_Tests PRIVATE Source)

target_compile_definitions(AFTERIMAGE_Tests
    PRIVATE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(AFTERIMAGE_Tests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events)

add_test(NAME AFTERIMAGE_Validation COMMAND AFTERIMAGE_Tests)
