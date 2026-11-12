cmake_minimum_required(VERSION 3.22)

project(StellarStrings VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal Binary (Apple Silicon + Intel), läuft ab macOS 11
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE: entweder lokaler Checkout (-DJUCE_DIR=/pfad/zu/JUCE) oder automatisch
# von GitHub laden.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Pfad zu einem lokalen JUCE-Checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(STELLAR_FORMATS AU VST3 Standalone)
else()
    set(STELLAR_FORMATS VST3 Standalone)
endif()

juce_add_plugin(StellarStrings
    COMPANY_NAME                "Stellar Audio"
    BUNDLE_ID                   "com.stellaraudio.stellarstrings"
    PLUGIN_MANUFACTURER_CODE    Stla
    PLUGIN_CODE                 Stst
    FORMATS                     ${STELLAR_FORMATS}
    PRODUCT_NAME                "Stellar Strings"
    DESCRIPTION                 "Retro string ensemble with stereo chorus, ping-pong delay and space reverb"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument Synth
    COPY_PLUGIN_AFTER_BUILD     TRUE)

target_sources(StellarStrings PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(StellarStrings PRIVATE Source)

target_compile_definitions(StellarStrings PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(StellarStrings
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Optional: Offline-Render-Test (rendert Akkorde aller Presets als WAV)
#   cmake -B build -DSTELLAR_BUILD_RENDER_TEST=ON
# ---------------------------------------------------------------------------
option(STELLAR_BUILD_RENDER_TEST "Offline-Render-Test bauen" OFF)
if(STELLAR_BUILD_RENDER_TEST)
    juce_add_console_app(StellarRenderTest PRODUCT_NAME "StellarRenderTest")
    target_sources(StellarRenderTest PRIVATE
        tools/RenderTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(StellarRenderTest PRIVATE Source)
    target_compile_definitions(StellarRenderTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="StellarStrings"
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0)
    target_link_libraries(StellarRenderTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
