cmake_minimum_required(VERSION 3.22)

project(TRossSeq VERSION 0.7.0)

# shown in the About window
string(TIMESTAMP TSEQ_BUILD_DATE "%d %b %Y" UTC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# JUCE: use a local checkout if -DJUCE_DIR=... is given, otherwise download it
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(TSEQ_FORMATS AU VST3)
elseif(WIN32)
    set(TSEQ_FORMATS VST3)
else()
    set(TSEQ_FORMATS VST3 LV2)
endif()

# Copy the built plug-ins into the user's plug-in folders (turned off on the build servers)
option(TSEQ_COPY_PLUGINS "Copy plug-ins to the plug-in folders after building" ON)

# An instrument with MIDI out: loads on an instrument/MIDI track in every host. Notes go to the
# host's MIDI out and/or straight to a MIDI port picked in the plug-in.
juce_add_plugin(TRossSeq
    COMPANY_NAME "Ticco Ross"
    PLUGIN_MANUFACTURER_CODE Tcro
    PLUGIN_CODE TSeq
    FORMATS ${TSEQ_FORMATS}
    LV2URI "urn:ticcoross:trossseq"
    PRODUCT_NAME "TRoss Seq"
    BUNDLE_ID com.ticcoross.trossseq
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Drum
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD ${TSEQ_COPY_PLUGINS})

target_sources(TRossSeq PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(TRossSeq PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    TSEQ_BUILD_DATE="${TSEQ_BUILD_DATE}")

target_link_libraries(TRossSeq
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional: headless test app (sequencer timing, swing, pattern queueing, recording, learn, state, editor snapshot)
option(TSEQ_BUILD_TESTS "Build the plugin test app" OFF)
if(TSEQ_BUILD_TESTS)
    juce_add_console_app(TRossSeqTest PRODUCT_NAME "TRossSeqTest")
    target_sources(TRossSeqTest PRIVATE tools/plugin_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(TRossSeqTest PRIVATE
        JucePlugin_Name="TRoss Seq" JucePlugin_VersionString="${PROJECT_VERSION}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        TSEQ_BUILD_DATE="${TSEQ_BUILD_DATE}")
    target_link_libraries(TRossSeqTest PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
