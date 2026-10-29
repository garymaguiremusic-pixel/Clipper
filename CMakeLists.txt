cmake_minimum_required(VERSION 3.22)

project(SolidClip VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(SOLIDCLIP_BUILD_AU         "Also build the Audio Unit (macOS only)"                      ON)
option(SOLIDCLIP_BUILD_STANDALONE "Also build a standalone app for quick testing"               ON)
option(SOLIDCLIP_BUILD_TESTS      "Build the SolidClipTests console app"                        ON)
option(SOLIDCLIP_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Sources shared by the plugin and the tests
set(SOLIDCLIP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/SolidComponents.cpp
    Source/DSP/ClipEngine.cpp)

set(SOLIDCLIP_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ------------------------------------------------------------------------------
# Plugin
set(SOLIDCLIP_FORMATS VST3)
if(APPLE AND SOLIDCLIP_BUILD_AU)
    list(APPEND SOLIDCLIP_FORMATS AU)
endif()
if(SOLIDCLIP_BUILD_STANDALONE)
    list(APPEND SOLIDCLIP_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine, which isn't possible
# when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(SOLIDCLIP_VST3_MANIFEST FALSE)
else()
    set(SOLIDCLIP_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(SolidClip
    PRODUCT_NAME                "SolidClip"
    COMPANY_NAME                "SolidClip Project"
    BUNDLE_ID                   com.solidclipproject.solidclip
    PLUGIN_MANUFACTURER_CODE    Scpj
    PLUGIN_CODE                 Sclp
    FORMATS                     ${SOLIDCLIP_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    VST3_AUTO_MANIFEST          ${SOLIDCLIP_VST3_MANIFEST}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${SOLIDCLIP_COPY_AFTER_BUILD})

target_sources(SolidClip PRIVATE ${SOLIDCLIP_SOURCES})
target_include_directories(SolidClip PRIVATE Source)
target_compile_definitions(SolidClip PUBLIC ${SOLIDCLIP_DEFINITIONS})

target_link_libraries(SolidClip
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Tests
if(SOLIDCLIP_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(SolidClipTests PRODUCT_NAME "SolidClipTests")
    target_sources(SolidClipTests PRIVATE Tests/SolidClipTests.cpp ${SOLIDCLIP_SOURCES})
    target_include_directories(SolidClipTests PRIVATE Source)
    target_compile_definitions(SolidClipTests PRIVATE
        ${SOLIDCLIP_DEFINITIONS}
        SOLIDCLIP_NO_PLUGIN_ENTRY=1
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(SolidClipTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME SolidClipTests COMMAND SolidClipTests)
endif()
