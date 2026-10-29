cmake_minimum_required(VERSION 3.22)
project(Granular VERSION 1.0.0 LANGUAGES C CXX)
# Linux builds are for the tests only; the box sysroot has no libxi headers and plug-ins don't need XInput2 touch / pen input
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
add_compile_definitions(NOMINMAX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(GR_VST3_AUTO_MANIFEST FALSE)
else()
    set(GR_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Granular
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Granular"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE Grnl
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${GR_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET Granular
    CLAP_ID "com.cmplxty.granular"
    CLAP_FEATURES audio-effect granular glitch mono stereo)

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(GRAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(GRAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Granular PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp)

target_compile_definitions(Granular PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Granular PRIVATE
    GRAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(Granular PUBLIC GR_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(GranularUpdateTest PRODUCT_NAME "GranularUpdateTest")
target_sources(GranularUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(GranularUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(GranularUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    GR_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GranularUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  GranularScreenshot out.png [banner|menu]
juce_add_console_app(GranularScreenshot PRODUCT_NAME "GranularScreenshot")
target_sources(GranularScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp)
target_include_directories(GranularScreenshot PRIVATE src)
target_link_libraries(GranularScreenshot PRIVATE GRAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(GranularScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 GR_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GranularScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
