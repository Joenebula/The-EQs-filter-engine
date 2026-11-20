# Plugin identity. Change the names here; keep the codes stable once you have
# used the plugin in a Cubase project, or Cubase will treat it as a new plugin.
set(PEQ_COMPANY_NAME     "Home Studio")
set(PEQ_PRODUCT_NAME     "ParaEQ")
set(PEQ_MANUFACTURER_CODE "Hmst")   # 4 chars, at least one upper-case
set(PEQ_PLUGIN_CODE      "Peq1")    # 4 chars, unique per plugin

juce_add_plugin(ParaEQ
    COMPANY_NAME                 "${PEQ_COMPANY_NAME}"
    BUNDLE_ID                    com.homestudio.paraeq
    PRODUCT_NAME                 "${PEQ_PRODUCT_NAME}"
    PLUGIN_MANUFACTURER_CODE     ${PEQ_MANUFACTURER_CODE}
    PLUGIN_CODE                  ${PEQ_PLUGIN_CODE}
    FORMATS                      VST3 Standalone
    VST3_CATEGORIES              Fx EQ
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    COPY_PLUGIN_AFTER_BUILD      ${PEQ_COPY_AFTER_BUILD})

target_sources(ParaEQ PRIVATE
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
    Parameters.h
    Parameters.cpp
    GUI/GraphMath.h
    GUI/Theme.h
    GUI/BandParams.h
    GUI/ParaLookAndFeel.h
    GUI/EQGraph.h
    GUI/EQGraph.cpp
    GUI/BandPanel.h
    GUI/BandPanel.cpp)

target_compile_definitions(ParaEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ParaEQ
    PRIVATE
        peq_dsp
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless GUI test: drives the editor through its public actions and saves
# screenshots. Development only (needs a display, e.g. Xvfb on Linux).
if(PEQ_BUILD_GUI_TESTS)
    add_executable(peq_gui_tests ../Tests/GuiTests.cpp)
    target_link_libraries(peq_gui_tests PRIVATE ParaEQ)
    # Compile against the same JUCE headers and settings as the plugin code.
    target_include_directories(peq_gui_tests PRIVATE $<TARGET_PROPERTY:ParaEQ,INCLUDE_DIRECTORIES>)
    target_compile_definitions(peq_gui_tests PRIVATE $<TARGET_PROPERTY:ParaEQ,COMPILE_DEFINITIONS>)
endif()
