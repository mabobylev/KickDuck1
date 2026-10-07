cmake_minimum_required(VERSION 3.22)

project(KickDuck1 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(KickDuck1
    COMPANY_NAME "KickDuck"
    BUNDLE_ID "com.kickduck.KickDuck1"

    PLUGIN_MANUFACTURER_CODE Kdck
    PLUGIN_CODE Kd01

    FORMATS VST3 Standalone

    PRODUCT_NAME "KickDuck1"

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(KickDuck1)

target_sources(KickDuck1 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h

    Source/PluginEditor.cpp
    Source/PluginEditor.h

    Source/DuckingCurve.cpp
    Source/DuckingCurve.h

    Source/WaveformHistory.cpp
    Source/WaveformHistory.h

    Source/PeakMeter.cpp
    Source/PeakMeter.h
)

target_compile_definitions(KickDuck1 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(KickDuck1 PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
)
