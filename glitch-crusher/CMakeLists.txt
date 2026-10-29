cmake_minimum_required(VERSION 3.22)
project(GlitchCrusher VERSION 1.0.3 LANGUAGES C CXX)
# Linux builds are for the tests only; the box sysroot has no libxi headers
if(UNIX AND NOT APPLE)
    add_compile_definitions(JUCE_USE_XINPUT=0)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# When cross-compiling, the VST3 manifest helper cannot be executed on the build host;
# moduleinfo.json is platform independent and is copied from a native build by the packaging script.
if(CMAKE_CROSSCOMPILING)
    set(GC_VST3_AUTO_MANIFEST FALSE)
else()
    set(GC_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(GlitchCrusher
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Glitch Crusher"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE GlCr
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${GC_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET GlitchCrusher
    CLAP_ID "com.cmplxty.glitch-crusher"
    CLAP_FEATURES audio-effect distortion filter)

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(GCAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(GCAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(GlitchCrusher PRIVATE
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(GlitchCrusher PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(GlitchCrusher PRIVATE
    GCAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(GlitchCrusher PUBLIC GC_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(GlitchCrusherUpdateTest PRODUCT_NAME "GlitchCrusherUpdateTest")
target_sources(GlitchCrusherUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(GlitchCrusherUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(GlitchCrusherUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    GC_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GlitchCrusherUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  GlitchCrusherScreenshot out.png [banner|menu]
juce_add_console_app(GlitchCrusherScreenshot PRODUCT_NAME "GlitchCrusherScreenshot")
target_sources(GlitchCrusherScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Presets.cpp)
target_include_directories(GlitchCrusherScreenshot PRIVATE src)
target_link_libraries(GlitchCrusherScreenshot PRIVATE GCAssets juce::juce_audio_utils juce::juce_recommended_config_flags)
target_compile_definitions(GlitchCrusherScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 GC_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(GlitchCrusherScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
