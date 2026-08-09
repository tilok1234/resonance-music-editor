cmake_minimum_required(VERSION 3.25)

project(ResonanceMusicEditor VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "JUCE_DIR must point to a JUCE 9 checkout")
endif()

add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

juce_add_console_app(ResonanceHostProbe
    BUNDLE_ID "com.resonance.musiceditor.hostprobe"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Host Probe")

juce_generate_juce_header(ResonanceHostProbe)

target_sources(ResonanceHostProbe PRIVATE
    src/main.cpp)

target_compile_definitions(ResonanceHostProbe PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ResonanceHostProbe,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_ASIO=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_WEB_BROWSER=0)

target_link_libraries(ResonanceHostProbe PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(ResonancePluginScanner
    BUNDLE_ID "com.resonance.musiceditor.pluginscanner"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Plugin Scanner")

juce_generate_juce_header(ResonancePluginScanner)

target_sources(ResonancePluginScanner PRIVATE
    src/scanner_main.cpp)

target_compile_definitions(ResonancePluginScanner PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ResonancePluginScanner,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_ASIO=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_WEB_BROWSER=0)

target_link_libraries(ResonancePluginScanner PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(ResonancePluginInventory
    BUNDLE_ID "com.resonance.musiceditor.plugininventory"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Plugin Inventory")

juce_generate_juce_header(ResonancePluginInventory)

target_sources(ResonancePluginInventory PRIVATE
    src/inventory_main.cpp)

target_compile_definitions(ResonancePluginInventory PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ResonancePluginInventory,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_USE_CURL=0)

target_link_libraries(ResonancePluginInventory PRIVATE
    juce::juce_core
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_executable(ScannerHangFixture
    tests/scanner_hang_fixture.cpp)

set_target_properties(ScannerHangFixture PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES)

juce_add_gui_app(ResonanceMusicEditor
    BUNDLE_ID "com.resonance.musiceditor"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Music Editor")

set_target_properties(ResonanceMusicEditor PROPERTIES
    OUTPUT_NAME "ResonanceMusicEditor")

juce_generate_juce_header(ResonanceMusicEditor)

target_sources(ResonanceMusicEditor PRIVATE
    src/edit_command.cpp
    src/edit_command.h
    src/editor_component.cpp
    src/editor_component.h
    src/known_plugin.cpp
    src/known_plugin.h
    src/loop_scheduler.h
    src/mixer_snapshot.h
    src/piano_roll.cpp
    src/piano_roll.h
    src/plugin_bundle_identity.h
    src/realtime_engine.cpp
    src/realtime_engine.h
    src/realtime_main.cpp
    src/song_project.cpp
    src/song_project.h)

target_compile_definitions(ResonanceMusicEditor PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ResonanceMusicEditor,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_ASIO=0
    JUCE_DIRECTSOUND=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_CURL=0
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_WASAPI=1
    JUCE_WEB_BROWSER=0)

target_link_libraries(ResonanceMusicEditor PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(RealtimeEngineTests
    BUNDLE_ID "com.resonance.musiceditor.realtimetests"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Realtime Engine Tests")

juce_generate_juce_header(RealtimeEngineTests)

target_sources(RealtimeEngineTests PRIVATE
    src/loop_scheduler.h
    src/mixer_snapshot.h
    src/realtime_engine.cpp
    src/realtime_engine.h
    tests/realtime_engine_tests.cpp)

target_compile_definitions(RealtimeEngineTests PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RealtimeEngineTests,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_USE_CURL=0)

target_link_libraries(RealtimeEngineTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors_headless
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(SongProjectTests
    BUNDLE_ID "com.resonance.musiceditor.songprojecttests"
    COMPANY_NAME "Resonance Music Editor"
    PRODUCT_NAME "Resonance Song Project Tests")

juce_generate_juce_header(SongProjectTests)

target_sources(SongProjectTests PRIVATE
    src/edit_command.cpp
    src/edit_command.h
    src/loop_scheduler.h
    src/song_project.cpp
    src/song_project.h
    tests/song_project_tests.cpp)

target_compile_definitions(SongProjectTests PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SongProjectTests,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_USE_CURL=0)

target_link_libraries(SongProjectTests PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
