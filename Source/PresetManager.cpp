#include "PresetManager.h"
#include "Parameters.h"

namespace
{
    const juce::Identifier presetNameId { "presetName" };
    const juce::Identifier paramsTag { "Params" };
    const juce::Identifier paramTag { "PARAM" };
    const juce::Identifier idAttr { "id" };
    const juce::Identifier valueAttr { "value" };

    bool isSoundParameter (const juce::String& id)
    {
        return id != peq::params::bypass;
    }

    //==========================================================================
    // Factory presets. Each is a list of bands; everything not listed stays at
    // its default (unused bands, dynamics off, 0 dB input/output).
    struct Band
    {
        int type;            // index into the Type list (0 Bell, 1 LowShelf, 2 HighShelf, 3 LowPass, 4 HighPass, 5 Notch, 6 BandPass)
        float freq;
        float gain = 0.0f;
        float q = 0.7071f;
        int slopeIndex = 1;  // 0=6, 1=12, 2=18, 3=24, 4=36, 5=48 dB/oct
        int mode = 0;        // 0 Stereo, 1 Left, 2 Right, 3 Mid, 4 Side
        bool dynamic = false;
        float threshold = -24.0f, range = -6.0f, attack = 10.0f, release = 150.0f;
    };

    enum Type { bell = 0, lowShelf, highShelf, lowPass, highPass, notch, bandPass };
    enum Mode { stereo = 0, left, right, mid, side };

    struct Factory
    {
        const char* name;
        std::vector<Band> bands;
    };

    const std::vector<Factory>& factoryPresets()
    {
        static const std::vector<Factory> presets
        {
            { "Default (flat)", {} },
            { "Vocal - clarity", {
                { highPass, 80.0f, 0.0f, 0.7071f, 1 },
                { bell, 250.0f, -2.5f, 1.2f },
                { bell, 3500.0f, 2.5f, 0.9f },
                { highShelf, 12000.0f, 2.0f } } },
            { "Vocal - de-ess (dynamic)", {
                { highPass, 70.0f, 0.0f, 0.7071f, 1 },
                { bell, 6500.0f, 0.0f, 3.0f, 1, stereo, true, -30.0f, -6.0f, 2.0f, 80.0f } } },
            { "Vocal - tame boom (dynamic)", {
                { highPass, 60.0f, 0.0f, 0.7071f, 2 },
                { bell, 220.0f, 0.0f, 1.4f, 1, stereo, true, -28.0f, -4.0f, 15.0f, 200.0f } } },
            { "Kick - punch", {
                { highPass, 30.0f, 0.0f, 0.7071f, 3 },
                { bell, 60.0f, 3.0f, 1.4f },
                { bell, 350.0f, -4.0f, 1.5f },
                { bell, 3500.0f, 3.0f, 1.2f } } },
            { "Snare - body and crack", {
                { highPass, 80.0f, 0.0f, 0.7071f, 1 },
                { bell, 200.0f, 2.0f, 1.2f },
                { bell, 900.0f, -2.0f, 2.0f },
                { bell, 5000.0f, 2.5f, 1.0f } } },
            { "Bass - tidy lows", {
                { highPass, 35.0f, 0.0f, 0.7071f, 3 },
                { bell, 120.0f, 0.0f, 1.0f, 1, stereo, true, -24.0f, -4.0f, 20.0f, 250.0f },
                { bell, 800.0f, 1.5f, 1.0f } } },
            { "Acoustic guitar - sparkle", {
                { highPass, 90.0f, 0.0f, 0.7071f, 1 },
                { bell, 200.0f, -3.0f, 1.0f },
                { highShelf, 10000.0f, 3.0f } } },
            { "Drum bus - glue", {
                { lowShelf, 80.0f, 1.5f },
                { bell, 400.0f, -1.5f, 0.8f },
                { highShelf, 10000.0f, 1.5f } } },
            { "Master - gentle tilt", {
                { lowShelf, 150.0f, -1.0f },
                { highShelf, 5000.0f, 1.5f } } },
            { "Master - mid/side width", {
                { highPass, 120.0f, 0.0f, 0.7071f, 1, side },
                { highShelf, 8000.0f, 2.0f, 0.7071f, 1, side },
                { bell, 3000.0f, 0.5f, 0.8f, 1, mid } } },
            { "Low cut 80 Hz", {
                { highPass, 80.0f, 0.0f, 0.7071f, 2 } } },
            { "Air boost", {
                { highShelf, 14000.0f, 3.0f } } },
            { "Telephone effect", {
                { highPass, 400.0f, 0.0f, 0.7071f, 5 },
                { lowPass, 3400.0f, 0.0f, 0.7071f, 5 },
                { bell, 1500.0f, 4.0f, 1.0f } } },
        };
        return presets;
    }
}

