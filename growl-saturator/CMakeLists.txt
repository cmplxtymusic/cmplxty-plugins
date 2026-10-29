cmake_minimum_required(VERSION 3.22)
project(Growl VERSION 1.0.2 LANGUAGES C CXX)
# Linux builds are for the tests only; the box sysroot has no libxi headers
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(GW_VST3_AUTO_MANIFEST FALSE)
else()
    set(GW_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Growl
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Growl"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE GrWl
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${GW_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET Growl
    CLAP_ID "com.cmplxty.growl-saturator"
    CLAP_FEATURES audio-effect distortion filter)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(Growl_CLAP "CMPLXTY Growl" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(GWAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(GWAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Growl PRIVATE
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(Growl PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Growl PRIVATE
    GWAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(Growl PUBLIC GW_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(GrowlUpdateTest PRODUCT_NAME "GrowlUpdateTest")
target_sources(GrowlUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(GrowlUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(GrowlUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    GW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GrowlUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  GrowlScreenshot out.png [banner|menu]
juce_add_console_app(GrowlScreenshot PRODUCT_NAME "GrowlScreenshot")
target_sources(GrowlScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Presets.cpp)
target_include_directories(GrowlScreenshot PRIVATE src)
target_link_libraries(GrowlScreenshot PRIVATE GWAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(GrowlScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 GW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GrowlScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
