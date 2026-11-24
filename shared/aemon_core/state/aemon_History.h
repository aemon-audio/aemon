// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Snapshot of every parameter's normalised value. Used for undo/redo, A/B and presets. */
struct ParamSnapshot
{
    std::vector<std::pair<juce::String, float>> values;

    static ParamSnapshot capture (juce::AudioProcessor& p)
    {
        ParamSnapshot s;
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! isExcluded (r->getParameterID()))
                    s.values.emplace_back (r->getParameterID(), r->getValue());
        return s;
    }

    /** Sets parameters one by one with gestures, so hosts record it and smoothing avoids clicks. */
    void apply (juce::AudioProcessorValueTreeState& apvts) const
    {
        for (auto& [id, v] : values)
            if (auto* r = apvts.getParameter (id))
                if (std::abs (r->getValue() - v) > 1.0e-7f)
                {
                    r->beginChangeGesture();
                    r->setValueNotifyingHost (v);
                    r->endChangeGesture();
                }
    }

    bool operator== (const ParamSnapshot& o) const
    {
        if (values.size() != o.values.size()) return false;
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i].first != o.values[i].first || std::abs (values[i].second - o.values[i].second) > 1.0e-6f) return false;
        return true;
    }

    juce::ValueTree toTree (const juce::Identifier& type) const
    {
        juce::ValueTree t (type);
        for (auto& [id, v] : values) t.setProperty (id, v, nullptr);
        return t;
    }

    static ParamSnapshot fromTree (const juce::ValueTree& t, juce::AudioProcessor& p)
    {
        auto s = capture (p);
        for (auto& [id, v] : s.values)
            if (t.hasProperty (id)) v = (float) t.getProperty (id);
        return s;
    }

    // Bypass is a host-level switch, not part of a sound setting.
    static bool isExcluded (const juce::String& id) { return id == "bypass"; }
};

/** Undo/redo of whole settings. A gesture begins (snapshot taken) and commits when it ends. */
class History
{
public:
    explicit History (juce::AudioProcessor& p) : proc (p) {}

    void begin() { if (! pending) pending = ParamSnapshot::capture (proc); }
    void commit()
    {
        if (! pending) return;
        if (! (*pending == ParamSnapshot::capture (proc)))
        {
            undoStack.push_back (*pending);
            if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
            redoStack.clear();
        }
        pending.reset();
        if (onChange) onChange();
    }
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo (juce::AudioProcessorValueTreeState& s) { step (undoStack, redoStack, s); }
    void redo (juce::AudioProcessorValueTreeState& s) { step (redoStack, undoStack, s); }

    std::function<void()> onChange;

private:
    void step (std::vector<ParamSnapshot>& from, std::vector<ParamSnapshot>& to, juce::AudioProcessorValueTreeState& s)
    {
        if (from.empty()) return;
        to.push_back (ParamSnapshot::capture (proc));
        auto snap = from.back();
        from.pop_back();
        snap.apply (s);
        if (onChange) onChange();
    }

    juce::AudioProcessor& proc;
    std::optional<ParamSnapshot> pending;
    std::vector<ParamSnapshot> undoStack, redoStack;
};

} // namespace aemon