//==============================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um)
    : state (s), undoManager (um), userFolder (defaultUserFolder())
{
    loadedSnapshot = snapshot();
}

juce::File PresetManager::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Home Studio").getChildFile ("ParaEQ").getChildFile ("Presets");
}

//==============================================================================
juce::StringArray PresetManager::getFactoryNames() const
{
    juce::StringArray names;
    for (const auto& f : factoryPresets())
        names.add (f.name);
    return names;
}

juce::StringArray PresetManager::getUserNames() const
{
    juce::StringArray names;
    if (userFolder.isDirectory())
        for (const auto& f : userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
            names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::String PresetManager::sanitiseName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim().substring (0, 80);
}

juce::File PresetManager::fileForUserPreset (const juce::String& name) const
{
    return userFolder.getChildFile (sanitiseName (name) + fileExtension);
}

bool PresetManager::userPresetExists (const juce::String& name) const
{
    return sanitiseName (name).isNotEmpty() && fileForUserPreset (name).existsAsFile();
}

//==============================================================================
juce::ValueTree PresetManager::capture() const
{
    juce::ValueTree params (paramsTag);
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isSoundParameter (rp->paramID))
            {
                juce::ValueTree child (paramTag);
                child.setProperty (idAttr, rp->paramID, nullptr);
                child.setProperty (valueAttr, rp->convertFrom0to1 (rp->getValue()), nullptr);
                params.appendChild (child, nullptr);
            }
    return params;
}

void PresetManager::apply (const juce::ValueTree& params)
{
    for (auto* p : state.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! isSoundParameter (rp->paramID))
            continue;

        const auto child = params.getChildWithProperty (idAttr, rp->paramID);
        const float normalised = child.isValid()
                                     ? rp->convertTo0to1 ((float) (double) child.getProperty (valueAttr))
                                     : rp->getDefaultValue();
        if (! juce::exactlyEqual (normalised, rp->getValue()))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (normalised);
            rp->endChangeGesture();
        }
    }
}

std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> (rootTag);
    xml->setAttribute ("name", name);
    xml->setAttribute ("version", formatVersion);
    const auto params = capture();
    for (const auto& child : params)
    {
        auto* e = xml->createNewChildElement (paramTag.toString());
        e->setAttribute (idAttr, child.getProperty (idAttr).toString());
        e->setAttribute (valueAttr, (double) child.getProperty (valueAttr));
    }
    return xml;
}

bool PresetManager::applyXml (const juce::XmlElement& xml, const juce::String& name)
{
    if (! xml.hasTagName (rootTag))
        return false;

    juce::ValueTree params (paramsTag);
    for (auto* e : xml.getChildWithTagNameIterator (paramTag.toString()))
    {
        if (! e->hasAttribute (idAttr.toString()) || ! e->hasAttribute (valueAttr.toString()))
            continue;
        juce::ValueTree child (paramTag);
        child.setProperty (idAttr, e->getStringAttribute (idAttr), nullptr);
        child.setProperty (valueAttr, e->getDoubleAttribute (valueAttr), nullptr);
        params.appendChild (child, nullptr);
    }

    if (undoManager != nullptr)
    {
        state.copyState();   // flush pending edits into their own undo step
        undoManager->beginNewTransaction ("Load preset");
    }
    apply (params);
    state.copyState();
    markLoaded (name);
    return true;
}

juce::ValueTree PresetManager::buildFactory (int index) const
{
    using peq::params::BandField;
    using peq::params::bandID;
    juce::ValueTree params (paramsTag);
    if (index < 0 || index >= (int) factoryPresets().size())
        return params;

    auto add = [&params] (const juce::String& id, double value)
    {
        juce::ValueTree child (paramTag);
        child.setProperty (idAttr, id, nullptr);
        child.setProperty (valueAttr, value, nullptr);
        params.appendChild (child, nullptr);
    };

    const auto& bands = factoryPresets()[(size_t) index].bands;
    for (int b = 0; b < (int) bands.size() && b < peq::params::numBands; ++b)
    {
        const auto& s = bands[(size_t) b];
        add (bandID (b, BandField::used), 1.0);
        add (bandID (b, BandField::on), 1.0);
        add (bandID (b, BandField::type), s.type);
        add (bandID (b, BandField::freq), s.freq);
        add (bandID (b, BandField::gain), s.gain);
        add (bandID (b, BandField::q), s.q);
        add (bandID (b, BandField::slope), s.slopeIndex);
        add (bandID (b, BandField::mode), s.mode);
        add (bandID (b, BandField::dyn), s.dynamic ? 1.0 : 0.0);
        add (bandID (b, BandField::thr), s.threshold);
        add (bandID (b, BandField::range), s.range);
        add (bandID (b, BandField::att), s.attack);
        add (bandID (b, BandField::rel), s.release);
    }
    // The other bands' frequencies keep their spread-out defaults (missing =
    // default), so a band added later still lands somewhere sensible.
    return params;
}

