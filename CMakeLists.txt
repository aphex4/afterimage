cmake_minimum_required(VERSION 3.22)

# CMake/JUCE VersionCode need MAJOR.MINOR.PATCH integers. Marketing RC label lives in
# AFTERIMAGE_VERSION_STRING (and docs) until ear sign-off — do not feed "1.0.0-rc.1" into
# juce_add_plugin VERSION (breaks _juce_version_code math on the "0-rc" token).
project(AFTERIMAGE VERSION 1.0.0 LANGUAGES C CXX)
set(AFTERIMAGE_VERSION_STRING "1.0.0-rc.1")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()

# Plugin formats: VST3 + Standalone always; AU on macOS.
set(AFTERIMAGE_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AFTERIMAGE_PLUGIN_FORMATS AU)
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
option(AFTERIMAGE_COMMERCIAL_RELEASE
       "Commercial packaging: fail if production license public key is placeholder or test key is on"
       OFF)
option(AFTERIMAGE_ENABLE_SANITIZERS "Build tests (and optional plugin) with ASan/UBSan" OFF)

if(AFTERIMAGE_COMMERCIAL_RELEASE)
    if(AFTERIMAGE_USE_TEST_LICENSE_KEY)
        message(FATAL_ERROR
            "AFTERIMAGE_COMMERCIAL_RELEASE=ON cannot be combined with AFTERIMAGE_USE_TEST_LICENSE_KEY=ON.")
    endif()
    message(STATUS "AFTERIMAGE: commercial release — production license key required at compile time")
endif()

if(AFTERIMAGE_USE_TEST_LICENSE_KEY AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "AFTERIMAGE_USE_TEST_LICENSE_KEY is ON for a Release build — do not ship this binary.")
endif()

# Strong warnings on project sources (JUCE itself uses juce_recommended_warning_flags).
function(afterimage_apply_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wsign-conversion
            -Wunreachable-code -Wunused
            -Wno-unused-parameter)
    endif()
endfunction()

function(afterimage_apply_sanitizers target)
    if(AFTERIMAGE_ENABLE_SANITIZERS AND NOT MSVC AND NOT WIN32)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Shared sources
# ---------------------------------------------------------------------------
set(AFTERIMAGE_DSP_SOURCES
    Source/DSP/SpectralEngine.cpp
    Source/DSP/STFTProcessor.cpp
    Source/DSP/SpectralHistoryBuffer.cpp
    Source/DSP/SpectralMemoryProfile.cpp
    Source/DSP/SpectralModes.cpp
    Source/DSP/SpectralTail.cpp
    Source/DSP/LogSmoother.cpp)

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
    FORMATS                 ${AFTERIMAGE_PLUGIN_FORMATS}
    PRODUCT_NAME            "AFTERIMAGE"
    BUNDLE_ID               "com.adamaudio.afterimage"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    # Keep FALSE so DAW hosts retain primary keyboard focus. ModeSelector still
    # accepts keys when it has focus inside the Standalone editor.
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Default off so Debug builds never overwrite ~/Library/.../AFTERIMAGE.vst3.
    # Release / commercial builds re-enable copy below.
    COPY_PLUGIN_AFTER_BUILD FALSE
    # Space-separated → JUCE joins as Fx|Filter|Modulation (not one bogus token).
    VST3_CATEGORIES         Fx Filter Modulation
    DESCRIPTION             "Spectral memory processor. Every sound leaves a ghost.")

# Host-visible JUCE VersionString stays "1.0.0" (feeds VersionCode 0x10000).
# Marketing RC label for docs / our code: AFTERIMAGE_VERSION_STRING.
target_compile_definitions(AFTERIMAGE PUBLIC
    AFTERIMAGE_VERSION_STRING="${AFTERIMAGE_VERSION_STRING}")

# Copy into user Plug-Ins only for Release single-config or commercial packaging.
if(AFTERIMAGE_COMMERCIAL_RELEASE OR (CMAKE_BUILD_TYPE STREQUAL "Release"))
    set_target_properties(AFTERIMAGE PROPERTIES JUCE_COPY_PLUGIN_AFTER_BUILD TRUE)
    message(STATUS "AFTERIMAGE: COPY_PLUGIN_AFTER_BUILD enabled (Release/commercial)")
else()
    message(STATUS "AFTERIMAGE: COPY_PLUGIN_AFTER_BUILD disabled (Debug stays in build tree)")
endif()

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
    Source/UI/GainMatchButton.cpp
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
        $<$<BOOL:${AFTERIMAGE_USE_TEST_LICENSE_KEY}>:AFTERIMAGE_USE_TEST_LICENSE_KEY=1>
        $<$<BOOL:${AFTERIMAGE_COMMERCIAL_RELEASE}>:AFTERIMAGE_COMMERCIAL_RELEASE=1>)

afterimage_apply_project_warnings(AFTERIMAGE)

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
# Validation + licensing + Gain Match tests (no plugin UI client)
# ---------------------------------------------------------------------------
add_executable(AFTERIMAGE_Tests
    Tests/ValidationTests.cpp
    Tests/EffectStrengthMeasure.cpp
    Tests/EffectStrengthTests.cpp
    Tests/LicensingTests.cpp
    Tests/GainMatchTests.cpp
    Source/PluginProcessor.cpp
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
        AFTERIMAGE_UNIT_TESTS=1
        AFTERIMAGE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tests/Fixtures"
        JucePlugin_Name="AFTERIMAGE"
        JucePlugin_Desc="AFTERIMAGE"
        JucePlugin_Manufacturer="AdamAudio"
        JucePlugin_ManufacturerWebsite=""
        JucePlugin_ManufacturerEmail=""
        JucePlugin_ManufacturerCode=0x4164616d
        JucePlugin_PluginCode=0x4166496d
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=0
        JucePlugin_Version=1.0.0
        JucePlugin_VersionCode=0x10000
        JucePlugin_VersionString="1.0.0"
        AFTERIMAGE_VERSION_STRING="${AFTERIMAGE_VERSION_STRING}"
        JucePlugin_VSTUniqueID=JucePlugin_PluginCode
        JucePlugin_VSTCategory=kPlugCategEffect)

afterimage_apply_project_warnings(AFTERIMAGE_Tests)
afterimage_apply_sanitizers(AFTERIMAGE_Tests)

target_link_libraries(AFTERIMAGE_Tests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_dsp
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
