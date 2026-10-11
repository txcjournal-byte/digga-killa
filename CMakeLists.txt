cmake_minimum_required(VERSION 3.22)

project(TrapWinSample VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# JUCE
#   - default: fetched from GitHub at a pinned tag
#   - offline / faster: cmake -DTRAPWIN_JUCE_PATH=/path/to/JUCE ..
# ------------------------------------------------------------------------------
set(TRAPWIN_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(TRAPWIN_JUCE_PATH)
    add_subdirectory(${TRAPWIN_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------
juce_add_plugin(TrapWinSample
    PRODUCT_NAME                "TrapWin Sample"
    COMPANY_NAME                "TrapWin"
    COMPANY_WEBSITE             "https://trapwin.com"
    COMPANY_EMAIL               "txcjournal@gmail.com"
    BUNDLE_ID                   com.trapwin.sample
    PLUGIN_MANUFACTURER_CODE    Trwn
    PLUGIN_CODE                 Twsp
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    HARDENED_RUNTIME_ENABLED    TRUE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    FORMATS                     VST3 AU Standalone)

juce_add_binary_data(TrapWinAssets
    SOURCES
        assets/fonts/ArchivoDisplay-BlackItalic.ttf
        assets/fonts/Archivo-ExtraBold.ttf
        assets/fonts/Archivo-SemiBold.ttf
        assets/fonts/IBMPlexMono-Medium.ttf
        assets/fonts/IBMPlexMono-SemiBold.ttf
        assets/models/basic_pitch.bin)

target_sources(TrapWinSample PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/core/AudioTools.cpp
    source/core/Edit.cpp
    source/core/Engine.cpp
    source/core/JobQueue.cpp
    source/core/SampleStore.cpp
    source/analysis/Features.cpp
    source/analysis/Analyzer.cpp
    source/analysis/Structure.cpp
    source/loops/LoopGenerator.cpp
    source/oneshots/OneShotExtractor.cpp
    source/dsp/Stretcher.cpp
    source/export/Exporter.cpp
    source/midi/BasicPitch.cpp
    source/midi/MidiExport.cpp
    source/playback/SamplePlayer.cpp
    source/playback/ClipPlayer.cpp
    source/ui/Theme.cpp
    source/ui/TrapWinLookAndFeel.cpp
    source/ui/FormatSwitch.cpp
    source/ui/Header.cpp
    source/ui/Vinyl.cpp
    source/ui/SampleList.cpp
    source/ui/EditorPanel.cpp
    source/ui/MainView.cpp)

# signalsmith-stretch 1.4.0 + signalsmith-linear 0.6.4 (MIT), vendored header-only
target_include_directories(TrapWinSample PRIVATE source)
target_include_directories(TrapWinSample SYSTEM PRIVATE
    third_party/signalsmith-stretch/include
    third_party/signalsmith-linear/include)

target_compile_definitions(TrapWinSample PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(TrapWinSample
    PRIVATE
        TrapWinAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Dev tool: headless UI snapshot (cmake -DTRAPWIN_BUILD_TOOLS=ON)
# ------------------------------------------------------------------------------
option(TRAPWIN_BUILD_TOOLS "Build developer tools" OFF)

if(TRAPWIN_BUILD_TOOLS)
    juce_add_console_app(TrapWinTest PRODUCT_NAME "TrapWinTest")
    target_sources(TrapWinTest PRIVATE tools/Snapshot.cpp)
    # reuse the plugin's compiled JUCE modules instead of building them twice
    target_include_directories(TrapWinTest PRIVATE source
        $<TARGET_PROPERTY:TrapWinSample,INCLUDE_DIRECTORIES>)
    target_compile_definitions(TrapWinTest PRIVATE
        $<TARGET_PROPERTY:TrapWinSample,COMPILE_DEFINITIONS>)
    target_link_libraries(TrapWinTest PRIVATE TrapWinSample)
endif()