//==============================================================================
bool PresetManager::loadFactory (int index)
{
    if (index < 0 || index >= (int) factoryPresets().size())
        return false;
    auto xml = std::make_unique<juce::XmlElement> (rootTag);
    for (const auto& child : buildFactory (index))
    {
        auto* e = xml->createNewChildElement (paramTag.toString());
        e->setAttribute (idAttr, child.getProperty (idAttr).toString());
        e->setAttribute (valueAttr, (double) child.getProperty (valueAttr));
    }
    return applyXml (*xml, factoryPresets()[(size_t) index].name);
}

bool PresetManager::loadUser (const juce::String& name)
{
    const auto file = fileForUserPreset (name);
    if (! file.existsAsFile())
        return false;
    auto xml = juce::XmlDocument::parse (file);
    return xml != nullptr && applyXml (*xml, file.getFileNameWithoutExtension());
}

bool PresetManager::loadFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;
    const auto name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    return applyXml (*xml, name.isNotEmpty() ? name : file.getFileNameWithoutExtension());
}

bool PresetManager::saveUser (const juce::String& rawName)
{
    const auto name = sanitiseName (rawName);
    if (name.isEmpty())
        return false;
    if (! userFolder.createDirectory())
        return false;
    const auto file = fileForUserPreset (name);
    if (! createPresetXml (name)->writeTo (file))
        return false;
    markLoaded (name);
    return true;
}

bool PresetManager::renameUser (const juce::String& oldName, const juce::String& rawNewName)
{
    const auto newName = sanitiseName (rawNewName);
    const auto from = fileForUserPreset (oldName);
    if (newName.isEmpty() || ! from.existsAsFile())
        return false;
    const auto to = fileForUserPreset (newName);
    if (to.existsAsFile() && to != from)
        return false;

    // Keep the name inside the file in step with the file name.
    auto xml = juce::XmlDocument::parse (from);
    if (xml == nullptr)
        return false;
    xml->setAttribute ("name", newName);
    if (! xml->writeTo (to))
        return false;
    if (to != from)
        from.deleteFile();

    if (getCurrentName() == sanitiseName (oldName))
        state.state.setProperty (presetNameId, newName, nullptr);
    return true;
}

bool PresetManager::deleteUser (const juce::String& name)
{
    const auto file = fileForUserPreset (name);
    if (! file.existsAsFile())
        return false;
    // To the recycle bin where possible, so a mistake can be undone there.
    if (! file.moveToTrash())
        return file.deleteFile();
    return true;
}

bool PresetManager::exportTo (const juce::File& file) const
{
    auto target = file.hasFileExtension (fileExtension) ? file : file.withFileExtension (fileExtension);
    return createPresetXml (getCurrentName())->writeTo (target);
}

juce::String PresetManager::importFrom (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (rootTag) || ! userFolder.createDirectory())
        return {};

    auto base = sanitiseName (xml->getStringAttribute ("name", file.getFileNameWithoutExtension()));
    if (base.isEmpty())
        base = sanitiseName (file.getFileNameWithoutExtension());
    if (base.isEmpty())
        base = "Imported";

    auto name = base;
    for (int n = 2; userPresetExists (name); ++n)
        name = base + " (" + juce::String (n) + ")";

    xml->setAttribute ("name", name);
    if (! xml->writeTo (fileForUserPreset (name)))
        return {};
    return loadUser (name) ? name : juce::String();
}

//==============================================================================
std::vector<float> PresetManager::snapshot() const
{
    std::vector<float> v;
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isSoundParameter (rp->paramID))
                v.push_back (rp->getValue());
    return v;
}

void PresetManager::markLoaded (const juce::String& name)
{
    state.state.setProperty (presetNameId, name, nullptr);
    loadedSnapshot = snapshot();
}

juce::String PresetManager::getCurrentName() const
{
    return state.state.getProperty (presetNameId, "Default").toString();
}

bool PresetManager::isModified() const
{
    const auto now = snapshot();
    if (now.size() != loadedSnapshot.size())
        return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (std::abs (now[i] - loadedSnapshot[i]) > 1.0e-6f)
            return true;
    return false;
}
