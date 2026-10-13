cmake_minimum_required(VERSION 3.22)

project(KickDuck1 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(KICKDUCK_COPY_PLUGIN_AFTER_BUILD "Copy VST3 into the user plugin directory" ON)

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

    COPY_PLUGIN_AFTER_BUILD ${KICKDUCK_COPY_PLUGIN_AFTER_BUILD}
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

include(CTest)
if(BUILD_TESTING)
    add_executable(KickDuck1Tests Tests/ProcessorTests.cpp)
    target_include_directories(KickDuck1Tests PRIVATE
        Source
        "$<TARGET_PROPERTY:KickDuck1,INCLUDE_DIRECTORIES>")
    target_compile_definitions(KickDuck1Tests PRIVATE
        "$<TARGET_PROPERTY:KickDuck1,COMPILE_DEFINITIONS>")
    target_link_libraries(KickDuck1Tests PRIVATE KickDuck1)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(KickDuck1Tests PRIVATE Tests/AllocationProbe.cpp)
        target_compile_definitions(KickDuck1Tests PRIVATE KICKDUCK_ALLOCATION_PROBE=1)
        target_link_options(KickDuck1Tests PRIVATE
            -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
            -Wl,--wrap=posix_memalign -Wl,--wrap=aligned_alloc)
    endif()
    find_package(Threads REQUIRED)
    add_executable(KickDuck1RealtimeTests Tests/RealtimeTests.cpp)
    target_include_directories(KickDuck1RealtimeTests PRIVATE Source)
    target_link_libraries(KickDuck1RealtimeTests PRIVATE Threads::Threads)
    add_test(NAME KickDuck1Realtime COMMAND KickDuck1RealtimeTests)
    set_tests_properties(KickDuck1Realtime PROPERTIES TIMEOUT 30)
    add_test(NAME KickDuck1Processor COMMAND KickDuck1Tests)
    set_tests_properties(KickDuck1Processor PROPERTIES TIMEOUT 30)
endif()
