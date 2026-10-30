cmake_minimum_required(VERSION 3.22)
project(MixTools VERSION 1.0.1 LANGUAGES C CXX)
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
    set(MT_VST3_AUTO_MANIFEST FALSE)
else()
    set(MT_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(MixTools
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Mix Tools"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE Mxtl
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${MT_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET MixTools
    CLAP_ID "com.cmplxty.mix-tools"
    CLAP_FEATURES audio-effect limiter transient-shaper mastering mixing mono stereo)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(MixTools_CLAP "CMPLXTY Mix Tools" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(MTAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(MTAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(MixTools PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp)

target_compile_definitions(MixTools PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MixTools PRIVATE
    MTAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(MixTools PUBLIC MT_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(MixToolsUpdateTest PRODUCT_NAME "MixToolsUpdateTest")
target_sources(MixToolsUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(MixToolsUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(MixToolsUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    MT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(MixToolsUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  MixToolsScreenshot out.png [banner|menu]
juce_add_console_app(MixToolsScreenshot PRODUCT_NAME "MixToolsScreenshot")
target_sources(MixToolsScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp)
target_include_directories(MixToolsScreenshot PRIVATE src)
target_link_libraries(MixToolsScreenshot PRIVATE MTAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(MixToolsScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 MT_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(MixToolsScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
