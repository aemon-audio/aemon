// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

struct Preset
{
    juce::String name, note;
    std::vector<std::pair<juce::String, float>> values; // real (denormalised) values; anything missing is default
    bool user = false;
    juce::File file;
    juce::String category;   // factory presets are grouped into these folders in the menu
    juce::String description;  // one line: what it does and when to use it
};

/** Everything a plugin keeps besides its parameters: undo history, A/B slots, presets and UI
    preferences (Simple/Pro, window size). Owned by the processor so it survives the editor closing. */
class PluginState
{
public:
    PluginState (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s, juce::String pluginName, std::vector<Preset> factory)
        : proc (p), apvts (s), name (std::move (pluginName)), factoryPresets (std::move (factory)), history (p)
    {
        ui.setProperty ("mode", "simple", nullptr);
        ui.setProperty ("scale", 1.0, nullptr);
        current = factoryPresets.empty() ? Preset { "Init" } : factoryPresets.front();
        slots[0] = slots[1] = ParamSnapshot::capture (p);
        rescanUserPresets();
    }

    juce::AudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    const juce::String name;
    std::vector<Preset> factoryPresets, userPresets;
    History history;
    juce::ValueTree ui { "ui" };
    Preset current;

    // ---- presets --------------------------------------------------------------
    juce::File userPresetFolder() const
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Aemon").getChildFile (name + " Presets");
    }

    void rescanUserPresets()
    {
        userPresets.clear();
        for (auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.aemonpreset"))
            if (auto xml = juce::XmlDocument::parse (f))
            {
                Preset p { xml->getStringAttribute ("name", f.getFileNameWithoutExtension()), "yours", {}, true, f };
                for (auto* e : xml->getChildWithTagNameIterator ("param"))
                    p.values.emplace_back (e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value"));
                userPresets.push_back (p);
            }
        std::sort (userPresets.begin(), userPresets.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
    }

    ParamSnapshot snapshotOf (const Preset& p) const
    {
        ParamSnapshot s;
        for (auto* prm : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                if (ParamSnapshot::isExcluded (r->getParameterID())) continue;
                float v = r->getDefaultValue();
                for (auto& [id, real] : p.values) if (id == r->getParameterID()) v = r->convertTo0to1 (real);
                s.values.emplace_back (r->getParameterID(), v);
            }
        return s;
    }

    void loadPreset (const Preset& p)
    {
        history.begin();
        snapshotOf (p).apply (apvts);
        current = p;
        history.commit();
    }

    bool isEdited() const { return ! (snapshotOf (current) == ParamSnapshot::capture (proc)); }

    bool saveUserPreset (const juce::String& presetName)
    {
        auto folder = userPresetFolder();
        if (! folder.createDirectory()) return false;
        juce::XmlElement xml ("AemonPreset");
        xml.setAttribute ("plugin", name);
        xml.setAttribute ("name", presetName);
        xml.setAttribute ("version", 1);
        for (auto* prm : proc.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! ParamSnapshot::isExcluded (r->getParameterID()))
                {
                    auto* e = xml.createNewChildElement ("param");
                    e->setAttribute ("id", r->getParameterID());
                    e->setAttribute ("value", r->convertFrom0to1 (r->getValue()));
                }
        auto file = folder.getChildFile (juce::File::createLegalFileName (presetName) + ".aemonpreset");
        if (! xml.writeTo (file)) return false;
        rescanUserPresets();
        for (auto& p : userPresets) if (p.file == file) current = p;
        return true;
    }

    std::vector<const Preset*> allPresets() const
    {
        std::vector<const Preset*> v;
        for (auto& p : factoryPresets) v.push_back (&p);
        for (auto& p : userPresets) v.push_back (&p);
        return v;
    }

    void stepPreset (int dir)
    {
        auto all = allPresets();
        if (all.empty()) return;
        int i = 0;
        for (int k = 0; k < (int) all.size(); ++k) if (all[(size_t) k]->name == current.name && all[(size_t) k]->user == current.user) i = k;
        i = (i + dir + (int) all.size()) % (int) all.size();
        loadPreset (*all[(size_t) i]);
    }

    // ---- A/B -------------------------------------------------------------------
    int abIndex = 0;
    ParamSnapshot slots[2];

    void switchAB (int to)
    {
        if (to == abIndex) return;
        slots[abIndex] = ParamSnapshot::capture (proc);
        abIndex = to;
        history.begin();
        slots[abIndex].apply (apvts);
        history.commit();
    }
    void copyAB() { slots[1 - abIndex] = ParamSnapshot::capture (proc); }

    // ---- persistence -------------------------------------------------------------
    void writeTo (juce::ValueTree& root)
    {
        slots[abIndex] = ParamSnapshot::capture (proc);
        root.removeChild (root.getChildWithName ("ui"), nullptr);
        root.removeChild (root.getChildWithName ("ab"), nullptr);
        auto u = ui.createCopy();
        u.setProperty ("preset", current.name, nullptr);
        u.setProperty ("presetUser", current.user, nullptr);
        root.appendChild (u, nullptr);
        juce::ValueTree ab ("ab");
        ab.setProperty ("current", abIndex, nullptr);
        ab.appendChild (slots[0].toTree ("A"), nullptr);
        ab.appendChild (slots[1].toTree ("B"), nullptr);
        root.appendChild (ab, nullptr);
    }

    void readFrom (const juce::ValueTree& root)
    {
        if (auto u = root.getChildWithName ("ui"); u.isValid())
        {
            ui.copyPropertiesFrom (u, nullptr);
            const auto pname = u.getProperty ("preset").toString();
            const bool isUser = (bool) u.getProperty ("presetUser", false);
            for (auto* p : allPresets()) if (p->name == pname && p->user == isUser) current = *p;
        }
        if (auto ab = root.getChildWithName ("ab"); ab.isValid())
        {
            abIndex = juce::jlimit (0, 1, (int) ab.getProperty ("current", 0));
            slots[0] = ParamSnapshot::fromTree (ab.getChildWithName ("A"), proc);
            slots[1] = ParamSnapshot::fromTree (ab.getChildWithName ("B"), proc);
        }
        slots[abIndex] = ParamSnapshot::capture (proc);
    }
};

} // namespace aemon
