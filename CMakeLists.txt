cmake_minimum_required(VERSION 3.22)

project(AFTERIMAGE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Universal binary on macOS when architectures are not otherwise specified.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE
#   1) -DJUCE_PATH=/path/to/JUCE   (preferred when a local checkout exists)
#   2) Otherwise FetchContent pulls JUCE 8.0.6
# ---------------------------------------------------------------------------
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

    Source/DSP/SpectralEngine.cpp
    Source/DSP/STFTProcessor.cpp
    Source/DSP/SpectralHistoryBuffer.cpp
    Source/DSP/SpectralModes.cpp

    Source/UI/AfterimageLookAndFeel.cpp
    Source/UI/MemoryPoolComponent.cpp
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
