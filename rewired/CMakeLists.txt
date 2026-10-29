cmake_minimum_required(VERSION 3.22)
project(Rewired VERSION 1.0.0 LANGUAGES C CXX)
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
    set(RW_VST3_AUTO_MANIFEST FALSE)
else()
    set(RW_VST3_AUTO_MANIFEST TRUE)
endif()

add_subdirectory(ext/JUCE EXCLUDE_FROM_ALL)
add_subdirectory(ext/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(Rewired
    COMPANY_NAME "CMPLXTY"
    PRODUCT_NAME "CMPLXTY Rewired"
    PLUGIN_MANUFACTURER_CODE Cmpl
    PLUGIN_CODE RwMd
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_AUTO_MANIFEST ${RW_VST3_AUTO_MANIFEST})

clap_juce_extensions_plugin(TARGET Rewired
    CLAP_ID "com.cmplxty.rewired"
    CLAP_FEATURES audio-effect utility mono stereo)

include(cmake/win-version-info.cmake)
cmplxty_win_version_info(Rewired_CLAP "CMPLXTY Rewired" "CMPLXTY")

# bundled UI fonts (Barlow, SIL OFL 1.1 - see assets/fonts/OFL.txt)
juce_add_binary_data(RWAssets SOURCES assets/fonts/Barlow-Regular.ttf assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(RWAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Rewired PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Layout.cpp
    src/Presets.cpp
    src/RewiredEngine.cpp
    src/OrtModel.cpp)

target_compile_definitions(Rewired PUBLIC
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Rewired PRIVATE
    RWAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------------------------------- version / tests / tools
target_compile_definitions(Rewired PUBLIC RW_VERSION_STRING="${PROJECT_VERSION}")

# Update-check test: offline logic natively; the network path as a static .exe under Wine against test/update_server.py
juce_add_console_app(RewiredUpdateTest PRODUCT_NAME "RewiredUpdateTest")
target_sources(RewiredUpdateTest PRIVATE test/update_test.cpp)
target_link_libraries(RewiredUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(RewiredUpdateTest PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    RW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(RewiredUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Renders the real editor to a PNG (run under Xvfb on Linux):  RewiredScreenshot out.png [banner|menu]
juce_add_console_app(RewiredScreenshot PRODUCT_NAME "RewiredScreenshot")
target_sources(RewiredScreenshot PRIVATE test/screenshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp src/Layout.cpp src/Presets.cpp src/RewiredEngine.cpp src/OrtModel.cpp)
target_include_directories(RewiredScreenshot PRIVATE src)
target_link_libraries(RewiredScreenshot PRIVATE RWAssets juce::juce_audio_utils juce::juce_recommended_config_flags ${CMAKE_DL_LIBS})
target_compile_definitions(RewiredScreenshot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STANDALONE_APPLICATION=0 RW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(RewiredScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Command-line transcriber (accuracy tests): RewiredCli --song mix.wav --out out.mid
juce_add_console_app(RewiredCli PRODUCT_NAME "RewiredCli")
target_sources(RewiredCli PRIVATE src/RewiredCli.cpp src/RewiredEngine.cpp src/OrtModel.cpp)
target_include_directories(RewiredCli PRIVATE src)
target_link_libraries(RewiredCli PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags ${CMAKE_DL_LIBS})
target_compile_definitions(RewiredCli PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 RW_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(RewiredCli PROPERTIES EXCLUDE_FROM_ALL TRUE)
