cmake_minimum_required(VERSION 3.22)
project(Quartz VERSION 1.0.5 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.9 (AGPLv3), patched for MinGW: see patches/juce-8.0.9-mingw.patch.
# An installed copy of the patched JUCE is used (cmake --install of ./JUCE).
set(QUARTZ_JUCE_DIR "/workspace/tc/juce-host/lib/cmake/JUCE-8.0.9" CACHE PATH "Installed JUCE cmake package dir")
find_package(JUCE CONFIG REQUIRED PATHS ${QUARTZ_JUCE_DIR} NO_DEFAULT_PATH)

set(QUARTZ_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_binary_data(QuartzAssets SOURCES
    Resources/Cinzel-SemiBold.ttf
    Resources/Inter-Regular.ttf
    Resources/GeistMono-Regular.ttf)
set_target_properties(QuartzAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(QUARTZ_COMMON_LIBS
    QuartzAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
set(QUARTZ_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                JUCE_USE_WINDOWS_MEDIA_FORMAT=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)

# --- VST3 plug-in (and optionally a Standalone app, same code) ---
set(QUARTZ_FORMATS VST3)
option(QUARTZ_STANDALONE "Also build the Standalone application" OFF)
if(QUARTZ_STANDALONE)
    list(APPEND QUARTZ_FORMATS Standalone)
endif()

juce_add_plugin(Quartz
    COMPANY_NAME "Quartz Audio"
    BUNDLE_ID com.quartzaudio.quartz
    PLUGIN_MANUFACTURER_CODE Qtzx
    PLUGIN_CODE Qrtz
    FORMATS ${QUARTZ_FORMATS}
    PRODUCT_NAME "Quartz"
    VERSION 1.0.5
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Distortion
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(Quartz PRIVATE ${QUARTZ_SOURCES})
if(WIN32)
    target_sources(Quartz PRIVATE cmake/compat/quartz_mingw_compat.cpp)
endif()
target_include_directories(Quartz PRIVATE Source)
target_link_libraries(Quartz PRIVATE ${QUARTZ_COMMON_LIBS})
target_compile_definitions(Quartz PUBLIC ${QUARTZ_DEFS})

# --- Linux-only developer tool: renders the editor to PNG for design comparison ---
if(NOT CMAKE_CROSSCOMPILING AND UNIX AND NOT APPLE)
    juce_add_console_app(QuartzShot PRODUCT_NAME "QuartzShot" BUNDLE_ID com.quartzaudio.quartzshot)
    target_sources(QuartzShot PRIVATE tools/shot_main.cpp ${QUARTZ_SOURCES})
    target_include_directories(QuartzShot PRIVATE Source)
    target_link_libraries(QuartzShot PRIVATE ${QUARTZ_COMMON_LIBS})
    target_compile_definitions(QuartzShot PUBLIC ${QUARTZ_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 QUARTZ_NO_PLUGIN_ENTRY=1)

    # UI / host-interaction tests (run under xvfb): tests/ui_test.cpp
    juce_add_console_app(QuartzUiTest PRODUCT_NAME "QuartzUiTest" BUNDLE_ID com.quartzaudio.quartzuitest)
    target_sources(QuartzUiTest PRIVATE tests/ui_test.cpp ${QUARTZ_SOURCES})
    target_include_directories(QuartzUiTest PRIVATE Source)
    target_link_libraries(QuartzUiTest PRIVATE ${QUARTZ_COMMON_LIBS})
    target_compile_definitions(QuartzUiTest PUBLIC ${QUARTZ_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1 QUARTZ_NO_PLUGIN_ENTRY=1)
endif()

# Update-check test (runs natively for the parse / compare logic, and as a static .exe under Wine for the real wininet request)
juce_add_console_app(QuartzUpdateTest PRODUCT_NAME "QuartzUpdateTest" BUNDLE_ID com.quartzaudio.quartzupdatetest)
target_sources(QuartzUpdateTest PRIVATE tests/update_test.cpp)
if(WIN32)
    target_sources(QuartzUpdateTest PRIVATE cmake/compat/quartz_mingw_compat.cpp)
endif()
target_include_directories(QuartzUpdateTest PRIVATE Source)
target_link_libraries(QuartzUpdateTest PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
target_compile_definitions(QuartzUpdateTest PUBLIC ${QUARTZ_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
set_target_properties(QuartzUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
