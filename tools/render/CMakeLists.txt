# Outil hors plugin : compile le processeur + la fenêtre d'OCTOPUS sous Linux (chaîne simulée) et exporte des captures PNG
cmake_minimum_required(VERSION 3.22)
project(OctopusRender VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} JUCE)
set(W ${CMAKE_CURRENT_SOURCE_DIR}/../..)
juce_add_binary_data(OctopusAssets SOURCES
  ${W}/background.png ${W}/fs_off.png ${W}/fs_on.png
  ${W}/fonts/Poppins-Regular.ttf ${W}/fonts/Poppins-Medium.ttf ${W}/fonts/Poppins-Bold.ttf)
juce_add_console_app(OctopusRender PRODUCT_NAME "OctopusRender")
target_sources(OctopusRender PRIVATE main.cpp ${W}/PluginProcessor.cpp ${W}/PluginEditor.cpp ${W}/OctopusEngine.cpp)
target_include_directories(OctopusRender PRIVATE ${W})
target_compile_definitions(OctopusRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JucePlugin_Name="Octopus" JucePlugin_Build_VST3=0
  JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 OCTOPUS_DEMO_ONLY=1)
target_link_libraries(OctopusRender PRIVATE OctopusAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
