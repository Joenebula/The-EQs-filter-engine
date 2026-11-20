#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Presets for ParaEQ: built-in factory presets plus user presets saved as
    files.

    A preset holds every sound parameter (bands, dynamics, input/output gain)
    but not bypass, window size or analyser settings. Files are small XML
    documents with the extension ".paraeq":

        <ParaEQPreset name="..." version="2">
          <PARAM id="b01_freq" value="1000"/> ...
        </ParaEQPreset>

    Values are stored in real units (Hz, dB, ms), so a preset stays valid if a
    parameter's range ever changes. Parameters missing from a preset (for
    example dynamics in a preset made before they existed) get their defaults.

    User presets live in
        Windows: %APPDATA%\Home Studio\ParaEQ\Presets
    and can be imported from / exported to anywhere.

    All functions are for the message thread. Loading goes through the host
    parameters (with change gestures), so Cubase sees the changes and the
    plugin's Undo can take them back in one step.
*/
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".paraeq";
    static constexpr const char* rootTag = "ParaEQPreset";
    static constexpr int formatVersion = 2;

    PresetManager (juce::AudioProcessorValueTreeState& state, juce::UndoManager* undo);

    /** Folder for user presets (created when first needed). Tests point this
        somewhere temporary. */
    void setUserFolder (const juce::File& folder) { userFolder = folder; }
    juce::File getUserFolder() const { return userFolder; }
    static juce::File defaultUserFolder();

    // ---------------------------------------------------------------- lists
    juce::StringArray getFactoryNames() const;
    /** User preset names (file names without extension), sorted A-Z. */
    juce::StringArray getUserNames() const;
    juce::File fileForUserPreset (const juce::String& name) const;

    // ---------------------------------------------------------------- actions
    bool loadFactory (int index);
    bool loadUser (const juce::String& name);
    /** Loads any preset file (also used by import). */
    bool loadFile (const juce::File& file);

    /** Saves the current settings as a user preset (overwrites a preset with
        the same name). Returns false if the name is empty or writing failed. */
    bool saveUser (const juce::String& name);
    bool renameUser (const juce::String& oldName, const juce::String& newName);
    bool deleteUser (const juce::String& name);

    /** Writes the current settings to a file of the user's choice. */
    bool exportTo (const juce::File& file) const;
    /** Copies a preset file into the user folder (keeping its name, adding a
        number if taken) and loads it. Returns the new preset's name, or an
        empty string if the file is not a valid preset. */
    juce::String importFrom (const juce::File& file);

    /** True if a user preset with this name exists. */
    bool userPresetExists (const juce::String& name) const;
    static juce::String sanitiseName (const juce::String& name);

    // ---------------------------------------------------------------- current
    /** Name shown in the preset bar ("Default" when nothing was loaded). */
    juce::String getCurrentName() const;
    /** True if any parameter has changed since the preset was loaded/saved. */
    bool isModified() const;
    /** Takes the current settings as "unmodified" (after a project loads). */
    void resetModifiedReference() { loadedSnapshot = snapshot(); }

    // ---------------------------------------------------------------- helpers
    /** All sound parameters (everything except bypass) as a "Params" tree of
        PARAM id/value children, values in real units. */
    juce::ValueTree capture() const;
    /** Sets every sound parameter from such a tree; parameters missing from it
        get their default. Each change is sent to the host as a gesture. */
    void apply (const juce::ValueTree& params);

    /** The preset document for the current settings. */
    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name) const;

private:
    bool applyXml (const juce::XmlElement& xml, const juce::String& name);
    void markLoaded (const juce::String& name);
    std::vector<float> snapshot() const;
    juce::ValueTree buildFactory (int index) const;

    juce::AudioProcessorValueTreeState& state;
    juce::UndoManager* undoManager = nullptr;
    juce::File userFolder;
    std::vector<float> loadedSnapshot;

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};
