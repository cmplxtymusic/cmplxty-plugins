cmake_minimum_required(VERSION 3.22)
project(Visualizer VERSION 1.0.1 LANGUAGES C CXX)
# Linux builds are for tests only; the build box sysroot has no libxi headers and the plug-in doesn't need XInput2 touch / pen input
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
add_compile_definitions(NOMINMAX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(VZ_VST3_AUTO_MANIFEST FALSE)
else()
    set(VZ_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Visualizer
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Visualizer"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE Vizl
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${VZ_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET Visualizer
    CLAP_ID "com.cmplxty.visualizer"
    CLAP_FEATURES audio-effect analyzer mono stereo)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(Visualizer_CLAP "CMPLXTY Visualizer" "CMPLXTY")
# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(VZAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(VZAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Visualizer PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/VizUI.cpp
    src/Renderer.cpp
    src/Presets.cpp)

target_compile_definitions(Visualizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE=0)

target_link_libraries(Visualizer PRIVATE
    VZAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(Visualizer PUBLIC VZ_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(VisualizerUpdateTest PRODUCT_NAME "VisualizerUpdateTest")
target_sources(VisualizerUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(VisualizerUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(VisualizerUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    VZ_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(VisualizerUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  VisualizerScreenshot out.png [banner|menu]
juce_add_console_app(VisualizerScreenshot PRODUCT_NAME "VisualizerScreenshot")
target_sources(VisualizerScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp
    src/VizUI.cpp src/Renderer.cpp src/Presets.cpp)
target_include_directories(VisualizerScreenshot PRIVATE src)
target_link_libraries(VisualizerScreenshot PRIVATE VZAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(VisualizerScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 VZ_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(VisualizerScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Analysis unit tests (level, bands, onsets, beat / tempo, host clock, FIFO under threads):  VisualizerAnalysisTest
add_executable(VisualizerAnalysisTest test/analysis_test.cpp)
set_target_properties(VisualizerAnalysisTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
find_package(Threads)
target_link_libraries(VisualizerAnalysisTest PRIVATE Threads::Threads)
