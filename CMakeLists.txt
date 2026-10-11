cmake_minimum_required(VERSION 3.22)

project(HybridPan VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Get JUCE.
#
# Option A (default): fetch JUCE automatically. Requires network at configure
# time. Pin to a recent JUCE 7/8 release.
#
# Option B: if you already have JUCE checked out locally, comment out the
# FetchContent block below and instead do:
#     add_subdirectory(/path/to/JUCE ${CMAKE_BINARY_DIR}/JUCE)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4          # any recent 7.x/8.x tag works
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(HybridPan
    COMPANY_NAME              "Austin Arias"
    PLUGIN_MANUFACTURER_CODE  Aria
    PLUGIN_CODE               Hpan
    FORMATS                   VST3 AU Standalone
    PRODUCT_NAME              "Hybrid Pan"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
)

target_sources(HybridPan PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(HybridPan PRIVATE Source)

target_compile_definitions(HybridPan PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(HybridPan PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)