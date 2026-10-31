cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), minimum macOS 10.13 (must be set BEFORE project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(WigaOctopus VERSION 1.3.7)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# images and fonts are embedded into the plugin (no external file needed at runtime)
juce_add_binary_data(OctopusAssets SOURCES
  background.png fs_off.png fs_on.png design/logo_octopus_blanc.png
  fonts/Poppins-Regular.ttf fonts/Poppins-Medium.ttf fonts/Poppins-Bold.ttf fonts/IBMPlexMono-SemiBold.ttf)

# Octopus drives REAPER through its VST3 extension (IReaperHostApplication) : VST3 only
juce_add_plugin(Octopus
  COMPANY_NAME "Studio Wiga"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgOc
  FORMATS VST3
  VST3_CATEGORIES Fx Tools
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  PRODUCT_NAME "Octopus"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Octopus PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp
  OctopusEngine.cpp
  ReaperBridge.cpp)

# VST3 SDK headers shipped with JUCE (FUnknown, used to query REAPER's interface)
target_include_directories(Octopus PRIVATE
  ${JUCE_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK)

target_compile_definitions(Octopus PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Octopus
  PRIVATE
    OctopusAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
