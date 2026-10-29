cmake_minimum_required(VERSION 3.22)
project(RobotVoice VERSION 1.0.1 LANGUAGES C CXX)
# Linux builds are for the tests only; the box sysroot has no libxi headers
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
add_compile_definitions(NOMINMAX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(RV_VST3_AUTO_MANIFEST FALSE)
else()
    set(RV_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(RobotVoice
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Robot Voice"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE RbVo
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Pitch_Shift
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${RV_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET RobotVoice
    CLAP_ID "com.cmplxty.robot-voice"
    CLAP_FEATURES audio-effect pitch-correction distortion mono stereo)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(RobotVoice_CLAP "CMPLXTY Robot Voice" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(RVAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(RVAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(RobotVoice PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp)

target_compile_definitions(RobotVoice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(RobotVoice PRIVATE
    RVAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(RobotVoice PUBLIC RV_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(RobotVoiceUpdateTest PRODUCT_NAME "RobotVoiceUpdateTest")
target_sources(RobotVoiceUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(RobotVoiceUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(RobotVoiceUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    RV_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(RobotVoiceUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  RobotVoiceScreenshot out.png [banner|menu]
juce_add_console_app(RobotVoiceScreenshot PRODUCT_NAME "RobotVoiceScreenshot")
target_sources(RobotVoiceScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp)
target_include_directories(RobotVoiceScreenshot PRIVATE src)
target_link_libraries(RobotVoiceScreenshot PRIVATE RVAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(RobotVoiceScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 RV_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(RobotVoiceScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
