cmake_minimum_required(VERSION 3.22)

project(Catnip VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# JUCE: use a local checkout if one is given (-DJUCE_PATH=...), otherwise download it.
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Catnip
    COMPANY_NAME "KINAI"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.kinai.catnip
    PLUGIN_MANUFACTURER_CODE Kina
    PLUGIN_CODE Ctnp
    PRODUCT_NAME "Catnip"
    DESCRIPTION "Drop a sample, get chops, play pads."
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_add_binary_data(CatnipData SOURCES
    resources/VT323-Regular.ttf
    resources/Archivo-Regular.ttf
    resources/Archivo-SemiBold.ttf
    resources/Archivo-ExtraBold.ttf
    resources/kinai.png
    resources/tex_plastic.jpg
    resources/tex_pad.jpg
    resources/tex_key.jpg)

target_sources(Catnip PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ChopAnalysis.cpp)

target_include_directories(Catnip PRIVATE Source)

target_compile_definitions(Catnip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Catnip
    PRIVATE
        CatnipData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Small offline test for the chop analysis (not shipped).
option(CATNIP_BUILD_TESTS "Build the offline analysis test" OFF)
if(CATNIP_BUILD_TESTS)
    juce_add_console_app(CatnipTest PRODUCT_NAME "CatnipTest")
    target_sources(CatnipTest PRIVATE Tests/AnalysisTest.cpp Source/ChopAnalysis.cpp)
    target_include_directories(CatnipTest PRIVATE Source)
    target_compile_definitions(CatnipTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(CatnipTest PRIVATE juce::juce_audio_basics juce::juce_core
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    juce_add_console_app(CatnipEngineTest PRODUCT_NAME "CatnipEngineTest")
    target_sources(CatnipEngineTest PRIVATE Tests/EngineTest.cpp Source/PluginProcessor.cpp
        Source/PluginEditor.cpp Source/ChopAnalysis.cpp)
    target_include_directories(CatnipEngineTest PRIVATE Source)
    target_compile_definitions(CatnipEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(CatnipEngineTest PRIVATE CatnipData juce::juce_audio_utils juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
