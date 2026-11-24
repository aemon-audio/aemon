// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Small square button with a stroked icon (undo, redo, arrows). */
class IconButton : public juce::Button, public HelpProvider
{
public:
    IconButton (const juce::String& name, juce::Path p, juce::String helpText);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::String getHelpText() const override { return help; }
private:
    juce::Path path;
    juce::String help;
};

/** Round bypass switch. Toggle state true = bypassed; lit amber while the plugin is active. */
class PowerButton : public juce::Button, public HelpProvider
{
public:
    PowerButton();
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::String getHelpText() const override { return "Bypass: click to hear the input untouched. The switch is smoothed, so it never clicks."; }
};

/** The frame every Aemon plugin shares: header (mode, presets, undo, A/B, size, bypass),
    a body and a control deck for each mode, a help strip, toasts and resizing. */
class Shell : public juce::Component, private juce::Timer
{
public:
    static constexpr int headerH = 48, deckH = 160, statusH = 26;

    Shell (juce::AudioProcessorEditor& editor, PluginState& state, juce::String pluginTitle, int baseW, int baseH);
    ~Shell() override;

    /** Components for each mode. The shell owns layout, the editor owns the components. */
    void setModeComponents (juce::Component* simpleBody, juce::Component* simpleDeck, juce::Component* proBody, juce::Component* proDeck);
    bool isSimple() const { return state.ui.getProperty ("mode").toString() != "pro"; }
    std::function<void (bool simple)> onModeChanged;

    void toast (const juce::String& text);
    void toastPreset();
    void setDefaultHint (const juce::String& t) { defaultHint = t; statusText = t; repaint(); }
    void setMeta (const juce::String& t) { meta = t; repaint(); }
    void refreshHeader();
    void beginEdit() { state.history.begin(); }
    void endEdit() { state.history.commit(); refreshHeader(); }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    juce::Component& getContent() { return content; }
    void setHint (const juce::String& t) { if (t != statusText) { statusText = t; content.repaint (0, baseH - statusH, baseW, statusH); } }

private:
    struct Content : juce::Component
    {
        Shell& owner;
        explicit Content (Shell& s) : owner (s) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void paintOverChildren (juce::Graphics& g) override { owner.paintContentOver (g); }
    };
    void paintContent (juce::Graphics&);
    void paintContentOver (juce::Graphics&);
    void layoutContent();
    void timerCallback() override;
    void updateHintFrom (juce::Component* c);
    void showPresetMenu();
    void startSaveAs();
    void applyMode (bool animate);

    juce::AudioProcessorEditor& editor;
    PluginState& state;
    const juce::String title;
    const int baseW, baseH;
    Content content { *this };

    SegControl modeSeg { { "Simple", "Pro" }, { "Simple: a few big controls, automatic checks and one-click fixes.", "Pro: every control and the full metering." } };
    IconButton prevBtn, nextBtn, undoBtn, redoBtn;
    juce::TextButton presetBox;
    Button saveBtn { "Save" };
    juce::TextButton abA { "A" }, abCopy { "A>B" }, abB { "B" }, scaleBtn { "100%" };
    PowerButton power;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAtt;
    std::unique_ptr<juce::TextEditor> nameEditor;
    std::unique_ptr<juce::LookAndFeel> headerLnf;

    juce::Component *sBody = nullptr, *sDeck = nullptr, *pBody = nullptr, *pDeck = nullptr;
    juce::String statusText, defaultHint, meta, toastText;
    double toastUntil = 0, flipStart = 0;
    float toastAlpha = 0;
    bool bypassed = false;
};

} // namespace aemon
