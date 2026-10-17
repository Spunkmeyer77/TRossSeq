#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Same hardware look as TRoss 606: brushed silver top, dark sequencer section, LED pads
class SeqLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SeqLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
    juce::Font getPopupMenuFont() override;
};

// Lower section: step numbers, play LEDs and the 9 x 16 pad grid. Click to toggle, drag to paint.
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (TrossSeqProcessor& p) : proc (p) { setOpaque (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragRow = -1; }

    static juce::Rectangle<float> padRect (int row, int step);
    void showStepMenu (int row, int step);
    void showAccentMenu();

private:
    TrossSeqProcessor& proc;
    int dragRow = -1, lastDragStep = -1;
    bool dragValue = false;
    bool hit (juce::Point<float>, int& row, int& step) const;
};

// A track's black name strip, played like a pad: click = note, Shift-click = accented note,
// right-click = rename / learn. Lights up whenever the track sends a note.
class NameStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    NameStrip (TrossSeqProcessor& p, int t);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override { editor.setBounds (getLocalBounds()); }
    void startRename();
    juce::uint32 clickTime = 0;   // last click: the strip flashes only for clicks
private:
    TrossSeqProcessor& proc;
    int track;
    bool lastAccent = false;
    juce::TextEditor editor;
    void finishRename (bool keep);
};

// A track's M button: click toggles the mute. Shift + mouse held: only this track plays (a momentary
// solo, beating every mute) until the mouse button is released. Nothing is toggled.
class MuteButton : public juce::TextButton
{
public:
    MuteButton (TrossSeqProcessor& p, int t) : juce::TextButton ("M"), proc (p), track (t) {}
    ~MuteButton() override { release(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isShiftDown() && e.mods.isLeftButtonDown())
        {
            holding = true;
            proc.momentarySolo = track;
            getProperties().set ("soloHeld", true);
            setButtonText ("S");
            repaint();
            return;
        }
        juce::TextButton::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! holding) juce::TextButton::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (holding) { release(); return; }
        juce::TextButton::mouseUp (e);
    }
private:
    TrossSeqProcessor& proc;
    int track;
    bool holding = false;
    void release()
    {
        if (! holding) return;
        holding = false;
        int expected = track;
        proc.momentarySolo.compare_exchange_strong (expected, -1);
        getProperties().remove ("soloHeld");
        setButtonText ("M");
        repaint();
    }
};

// EXPORT MIDI key: click = menu (this pattern / all in a row / all as separate files), drag = drop the
// playing pattern onto a DAW track as a MIDI clip
class ExportKey : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ExportKey (TrossSeqProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { pressed = true; dragged = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    TrossSeqProcessor& proc;
    bool pressed = false, dragged = false;
    std::unique_ptr<juce::FileChooser> chooser;
    void showMenu();
    void save (std::vector<int> slots, const juce::String& title);
    void exportAllSeparate();
    std::vector<int> nonEmptySlots() const;
};

// 12 pattern buttons, 2 across x 6 down. LED = slot playing (blinks when queued), light cap = selected.
// Right-click a button: copy it to another slot, or clear it.
class PatternPad : public juce::Component
{
public:
    explicit PatternPad (TrossSeqProcessor& p) : proc (p) { setOpaque (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    static juce::Rectangle<float> buttonRect (int slot);
private:
    TrossSeqProcessor& proc;
};

// Amber read-out: playing pattern, and the queued one
class StatusLcd : public juce::Component
{
public:
    explicit StatusLcd (TrossSeqProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    TrossSeqProcessor& proc;
};

// MIDI port selector: refreshes the port list each time it opens
class PortBox : public juce::ComboBox
{
public:
    explicit PortBox (TrossSeqProcessor& p);
    void showPopup() override { refresh(); juce::ComboBox::showPopup(); }
    void refresh();
private:
    TrossSeqProcessor& proc;
    juce::Array<juce::MidiDeviceInfo> devices;
};

// The About window: opens over the panel when you click the TRoss SEQ logo.
// Click anywhere (or press Esc) to close it.
class AboutOverlay : public juce::Component
{
public:
    explicit AboutOverlay (TrossSeqProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    // the manual built into the plug-in, written to a temporary file; returns it (or an empty File)
    static juce::File writeManual();
    juce::TextButton manualBtn { "OPEN THE MANUAL" };
    static constexpr const char* kContact = "ticcoross@gmail.com";
    juce::HyperlinkButton contactLink { kContact, juce::URL (juce::String ("mailto:") + kContact) };
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible (false); return true; } return false; }
    juce::Rectangle<float> card() const;
    static juce::String formatAndHost (const TrossSeqProcessor&);
private:
    TrossSeqProcessor& proc;
};

// Invisible click area over the logo
class LogoButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    LogoButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("About TRoss Seq"); }
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }
    std::function<void()> onClick;
};

class TrossSeqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TrossSeqEditor (TrossSeqProcessor&);
    ~TrossSeqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1120, kHeight = 461;

private:
    // everything is laid out at kWidth x kHeight here; the editor scales it
    struct Content : public juce::Component
    {
        explicit Content (TrossSeqEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics& g) override { ed.paintPanel (g); }
        TrossSeqEditor& ed;
    };

    TrossSeqProcessor& proc;
    SeqLookAndFeel lnf;
    Content content { *this };
    StepGrid grid { proc };
    PatternPad patternPad { proc };
    StatusLcd lcd { proc };
    PortBox portBox { proc };
    std::vector<std::unique_ptr<NameStrip>> strips;
    ExportKey exportKey { proc };

    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Button>> buttons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> bAtts;
    juce::Slider* noteBox[trossseq::kNumTracks] {};
    juce::Slider* chanBox[trossseq::kNumTracks] {};
    juce::TextButton* learnBtn[trossseq::kNumTracks] {};
    juce::TextButton clearBtn { "CLEAR" }, mapBtn { "PRESETS" };
    juce::TextButton randomBtn { "RANDOM" }, fillBtn { "FILL" }, undoBtn { "UNDO" };
    juce::ComboBox styleBox;
    // STUTTER: held = on (momentary), plus its slice length and ROLL
public:
    juce::TextButton stutterBtn { "STUTTER" };
    juce::ComboBox stutRateBox;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stutRateAtt;
    bool stutterHeld = false, lastStutter = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt;
    bool lastPendingRnd = false;
    int lastLockVersion = -1;
    // standalone app only
    juce::TextButton playBtn { "PLAY" };
    juce::Slider bpmBox;
    juce::TextButton helpBtn { "HELP" };
    LogoButton logoBtn;
    AboutOverlay about { proc };
public:
    void showAbout();
    bool isAboutShown() const { return about.isVisible(); }
private:
    std::unique_ptr<juce::TooltipWindow> tooltips;
    void updateHelp();

    int lastVersion = -1, lastStep = -2, lastSlot = -1, lastPending = -2, lastLen = -1, lastSel = -1,
        lastMapVersion = -1, lastNameVersion = -1, lastLearn = -2, lastSolo = -2;
    juce::String lastPortText;

    juce::Slider& addKnob (const juce::String& paramId, juce::Point<int> centre, int diameter);
    juce::Button& addButton (const juce::String& paramId, const juce::String& text, juce::Rectangle<int> r, const char* style);
    juce::Slider& addReadout (juce::Rectangle<int> r, double lo, double hi, std::function<juce::String (double)> text);
    void showMapMenu();
    void saveMapAs();
    void renameMap (const juce::String& from);
    void deleteMap (const juce::String& name);
    void askName (const juce::String& title, const juce::String& initial, const juce::String& okText, std::function<void (juce::String)> done);
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText, std::function<void()> yes);
    juce::String lastMapLabel;
    int lastChain = -1;
    void paintPanel (juce::Graphics&);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrossSeqEditor)
};
