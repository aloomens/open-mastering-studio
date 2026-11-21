cmake_minimum_required(VERSION 3.22)

project(MasteringStudio VERSION 1.8.1 LANGUAGES C CXX)

# Product name shown to users (files, window, DAW). The CMake target, plugin codes and the saved-state tag
# stay "MasteringStudio" / Msto / Msst, so sessions made with earlier versions still find and load the plugin.
set(MS_PRODUCT_NAME "Open Mastering Studio")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)

juce_add_plugin(MasteringStudio
    COMPANY_NAME "${MS_PRODUCT_NAME}"
    PRODUCT_NAME "${MS_PRODUCT_NAME}"
    PLUGIN_MANUFACTURER_CODE Msto
    PLUGIN_CODE Msst
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_big.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon_small.png"
    VST3_CATEGORIES Fx Mastering Dynamics EQ)

# JUCE regenerates the Windows version resource (.rc: the version Windows shows in a file's
# Properties) only when the icon changes, not when the version does. Remove it whenever the version
# changes so the next build writes it again with the current number.
get_target_property(ms_generated_dir MasteringStudio JUCE_GENERATED_SOURCES_DIRECTORY)
if(NOT "${MS_LAST_RC_VERSION}" STREQUAL "${PROJECT_VERSION}")
    file(REMOVE "${ms_generated_dir}/MasteringStudio_resources.rc")   # also picks up a product-name change
    set(MS_LAST_RC_VERSION "${PROJECT_VERSION}" CACHE INTERNAL "Version the Windows resource file was last generated for")
endif()

set(MS_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Assistant.cpp
    Source/ModuleViews.cpp)

set(MS_DEFS
    MS_VERSION="${PROJECT_VERSION}"
    MS_PRODUCT_NAME="${MS_PRODUCT_NAME}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

# App icon as embedded data (title bar of the standalone window). Regenerate with Tools/make_icon.ps1.
juce_add_binary_data(MasteringStudioAssets SOURCES Resources/icon_big.png Resources/icon_small.png)
set_target_properties(MasteringStudioAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(MasteringStudio PRIVATE ${MS_SOURCES})
target_compile_definitions(MasteringStudio PUBLIC ${MS_DEFS})

# ---- ASIO for the standalone app (Steinberg ASIO SDK in external/asiosdk) ----
set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/asiosdk" CACHE PATH "Folder containing the ASIO SDK (with common/iasiodrv.h)")
if(WIN32 AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
    message(STATUS "ASIO SDK found at ${ASIO_SDK_DIR} - standalone app will support ASIO")
    target_compile_definitions(MasteringStudio PUBLIC JUCE_ASIO=1)
    target_include_directories(MasteringStudio PUBLIC "${ASIO_SDK_DIR}/common")
elseif(WIN32)
    message(WARNING "ASIO SDK not found in ${ASIO_SDK_DIR} - standalone app will only offer Windows Audio/DirectSound")
endif()

target_link_libraries(MasteringStudio
    PRIVATE
        MasteringStudioAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline DSP / preset test (compiles the plugin sources into a console app) ----
option(MS_BUILD_TESTS "Build the offline DSP test" ON)
if(MS_BUILD_TESTS)
    juce_add_console_app(MasteringTest PRODUCT_NAME "MasteringTest")
    target_sources(MasteringTest PRIVATE Tests/MasteringTest.cpp Tests/LabMeasurements.cpp ${MS_SOURCES})
    target_compile_definitions(MasteringTest PRIVATE ${MS_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MasteringTest
        PRIVATE MasteringStudioAssets juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
