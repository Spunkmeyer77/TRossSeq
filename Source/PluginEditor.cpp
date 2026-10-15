#include "PluginEditor.h"
#include "MidiExport.h"

using namespace trossseq;

namespace
{
namespace col
{
const juce::Colour silverTop   { 0xffd9d9d4 }, silverBottom { 0xffb9b9b3 };
const juce::Colour ink         { 0xff1d1d1d };
const juce::Colour printText   { 0xff2d2d2d };
const juce::Colour softText    { 0xff555555 };
const juce::Colour orange      { 0xffe0531f };
const juce::Colour stripBg     { 0xff2b2b2b };
const juce::Colour stripText   { 0xfff0efe8 };
const juce::Colour knobRing    { 0xffa9a9a3 };
const juce::Colour darkTop     { 0xff2a2a2a }, darkBottom { 0xff1d1d1d };
const juce::Colour darkLabel   { 0xff9a9a92 };
const juce::Colour numText     { 0xffb9b9b0 };
const juce::Colour rowText     { 0xffe9e8e0 };
const juce::Colour accText     { 0xffff8a55 };
const juce::Colour ledOff      { 0xff4a1a10 };
const juce::Colour ledOn       { 0xffff5a1f };
const juce::Colour cream1      { 0xffe7e5dc }, cream2 { 0xffc9c7bd };
const juce::Colour grey1       { 0xffa9a7a0 }, grey2  { 0xff8f8d86 };
const juce::Colour orange1     { 0xffe0531f }, orange2 { 0xffc2410f };
const juce::Colour yellow1     { 0xfff0b23a }, yellow2 { 0xffd6951a };
const juce::Colour muteOn      { 0xffffcf4a }, soloOn { 0xff4f9ef0 };
const juce::Colour lcdBg       { 0xff1a1712 }, lcdText { 0xffff6a2b };
const juce::Colour okGreen     { 0xff3f8f3a };
}

juce::Font font (float size, bool bold = false, float kerning = 0.0f)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}

juce::Font mono (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)); }

void drawLed (juce::Graphics& g, juce::Point<float> c, float d, bool lit, juce::Colour on = col::ledOn)
{
    if (lit)
    {
        g.setGradientFill (juce::ColourGradient (on.withAlpha (0.45f), c, on.withAlpha (0.0f), c.translated (d * 1.0f, 0), true));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3c4), c, on.darker (0.2f), c.translated (d * 0.5f, 0), true));
    }
    else
        g.setColour (col::ledOff);
    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, float radius, float shadow)
{
    g.setColour (juce::Colour (0xff0b0b0b));
    g.fillRoundedRectangle (r.translated (0, shadow), radius);
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
}

// Ableton note names: C1 = 36
juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
}

// ---- layout (kWidth x kHeight)
constexpr int kMargin = 18, kTopH = 124, kSeqY = 127;
constexpr int kMasterX = 984, kMasterW = 118;
constexpr int kRndY = kSeqY + 212;   // randomiser block, under the pattern buttons

// lower section, in StepGrid coordinates (the grid sits at 0, kSeqY)
constexpr float kGridX = 104.0f, kGridR = 690.0f, kColGap = 5.0f, kPadTop = 46.0f, kRowPitch = 20.0f, kPadH = 16.0f;
constexpr float kColW = (kGridR - kGridX - 15 * kColGap) / 16.0f;
constexpr int kNameX = 18, kNameW = 78;
constexpr int kNoteX = 700, kNoteW = 54, kChanX = 760, kChanW = 32, kMuteX = 798, kMuteW = 24, kLearnX = 828, kLearnW = 44;
int rowY (int row) { return kSeqY + (int) kPadTop + row * (int) kRowPitch; }
} // namespace

// ================================================================================================
SeqLookAndFeel::SeqLookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, col::lcdBg);
    setColour (juce::BubbleComponent::outlineColourId, juce::Colours::black);
    setColour (juce::TooltipWindow::textColourId, col::lcdText);
    setColour (juce::ComboBox::textColourId, col::lcdText);
    setColour (juce::ComboBox::arrowColourId, col::lcdText);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff222222));
    setColour (juce::PopupMenu::textColourId, col::rowText);
    setColour (juce::PopupMenu::headerTextColourId, col::darkLabel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::orange);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, col::stripText);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::orange.withAlpha (0.4f));
    setColour (juce::TextEditor::outlineColourId, col::orange);
    setColour (juce::TextEditor::focusedOutlineColourId, col::orange);
    setColour (juce::CaretComponent::caretColourId, col::ink);
    setColour (juce::AlertWindow::backgroundColourId, col::darkTop);
    setColour (juce::AlertWindow::textColourId, col::rowText);
    setColour (juce::AlertWindow::outlineColourId, col::orange);
}

void SeqLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                       juce::Slider& s)
{
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();

    if (s.getProperties().contains ("seg"))   // red LED read-out (LENGTH, NOTE, CH, BPM)
    {
        g.setColour (juce::Colour (0xff120f0c));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (s.isMouseOverOrDragging() ? col::ledOn.withAlpha (0.6f) : juce::Colours::black);
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
        const auto txt = s.getTextFromValue (s.getValue());
        const float size = (float) s.getProperties().getWithDefault ("segSize", 20.0f);
        g.setFont (mono (size));
        g.setColour (col::ledOn.withAlpha (0.35f));
        g.drawText (txt, area.translated (0, 0.5f).expanded (0.6f), juce::Justification::centred);
        g.setColour (col::ledOn);
        g.drawText (txt, area, juce::Justification::centred);
        return;
    }

    // black knurled knob with a light ring and a white pointer
    const float d = juce::jmin (area.getWidth(), area.getHeight()) - 6.0f;
    const auto c = area.getCentre();
    auto body = juce::Rectangle<float> (d, d).withCentre (c);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (body.expanded (3.0f).translated (0, 2.0f));
    g.setColour (col::knobRing);
    g.fillEllipse (body.expanded (3.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4a), c.x - d * 0.12f, c.y - d * 0.18f,
                                             juce::Colour (0xff0b0b0b), c.x + d * 0.5f, c.y + d * 0.5f, true));
    g.fillEllipse (body);
    g.setColour (juce::Colour (0xff262626));
    g.drawEllipse (body.reduced (1.0f), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (int i = 0; i < 24; ++i)
    {
        const float a = (float) i * juce::MathConstants<float>::twoPi / 24.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (d * 0.5f - 2.5f, a), c.getPointOnCircumference (d * 0.5f - 0.5f, a)), 1.0f);
    }

    const float angle = start + pos * (end - start);
    juce::Path p;
    p.startNewSubPath (c.getPointOnCircumference (d * 0.5f - 2.5f, angle));
    p.lineTo (c.getPointOnCircumference (d * 0.5f - d * 0.36f, angle));
    g.setColour (juce::Colour (0xfff2f2ee));
    g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SeqLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);

    if (style == "rubber" || style == "rubberDark")   // small buttons
    {
        r = r.withTrimmedBottom (1.0f);
        const bool soloHeld = b.getProperties().contains ("soloHeld");   // Shift-held M button
        const auto onColour = soloHeld ? col::soloOn : juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["onColour"]);
        const bool dark = style == "rubberDark";
        const auto offTop = dark ? juce::Colour (0xff5a5a56) : juce::Colour (0xffefefea);
        const auto offBot = dark ? juce::Colour (0xff42423f) : juce::Colour (0xffcfcfc9);
        const auto top = on || soloHeld ? onColour.brighter (0.15f) : offTop;
        const auto bot = on || soloHeld ? onColour.darker (0.1f) : offBot;
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0, 1.0f), 3.0f);
        g.setGradientFill (juce::ColourGradient (top.brighter (highlighted ? 0.05f : 0.0f), 0, r.getY(), bot, 0, r.getBottom(), false));
        g.fillRoundedRectangle (r.translated (0, down ? 1.0f : 0.0f), 3.0f);
        if (b.getProperties().contains ("led"))
            drawLed (g, { r.getX() + 8.0f, r.getCentreY() }, 5.0f, on, juce::Colour (0xff49d04a));
        return;
    }

    // hardware keys on the dark section (RUN, CLEAR, REC, PLAY, DRUM MAP)
    r = r.withTrimmedBottom (2.0f).translated (0, down ? 1.5f : 0.0f);
    const bool rec = style == "rec";
    if (rec && on) drawKey (g, r, col::orange1, col::orange2, 4.0f, down ? 0.5f : 2.0f);
    else           drawKey (g, r, highlighted ? col::cream1.brighter (0.05f) : col::cream1, col::cream2, 4.0f, down ? 0.5f : 2.0f);

    if (b.getProperties().contains ("led"))
        drawLed (g, { r.getX() + 12.0f, r.getCentreY() }, 6.0f, on, rec ? juce::Colours::white : col::ledOn);
}

void SeqLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto style = b.getProperties()["style"].toString();
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds();
    if (style == "rubber" || style == "rubberDark")
    {
        const bool dark = style == "rubberDark";
        if (b.getProperties().contains ("soloHeld")) g.setColour (juce::Colours::white);
        else g.setColour (on ? (dark ? juce::Colour (0xff1d1d1d) : juce::Colour (0xff222222)) : (dark ? col::rowText : juce::Colour (0xff333333)));
        g.setFont (font (8.5f, true, 0.04f));
        if (b.getProperties().contains ("led")) r = r.withTrimmedLeft (12);
        g.drawText (b.getButtonText(), r.withTrimmedBottom (1).translated (0, down ? 1 : 0), juce::Justification::centred);
        return;
    }
    r = r.withTrimmedBottom (2).translated (0, down ? 1 : 0);
    if (b.getProperties().contains ("led")) r = r.withTrimmedLeft (14);
    g.setColour (style == "rec" && on ? juce::Colours::white : juce::Colour (0xff222222));
    g.setFont (font (9.0f, true, 0.08f));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void SeqLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour (col::lcdBg);
    g.fillRoundedRectangle (body, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (body.reduced (0.5f), 3.0f, 1.0f);
}

void SeqLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h);
    g.setColour (col::lcdBg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.isMouseOver (true) ? col::ledOn.withAlpha (0.6f) : juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (col::lcdText);
    g.fillPath (arrow);
}

juce::Font SeqLookAndFeel::getComboBoxFont (juce::ComboBox&) { return mono (11.5f); }

void SeqLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font SeqLookAndFeel::getSliderPopupFont (juce::Slider&) { return mono (12.0f); }
juce::Font SeqLookAndFeel::getPopupMenuFont() { return font (13.0f); }

// ================================================================================================
juce::Rectangle<float> StepGrid::padRect (int row, int step)
{
    return { kGridX + (float) step * (kColW + kColGap), kPadTop + (float) row * kRowPitch, kColW, kPadH };
}

void StepGrid::paint (juce::Graphics& g)
{
    static const juce::Colour groupTop[4] = { col::cream1, col::grey1, col::orange1, col::yellow1 };
    static const juce::Colour groupBot[4] = { col::cream2, col::grey2, col::orange2, col::yellow2 };

    const auto h = (float) getHeight();
    g.setGradientFill (juce::ColourGradient (col::darkTop, 0, 0, col::darkBottom, 0, h, false));
    g.fillAll();

    const int slot = proc.getCurrentSlot();
    const int playing = proc.getPlayStep();
    const int len = (int) proc.apvts.getRawParameterValue ("seqlen")->load();

    g.setColour (col::darkLabel);
    g.setFont (font (9.0f, false, 0.2f));
    g.drawText ("STEP", kMargin, 12, 46, 12, juce::Justification::centredLeft);
    g.drawText ("NOTE", kNoteX, 12, kNoteW, 12, juce::Justification::centred);
    g.drawText ("CH", kChanX, 12, kChanW, 12, juce::Justification::centred);
    g.drawText ("MUTE", kMuteX - 4, 12, kMuteW + 8, 12, juce::Justification::centred);
    g.drawText ("LEARN", kLearnX, 12, kLearnW, 12, juce::Justification::centred);

    for (int s = 0; s < kNumSteps; ++s)
    {
        const auto x = kGridX + (float) s * (kColW + kColGap);
        g.setColour (s % 4 == 0 ? col::orange : col::numText);
        g.setFont (font (9.5f, s % 4 == 0));
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (x, 12.0f, kColW, 12.0f), juce::Justification::centred);
        drawLed (g, { x + kColW * 0.5f, 34.0f }, 7.0f, s == playing);
    }

    // accent row label (the drum rows have their clickable name strips on top)
    g.setColour (col::accText);
    g.setFont (font (10.0f, true, 0.1f));
    g.drawText ("ACCENT", juce::Rectangle<float> ((float) kNameX, kPadTop, (float) kNameW, kPadH), juce::Justification::centred);
    g.setColour (col::darkLabel);
    g.setFont (font (8.5f, false, 0.1f));
    g.drawText ("PLAYS AT THE ACCENT VELOCITY", juce::Rectangle<float> ((float) kNoteX, kPadTop, (float) (kLearnX + kLearnW - kNoteX), kPadH),
                juce::Justification::centred);

    for (int row = 0; row < kNumRows; ++row)
    {
        for (int s = 0; s < kNumSteps; ++s)
        {
            const auto r = padRect (row, s);
            const bool on = proc.patterns.cell (slot, row, s);
            const int grp = s / 4;

            g.setColour (juce::Colour (0xff111111));
            g.fillRoundedRectangle (r.translated (0, 1.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (groupTop[grp], 0, r.getY(), groupBot[grp], 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 3.0f);

            const auto c = r.getCentre();
            if (on && proc.patterns.flam (slot, row, s))   // flam: a column of 1-3 tiny LEDs on the pad's left edge, main LED moves right
            {
                const auto fs = proc.patterns.flamStyle (slot, row, s);
                const auto cm = juce::Point<float> (r.getX() + r.getWidth() * 0.58f, c.y);
                g.setColour (juce::Colour (0xff1a1a1a));
                g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (cm));
                drawLed (g, cm, 8.0f, true);
                static const float ys[4][3] = { {}, { 0.5f }, { 0.3f, 0.7f }, { 0.2f, 0.5f, 0.8f } };
                const float gx = r.getX() + 5.5f;
                for (int k = 0; k < fs.graces; ++k)
                {
                    const juce::Point<float> gc { gx, r.getY() + r.getHeight() * ys[fs.graces][k] };
                    g.setColour (juce::Colour (0xff1a1a1a));                     // dark socket, so it shows on every pad colour
                    g.fillEllipse (juce::Rectangle<float> (fs.graces == 3 ? 4.6f : 5.4f, fs.graces == 3 ? 4.6f : 5.4f).withCentre (gc));
                    drawLed (g, gc, fs.graces == 3 ? 3.0f : 3.6f, true);
                }
                if (fs.spacing != 0 || fs.level != 0)   // its own spacing / level: a small mark under the main LED
                {
                    g.setColour (juce::Colour (0xff1a1a1a).withAlpha (0.55f));
                    g.fillRect (cm.x - 5.0f, r.getBottom() - 2.5f, 10.0f, 1.2f);
                }
            }
            else if (on)
            {
                g.setColour (juce::Colour (0xff1a1a1a));
                g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
                drawLed (g, c, 8.0f, true);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.2f));
                g.fillEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (c));
            }

            if (s == playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.75f));
                g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 1.0f);
            }
            if (s >= len)   // past LENGTH: dimmed
            {
                g.setColour (col::darkBottom.withAlpha (0.65f));
                g.fillRoundedRectangle (r.expanded (0.5f, 1.5f), 3.0f);
            }
        }
    }
}

bool StepGrid::hit (juce::Point<float> p, int& row, int& step) const
{
    for (row = 0; row < kNumRows; ++row)
        for (step = 0; step < kNumSteps; ++step)
            if (padRect (row, step).expanded (kColGap * 0.5f, 2.5f).contains (p)) return true;
    return false;
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int row, step;
    const int slot = proc.getCurrentSlot();
    if (e.mods.isPopupMenu())
    {
        if (hit (e.position, row, step)) { showStepMenu (row, step); return; }
        if (juce::Rectangle<float> ((float) kNameX, kPadTop, (float) kNameW, kPadH).contains (e.position)) showAccentMenu();   // ACCENT label
        return;
    }
    if (! hit (e.position, row, step)) return;
    if (e.mods.isShiftDown() && row > 0)   // Shift-click: flam on (switching the step on if needed) / flam off
    {
        if (! proc.patterns.cell (slot, row, step))
        {
            proc.patterns.setCell (slot, row, step, true);
            proc.patterns.setFlam (slot, row, step, true);
        }
        else
            proc.patterns.setFlam (slot, row, step, ! proc.patterns.flam (slot, row, step));
        dragRow = -1;
        repaint();
        return;
    }
    dragValue = ! proc.patterns.cell (slot, row, step);
    dragRow = row; lastDragStep = step;
    proc.patterns.setCell (slot, row, step, dragValue);
    repaint();
}

// right-click on a pad: the step and its flam
void StepGrid::showStepMenu (int row, int step)
{
    if (row == 0) { showAccentMenu(); return; }
    auto& pm = proc.patterns;
    const int slot = proc.getCurrentSlot();
    const bool on = pm.cell (slot, row, step), fl = on && pm.flam (slot, row, step);
    const auto fs = pm.flamStyle (slot, row, step);
    const float knobMs = proc.apvts.getRawParameterValue ("flamtime")->load();
    const float knobLevel = proc.apvts.getRawParameterValue ("flamlevel")->load();

    juce::PopupMenu m;
    m.addSectionHeader (proc.getTrackName (row - 1) + "  ·  step " + juce::String (step + 1));
    m.addItem (1, on ? "Step off" : "Step on");
    m.addSeparator();
    m.addSectionHeader ("Flam  (Shift-click a pad: on / off)");
    m.addItem (10, "No flam", true, ! fl);
    m.addItem (11, "Flam  (1 grace note)", true, fl && fs.graces == 1);
    m.addItem (12, "Drag  (2 grace notes)", true, fl && fs.graces == 2);
    m.addItem (13, "Ruff  (3 grace notes)", true, fl && fs.graces == 3);
    juce::PopupMenu sp, lv;
    sp.addItem (20, "FLAM knob  (" + juce::String (knobMs, 0) + " ms)", true, fl && fs.spacing == 0);
    for (int i = 1; i < FlamStyle::kNumSpacings; ++i)
        sp.addItem (20 + i, juce::String (FlamStyle::kSpacingMs[i], 0) + " ms", true, fl && fs.spacing == i);
    lv.addItem (30, "FLAM LEVEL knob  (" + juce::String (knobLevel, 0) + " %)", true, fl && fs.level == 0);
    for (int i = 1; i < FlamStyle::kNumLevels; ++i)
        lv.addItem (30 + i, juce::String (FlamStyle::kLevelPct[i], 0) + " %", true, fl && fs.level == i);
    m.addSubMenu ("Spacing", sp);
    m.addSubMenu ("Grace level", lv);
    m.addSeparator();
    m.addItem (40, "Use this flam on every hit in this lane", fl);
    m.addItem (41, "Remove all flams in this lane", pm.getFlams (slot, row) != 0);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                         localAreaToGlobal (padRect (row, step).toNearestInt())),
                     [safe = juce::Component::SafePointer<StepGrid> (this), row, step, slot] (int id)
                     {
                         if (safe == nullptr || id <= 0) return;
                         auto& p = safe->proc;
                         auto& pm = p.patterns;
                         auto style = pm.flamStyle (slot, row, step);
                         auto ensureFlam = [&]
                         {
                             if (! pm.cell (slot, row, step)) pm.setCell (slot, row, step, true);
                             if (! pm.flam (slot, row, step)) { pm.setFlam (slot, row, step, true); style = FlamStyle(); }
                         };
                         if (id == 1)  pm.setCell (slot, row, step, ! pm.cell (slot, row, step));
                         if (id == 10) pm.setFlam (slot, row, step, false);
                         if (id >= 11 && id <= 13) { ensureFlam(); style.graces = id - 10; pm.setFlamStyle (slot, row, step, style.encode()); }
                         if (id >= 20 && id < 20 + FlamStyle::kNumSpacings) { ensureFlam(); style.spacing = id - 20; pm.setFlamStyle (slot, row, step, style.encode()); }
                         if (id >= 30 && id < 30 + FlamStyle::kNumLevels) { ensureFlam(); style.level = id - 30; pm.setFlamStyle (slot, row, step, style.encode()); }
                         if (id == 40) p.flamWholeRow (row, pm.getFlamStyle (slot, row, step));
                         if (id == 41) p.clearRowFlams (row, false);
                         safe->repaint();
                     });
}

// right-click on the ACCENT label (or an accent pad)
void StepGrid::showAccentMenu()
{
    const int slot = proc.getCurrentSlot();
    bool anyAll = false;
    for (int sl = 0; sl < kNumSlots; ++sl) anyAll |= proc.patterns.get (sl, 0) != 0;
    juce::PopupMenu m;
    m.addSectionHeader ("ACCENT row");
    m.addItem (1, "Clear the accent row", proc.patterns.get (slot, 0) != 0);
    m.addItem (2, "Clear the accent row in all 12 patterns", anyAll);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<StepGrid> (this)] (int id)
                     {
                         if (safe == nullptr) return;
                         if (id == 1) safe->proc.clearRow (0, false);
                         if (id == 2) safe->proc.clearRow (0, true);
                         safe->repaint();
                     });
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0) return;
    int row, step;
    if (! hit (e.position, row, step) || row != dragRow || step == lastDragStep) return;
    lastDragStep = step;
    proc.patterns.setCell (proc.getCurrentSlot(), row, step, dragValue);
    repaint();
}

// ================================================================================================
NameStrip::NameStrip (TrossSeqProcessor& p, int t) : proc (p), track (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setFont (font (10.0f, true));
    editor.setInputRestrictions (10);
    editor.onReturnKey = [this] { finishRename (true); };
    editor.onEscapeKey = [this] { finishRename (false); };
    editor.onFocusLost = [this] { finishRename (true); };
}

void NameStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // lights up only when you click it (not when the lane plays)
    const auto now = juce::Time::getMillisecondCounter();
    const float t = juce::jlimit (0.0f, 1.0f, (float) (now - clickTime) / 160.0f);
    const auto flash = lastAccent ? juce::Colour (0xffffffff) : col::orange;
    const auto bg = clickTime != 0 && t < 1.0f ? flash.interpolatedWith (col::stripBg, t) : col::stripBg;
    g.setColour (bg);
    g.fillRoundedRectangle (r, 2.0f);
    const int solo = proc.momentarySolo.load();
    const bool silenced = (solo >= 0 && solo != track)   // another track is held solo
                          || proc.getTrackName (track) == "---";   // spare lane in this map
    g.setColour ((bg.getPerceivedBrightness() > 0.5f ? col::stripBg : col::stripText).withAlpha (silenced ? 0.3f : 1.0f));
    g.setFont (font (10.0f, true, 0.1f));
    g.drawText (proc.getTrackName (track), getLocalBounds(), juce::Justification::centred);
    if (proc.locked[track].load())   // padlock: the randomiser leaves this lane alone
    {
        const float x = r.getX() + 5.0f, y = r.getCentreY() - 1.0f;
        g.setColour (col::accText);
        juce::Path shackle;
        shackle.addCentredArc (x + 3.0f, y, 2.2f, 2.6f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.2f));
        g.fillRoundedRectangle (x, y, 6.0f, 4.5f, 0.8f);
    }
    if (solo == track)
    {
        g.setColour (col::soloOn);
        g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
    }
}

void NameStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Track " + juce::String (track + 1) + ": note " + juce::String (proc.map.note (track))
                            + " (" + noteName (proc.map.note (track)) + "), channel " + juce::String (proc.map.channel (track)));
        const int row = track + 1, slot = proc.getCurrentSlot();
        const bool hasSteps = proc.patterns.get (slot, row) != 0, hasFlams = proc.patterns.getFlams (slot, row) != 0;
        bool anyAll = false, anyFlamsAll = false;
        for (int sl = 0; sl < kNumSlots; ++sl) { anyAll |= proc.patterns.get (sl, row) != 0; anyFlamsAll |= proc.patterns.getFlams (sl, row) != 0; }
        m.addItem (1, "Rename...  (Shift-click)");
        m.addItem (2, "Learn note from MIDI input");
        m.addSeparator();
        m.addItem (10, "Clear this lane", hasSteps);
        m.addItem (11, "Clear this lane's flams", hasFlams);
        m.addItem (12, "Clear this lane in all 12 patterns", anyAll);
        m.addItem (13, "Clear this lane's flams in all 12 patterns", anyFlamsAll);
        m.addSeparator();
        m.addItem (3, "Randomise this lane");
        m.addItem (4, "Lock from the randomiser  (Alt-click)", true, proc.locked[track].load());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<NameStrip> (this)] (int id)
                         {
                             if (safe == nullptr) return;
                             if (id == 1) safe->startRename();
                             if (id == 2) safe->proc.learnTrack = safe->track;
                             if (id == 3) safe->proc.randomise (TrossSeqProcessor::RandomOp::Lane, safe->track);
                             if (id == 4) safe->proc.toggleLock (safe->track);
                             if (id == 10) safe->proc.clearRow (safe->track + 1, false);
                             if (id == 11) safe->proc.clearRowFlams (safe->track + 1, false);
                             if (id == 12) safe->proc.clearRow (safe->track + 1, true);
                             if (id == 13) safe->proc.clearRowFlams (safe->track + 1, true);
                             safe->repaint();
                         });
        return;
    }
    if (e.mods.isAltDown())   { proc.toggleLock (track); repaint(); return; }
    if (e.mods.isShiftDown()) { startRename(); return; }
    lastAccent = e.mods.isCommandDown();   // Cmd-click (Mac) / Ctrl-click (Windows): accented hit
    proc.panelHit (track, lastAccent);
    clickTime = juce::Time::getMillisecondCounter();
    repaint();
}

void NameStrip::startRename()
{
    editor.setText (proc.getTrackName (track), false);
    editor.setVisible (true);
    editor.grabKeyboardFocus();
    editor.selectAll();
}

void NameStrip::finishRename (bool keep)
{
    if (! editor.isVisible()) return;
    if (keep && editor.getText().trim().isNotEmpty()) proc.setTrackName (track, editor.getText().trim());
    editor.setVisible (false);
    repaint();
}

// ================================================================================================
void ExportKey::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f).withTrimmedBottom (2.0f).translated (0, pressed ? 1.5f : 0.0f);
    drawKey (g, r, isMouseOver() ? col::cream1.brighter (0.05f) : col::cream1, col::cream2, 4.0f, pressed ? 0.5f : 2.0f);
    g.setColour (juce::Colour (0xff222222));
    g.setFont (font (8.5f, true, 0.04f));
    g.drawText ("EXPORT MIDI", r, juce::Justification::centred);
}

static MidiExport::Settings exportSettings (TrossSeqProcessor& proc)
{
    auto v = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    MidiExport::Settings s;
    s.length = juce::jlimit (1, kNumSteps, (int) v ("seqlen"));
    s.bpm = proc.currentBpm.load();
    s.velocity = (int) v ("velocity");
    s.accentVelocity = (int) v ("accvel");
    s.gate = v ("gate") / 100.0f;
    s.swing = v ("swing");
    s.flamMs = v ("flamtime");
    s.flamLevel = v ("flamlevel");
    return s;
}

std::vector<int> ExportKey::nonEmptySlots() const
{
    std::vector<int> slots;
    for (int s = 0; s < kNumSlots; ++s)
        if (! proc.patterns.isEmpty (s)) slots.push_back (s);
    return slots;
}

void ExportKey::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true; pressed = false; repaint();
    // write the playing pattern to a temporary file and hand it to the DAW as a file drag
    const int slot = proc.getCurrentSlot();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TRossSeq");
    dir.createDirectory();
    const auto file = dir.getChildFile (MidiExport::fileName (slot));
    if (MidiExport::write (MidiExport::make (proc.patterns, proc.map, { slot }, exportSettings (proc),
                                             "TRoss Seq Pattern " + juce::String (slot + 1)), file))
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void ExportKey::mouseUp (const juce::MouseEvent& e)
{
    const bool wasClick = pressed && ! dragged && getLocalBounds().contains (e.getPosition());
    pressed = false; repaint();
    if (wasClick) showMenu();
}

void ExportKey::showMenu()
{
    const int slot = proc.getCurrentSlot();
    const bool playingEmpty = proc.patterns.isEmpty (slot);
    const auto filled = nonEmptySlots();
    const auto s = exportSettings (proc);
    juce::PopupMenu m;
    m.addSectionHeader ("Export as MIDI (" + juce::String (s.length) + " steps, " + juce::String (juce::roundToInt (s.bpm)) + " BPM)");
    m.addItem (1, "Pattern " + juce::String (slot + 1) + " (playing)" + (playingEmpty ? " - empty" : ""), ! playingEmpty);
    m.addItem (2, "All patterns, in a row (" + juce::String ((int) filled.size()) + " with notes)", ! filled.empty());
    m.addItem (3, "All patterns as separate files", ! filled.empty());
    m.addSeparator();
    m.addItem (4, "Tip: drag this key onto a MIDI track to drop the playing pattern as a clip", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ExportKey> (this), slot, filled] (int id)
                     {
                         if (safe == nullptr) return;
                         if (id == 1) safe->save ({ slot }, MidiExport::fileName (slot));
                         if (id == 2) safe->save (filled, "TRoss Seq Patterns.mid");
                         if (id == 3) safe->exportAllSeparate();
                     });
}

static juce::File exportStartFolder()
{
    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return docs.isDirectory() ? docs : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

void ExportKey::save (std::vector<int> slots, const juce::String& fileName)
{
    auto midi = MidiExport::make (proc.patterns, proc.map, slots, exportSettings (proc),
                                  slots.size() == 1 ? "TRoss Seq Pattern " + juce::String (slots[0] + 1) : juce::String ("TRoss Seq Patterns"));
    chooser = std::make_unique<juce::FileChooser> ("Save as MIDI", exportStartFolder().getChildFile (fileName), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
                          [midi] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f != juce::File()) MidiExport::write (midi, f.withFileExtension ("mid"));
                          });
}

void ExportKey::exportAllSeparate()
{
    std::vector<std::pair<juce::String, juce::MidiFile>> files;
    const auto s = exportSettings (proc);
    for (int slot : nonEmptySlots())
        files.push_back ({ MidiExport::fileName (slot),
                           MidiExport::make (proc.patterns, proc.map, { slot }, s, "TRoss Seq Pattern " + juce::String (slot + 1)) });
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the MIDI files", exportStartFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [files] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir.isDirectory())
                                  for (auto& [name, midi] : files) MidiExport::write (midi, dir.getChildFile (name));
                          });
}

// ================================================================================================
juce::Rectangle<float> PatternPad::buttonRect (int slot)
{
    return { 12.0f + (float) (slot % 2) * 44.0f, 31.0f + (float) (slot / 2) * 29.0f, 38.0f, 21.0f };
}

void PatternPad::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (col::darkTop, 0, 0, col::darkBottom, 0, (float) getHeight(), false));
    g.fillAll();

    g.setColour (col::darkLabel);
    g.setFont (font (8.5f, false, 0.15f));
    g.drawText ("PATTERN", 0, 12, 94, 12, juce::Justification::centred);

    const int sel = (int) proc.apvts.getRawParameterValue ("pattern")->load();
    const int cur = proc.getCurrentSlot(), pending = proc.getPendingSlot();
    const bool blink = (juce::Time::getMillisecondCounter() / 200) % 2 == 0;

    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto r = buttonRect (i);
        if (i == sel) drawKey (g, r, col::cream1, col::cream2, 4.0f, 2.0f);
        else          drawKey (g, r, col::grey1, col::grey2, 4.0f, 2.0f);
        drawLed (g, { r.getX() + 6.5f, r.getY() + 6.5f }, 5.0f, i == cur || (i == pending && blink));
        g.setColour (juce::Colour (0xff222222));
        g.setFont (font (10.0f, true));
        g.drawText (juce::String (i + 1), r.withTrimmedLeft (6.0f), juce::Justification::centred);
        if (proc.patterns.isEmpty (i))   // small dash for empty slots
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (r.getRight() - 9.0f, r.getBottom() - 5.0f, 5.0f, 1.5f);
        }
    }
}

void PatternPad::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumSlots; ++i)
        if (buttonRect (i).expanded (2.0f).contains (e.position))
        {
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu copyTo;
                for (int s = 0; s < kNumSlots; ++s) copyTo.addItem (100 + s, "Pattern " + juce::String (s + 1), s != i);
                juce::PopupMenu m;
                m.addSectionHeader ("Pattern " + juce::String (i + 1));
                m.addSubMenu ("Copy to", copyTo);
                m.addItem (1, "Clear", ! proc.patterns.isEmpty (i));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                 [safe = juce::Component::SafePointer<PatternPad> (this), i] (int id)
                                 {
                                     if (safe == nullptr) return;
                                     if (id == 1) safe->proc.patterns.clear (i);
                                     if (id >= 100) safe->proc.patterns.copySlot (i, id - 100);
                                     safe->repaint();
                                 });
                return;
            }
            if (auto* p = proc.apvts.getParameter ("pattern"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) i));
                p->endChangeGesture();
            }
            repaint();
            return;
        }
}

// ================================================================================================
void StatusLcd::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::lcdBg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    const int cur = proc.getCurrentSlot(), pending = proc.getPendingSlot();
    const juce::String txt = pending >= 0 ? "PAT " + juce::String (cur + 1) + " > " + juce::String (pending + 1)
                           : proc.isRandomPending() ? "PAT " + juce::String (cur + 1) + " > RND"
                                                    : "PATTERN " + juce::String (cur + 1);
    g.setFont (mono (11.5f));
    g.setColour (col::lcdText.withAlpha (0.3f));
    g.drawText (txt, r.expanded (0.5f), juce::Justification::centred);
    g.setColour (col::lcdText);
    g.drawText (txt, r, juce::Justification::centred);
}

// ================================================================================================
PortBox::PortBox (TrossSeqProcessor& p) : proc (p)
{
    setTooltip ("Sends straight to this MIDI port (interface or USB drum box). Works in any host.");
    onChange = [this]
    {
        const int id = getSelectedId();
        if (id == 1) proc.selectPort ({});
        else if (id >= 2 && id - 2 < devices.size()) proc.selectPort (devices[id - 2].identifier);
        refresh();
    };
    refresh();
}

void PortBox::refresh()
{
    devices = juce::MidiOutput::getAvailableDevices();
    clear (juce::dontSendNotification);
    addItem ("NONE", 1);
    const auto cur = proc.getPortIdentifier();
    int selected = 1;
    for (int i = 0; i < devices.size(); ++i)
    {
        addItem (devices[i].name, i + 2);
        if (devices[i].identifier == cur) selected = i + 2;
    }
    if (selected == 1 && proc.getPortName().isNotEmpty())   // saved port that isn't plugged in
        setText (proc.getPortName() + " (OFFLINE)", juce::dontSendNotification);
    else
        setSelectedId (selected, juce::dontSendNotification);
}

// ================================================================================================
TrossSeqEditor::TrossSeqEditor (TrossSeqProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kWidth, kHeight);

    // ---- silver top: feel, MIDI out, drum map
    addKnob ("velocity",  { 42, 94 }, 32);
    addKnob ("accvel",    { 92, 94 }, 32);
    addKnob ("gate",      { 142, 94 }, 32);
    addKnob ("swing",     { 192, 94 }, 32);
    addKnob ("flamtime",  { 242, 94 }, 32).setTooltip ("Flam spacing: how far before the step the grace notes start. Shift-click a pad for a flam, right-click it for more");
    addKnob ("flamlevel", { 292, 94 }, 32).setTooltip ("Flam level: how loud the grace notes are, as % of the step's velocity");

    content.addAndMakeVisible (portBox);
    portBox.setBounds (342, 76, 210, 22);
    auto toggle = [this] (const char* id, const char* text, int y, const char* tip)
    {
        auto& b = addButton (id, text, { 566, y, 84, 15 }, "rubber");
        b.getProperties().set ("onColour", (juce::int64) juce::Colour (0xffdfe8d8).getARGB());
        b.getProperties().set ("led", true);
        b.setTooltip (tip);
    };
    toggle ("hostout",  "HOST OUT",  64, "Also send the notes to the host's MIDI out (route it to a MIDI track in your DAW)");
    toggle ("clockout", "CLOCK OUT", 83, "Send MIDI clock, start and stop to the MIDI OUT port, so the drum box follows the tempo");
    toggle ("thru",     "MIDI THRU", 102, "Pass incoming MIDI (your keyboard) through to the outputs");
    addKnob ("offset", { 690, 94 }, 34).setTooltip ("Delays everything sent to the MIDI OUT port, to line the drum box up with the audio");

    mapBtn.getProperties().set ("style", "key");
    mapBtn.setBounds (742, 76, 110, 24);
    mapBtn.onClick = [this] { showMapMenu(); };
    mapBtn.setTooltip ("Note maps for common drum machines, all tracks on one channel, save as default");
    content.addAndMakeVisible (mapBtn);

    // HELP: the hover tips on/off (remembered for every TRoss Seq)
    helpBtn.getProperties().set ("style", "rubber");
    helpBtn.getProperties().set ("led", true);
    helpBtn.getProperties().set ("onColour", (juce::int64) juce::Colour (0xffdfe8d8).getARGB());
    helpBtn.setClickingTogglesState (true);
    helpBtn.setBounds (kWidth - kMargin - 112, 29, 58, 15);
    {
        juce::PropertiesFile settings (TrossSeqProcessor::settingsOptions());
        helpBtn.setToggleState (settings.getBoolValue ("showHelp", true), juce::dontSendNotification);
    }
    helpBtn.onClick = [this]
    {
        juce::PropertiesFile settings (TrossSeqProcessor::settingsOptions());
        settings.setValue ("showHelp", helpBtn.getToggleState());
        settings.saveIfNeeded();
        updateHelp();
    };
    content.addAndMakeVisible (helpBtn);
    updateHelp();

    content.addAndMakeVisible (lcd);
    lcd.setBounds (kMasterX + 7, 74, kMasterW - 14, 22);

    // ---- sequencer
    content.addAndMakeVisible (grid);
    grid.setBounds (0, kSeqY, 918, kHeight - kSeqY);
    content.addAndMakeVisible (patternPad);
    patternPad.setBounds (918, kSeqY, 100, 206);

    for (int t = 0; t < kNumTracks; ++t)
    {
        const int y = rowY (t + 1);
        auto strip = std::make_unique<NameStrip> (proc, t);
        strip->setBounds (kNameX, y, kNameW, (int) kPadH);
        strip->setTooltip ("Click to play (Cmd/Ctrl-click: accent). Shift-click to rename, Alt-click to lock, right-click to clear and more. "
                           "Records into the pattern while REC is on and the song plays.");
        content.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));

        auto& note = addReadout ({ kNoteX, y, kNoteW, (int) kPadH }, 0, 127, [] (double v) { return juce::String ((int) v) + " " + noteName ((int) v); });
        note.onValueChange = [this, t, &note] { proc.map.setNote (t, (int) note.getValue()); };
        note.setTooltip ("MIDI note: drag up/down");
        noteBox[t] = &note;
        auto& ch = addReadout ({ kChanX, y, kChanW, (int) kPadH }, 1, 16, [] (double v) { return juce::String ((int) v); });
        ch.onValueChange = [this, t, &ch] { proc.map.setChannel (t, (int) ch.getValue()); };
        ch.setTooltip ("MIDI channel: drag up/down");
        chanBox[t] = &ch;

        auto mute = std::make_unique<MuteButton> (proc, t);
        mute->setClickingTogglesState (true);
        mute->getProperties().set ("style", "rubberDark");
        mute->getProperties().set ("onColour", (juce::int64) col::muteOn.getARGB());
        mute->setBounds (kMuteX, y + 1, kMuteW, (int) kPadH - 1);
        mute->setTooltip ("Click: mute. Shift + hold: solo this track until you let go");
        content.addAndMakeVisible (*mute);
        bAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "mute" + juce::String (t + 1), *mute));
        buttons.push_back (std::move (mute));

        auto learn = std::make_unique<juce::TextButton> ("LEARN");
        learn->getProperties().set ("style", "rubberDark");
        learn->getProperties().set ("onColour", (juce::int64) col::orange.getARGB());
        learn->setBounds (kLearnX, y + 1, kLearnW, (int) kPadH - 1);
        learn->setTooltip ("Then hit a key or pad: this track takes its note and channel");
        learn->onClick = [this, t] { proc.learnTrack = proc.learnTrack.load() == t ? -1 : t; };
        content.addAndMakeVisible (*learn);
        learnBtn[t] = learn.get();
        buttons.push_back (std::move (learn));
    }

    auto len = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    len->getProperties().set ("seg", true);
    len->setMouseDragSensitivity (120);
    len->setBounds (1034, kSeqY + 31, 48, 30);
    content.addAndMakeVisible (*len);
    sAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "seqlen", *len));
    len->setDoubleClickReturnValue (true, 16.0);
    len->setTooltip ("Drag up/down to set the pattern length");
    sliders.push_back (std::move (len));

    addButton ("seqon", "RUN", { 1027, kSeqY + 72, 62, 24 }, "key").getProperties().set ("led", true);
    clearBtn.getProperties().set ("style", "key");
    clearBtn.setBounds (1027, kSeqY + 104, 62, 24);
    clearBtn.onClick = [this] { proc.clearCurrentPattern(); grid.repaint(); };
    content.addAndMakeVisible (clearBtn);
    addButton ("rec", "REC", { 1027, kSeqY + 136, 62, 24 }, "rec").getProperties().set ("led", true);
    exportKey.setBounds (1025, kSeqY + 170, 66, 26);
    exportKey.setTooltip ("Click: export patterns as MIDI files. Drag onto a MIDI track: drop the playing pattern as a clip.");
    content.addAndMakeVisible (exportKey);

    // ---- randomiser
    addKnob ("density", { 950, kRndY + 40 }, 34).setTooltip ("How busy random patterns are");
    for (int i = 0; i < kNumStyles; ++i) styleBox.addItem (styleName (i), i + 1);
    styleBox.setBounds (990, kRndY + 22, 114, 22);
    styleBox.setTooltip ("Random style: the character of what RANDOM writes");
    content.addAndMakeVisible (styleBox);
    styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "rstyle", styleBox);
    auto key = [this] (juce::TextButton& b, juce::Rectangle<int> r, const char* tip)
    {
        b.getProperties().set ("style", "key");
        b.setBounds (r);
        b.setTooltip (tip);
        content.addAndMakeVisible (b);
    };
    key (randomBtn, { 926, kRndY + 72, 64, 24 }, "New random pattern (the accent row stays). Shift-click: small variation of this one");
    randomBtn.getProperties().set ("led", true);
    randomBtn.onClick = [this]
    {
        const bool shift = juce::ModifierKeys::getCurrentModifiers().isShiftDown();
        proc.randomise (shift ? TrossSeqProcessor::RandomOp::Mutate : TrossSeqProcessor::RandomOp::NewPattern);
        grid.repaint(); lcd.repaint();
    };
    key (fillBtn, { 996, kRndY + 72, 50, 24 }, "Random fill on the last four steps");
    fillBtn.onClick = [this] { proc.randomise (TrossSeqProcessor::RandomOp::Fill); grid.repaint(); lcd.repaint(); };
    key (undoBtn, { 1052, kRndY + 72, 52, 24 }, "Back to the pattern before the last RANDOM, FILL, CLEAR or lane clear");
    undoBtn.onClick = [this] { proc.undoRandom(); grid.repaint(); lcd.repaint(); };

    if (proc.isStandalone())   // no host transport: own play button + tempo
    {
        playBtn.getProperties().set ("style", "key");
        playBtn.getProperties().set ("led", true);
        playBtn.setClickingTogglesState (true);
        playBtn.setToggleState (proc.internalPlay.load(), juce::dontSendNotification);
        playBtn.onClick = [this] { proc.internalPlay = playBtn.getToggleState(); };
        playBtn.setBounds (991, 100, 50, 24);
        content.addAndMakeVisible (playBtn);

        bpmBox.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        bpmBox.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        bpmBox.getProperties().set ("seg", true);
        bpmBox.getProperties().set ("segSize", 10.5f);
        bpmBox.setRange (40.0, 240.0, 1.0);
        bpmBox.setValue (proc.internalBpm.load(), juce::dontSendNotification);
        bpmBox.setMouseDragSensitivity (300);
        bpmBox.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " BPM"; };
        bpmBox.onValueChange = [this] { proc.internalBpm = (float) bpmBox.getValue(); };
        bpmBox.setBounds (1046, 102, 50, 20);
        content.addAndMakeVisible (bpmBox);
    }

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (30);
}

TrossSeqEditor::~TrossSeqEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Slider& TrossSeqEditor::addKnob (const juce::String& paramId, juce::Point<int> centre, int diameter)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    const int box = diameter + 6;
    s->setBounds (juce::Rectangle<int> (box, box).withCentre (centre));
    s->setPopupDisplayEnabled (true, true, this, 1200);
    content.addAndMakeVisible (*s);
    sAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, *s));
    if (auto* p = proc.apvts.getParameter (paramId))
        s->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    sliders.push_back (std::move (s));
    return *sliders.back();
}

juce::Button& TrossSeqEditor::addButton (const juce::String& paramId, const juce::String& text, juce::Rectangle<int> r, const char* style)
{
    auto b = std::make_unique<juce::TextButton> (text);
    b->setClickingTogglesState (true);
    b->getProperties().set ("style", style);
    b->setBounds (r);
    content.addAndMakeVisible (*b);
    bAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramId, *b));
    buttons.push_back (std::move (b));
    return *buttons.back();
}

juce::Slider& TrossSeqEditor::addReadout (juce::Rectangle<int> r, double lo, double hi, std::function<juce::String (double)> text)
{
    auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
    s->getProperties().set ("seg", true);
    s->getProperties().set ("segSize", 11.0f);
    s->setRange (lo, hi, 1.0);
    s->setMouseDragSensitivity ((int) juce::jlimit (80.0, 400.0, (hi - lo) * 3.0));
    s->textFromValueFunction = std::move (text);
    s->setBounds (r);
    content.addAndMakeVisible (*s);
    sliders.push_back (std::move (s));
    return *sliders.back();
}

void TrossSeqEditor::showMapMenu()
{
    const auto maps = TrossSeqProcessor::listMaps();
    const auto current = proc.getMapName();
    const bool modified = proc.isMapModified();

    juce::PopupMenu m;
    static const char* kinds[3] = { "Built-in: General MIDI", "Built-in: hardware", "Built-in: software" };
    for (int k = 0; k < 3; ++k)
    {
        m.addSectionHeader (kinds[k]);
        for (int i = 0; i < TrackMap::kNumPresets; ++i)
            if (TrackMap::preset (i).kind == k)
                m.addItem (1 + i, juce::String (TrackMap::preset (i).name) + "  (ch " + juce::String (TrackMap::preset (i).channel) + ")",
                           true, current == TrackMap::preset (i).name && ! modified);
    }

    m.addSectionHeader ("My maps");
    if (maps.isEmpty()) m.addItem (-1, "(none saved yet: use Save map as...)", false);
    for (int i = 0; i < maps.size(); ++i)
        m.addItem (200 + i, maps[i], true, maps[i] == current && ! modified);
    m.addSeparator();
    m.addItem (301, "Save" + (proc.isUserMap() ? " \"" + current + "\"" : juce::String()), proc.isUserMap() && modified);
    m.addItem (300, "Save map as...");
    juce::PopupMenu ren, del;
    for (int i = 0; i < maps.size(); ++i) { ren.addItem (400 + i, maps[i]); del.addItem (500 + i, maps[i]); }
    m.addSubMenu ("Rename map", ren, ! maps.isEmpty());
    m.addSubMenu ("Delete map", del, ! maps.isEmpty());
    m.addItem (302, "Show maps folder");

    juce::PopupMenu chans;
    for (int c = 1; c <= 16; ++c) chans.addItem (100 + c, "Channel " + juce::String (c));
    m.addSeparator();
    m.addSubMenu ("All lanes on channel", chans);
    m.addItem (50, "Save map + MIDI port as default for new instances");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mapBtn),
                     [safe = juce::Component::SafePointer<TrossSeqEditor> (this), maps] (int id)
                     {
                         if (safe == nullptr || id <= 0) return;
                         auto& p = safe->proc;
                         if (id >= 1 && id <= TrackMap::kNumPresets) p.applyPreset (id - 1);
                         else if (id > 100 && id <= 116) p.map.setAllChannels (id - 100);
                         else if (id == 50) p.saveAsDefault();
                         else if (id >= 200 && id < 300) p.loadMap (maps[id - 200]);
                         else if (id == 300) safe->saveMapAs();
                         else if (id == 301) p.saveMap (p.getMapName());
                         else if (id == 302)
                         {
                             TrossSeqProcessor::mapsFolder().createDirectory();
                             TrossSeqProcessor::mapsFolder().revealToUser();
                         }
                         else if (id >= 400 && id < 500) safe->renameMap (maps[id - 400]);
                         else if (id >= 500 && id < 600) safe->deleteMap (maps[id - 500]);
                         safe->content.repaint (0, 0, kWidth, kTopH);
                     });
}

// a small name prompt in the panel's colours; `done` gets the trimmed name (not called on Cancel)
void TrossSeqEditor::askName (const juce::String& title, const juce::String& initial, const juce::String& okText,
                              std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, "Name:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", initial);
    w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (auto* ed = w->getTextEditor ("name")) { ed->setInputRestrictions (40); ed->selectAll(); }
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) done (name);
    }), true);
}

void TrossSeqEditor::confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                              std::function<void()> yes)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([yes] (int r) { if (r == 1) yes(); }), true);
}

void TrossSeqEditor::saveMapAs()
{
    const auto current = proc.getMapName();
    askName ("Save drum map", current.isNotEmpty() ? current : juce::String ("My drum box"), "Save",
             [safe = juce::Component::SafePointer<TrossSeqEditor> (this)] (juce::String name)
             {
                 if (safe == nullptr) return;
                 auto save = [safe, name]
                 {
                     if (safe == nullptr) return;
                     if (! safe->proc.saveMap (name))
                         safe->confirm ("Couldn't save the map", "Check that Documents/TRoss Seq/Maps can be written to.", "OK", [] {});
                     safe->content.repaint (0, 0, kWidth, kTopH);
                 };
                 if (TrossSeqProcessor::mapExists (name))
                     safe->confirm ("Replace \"" + name + "\"?", "A map with this name already exists.", "Replace", save);
                 else
                     save();
             });
}

void TrossSeqEditor::renameMap (const juce::String& from)
{
    askName ("Rename \"" + from + "\"", from, "Rename",
             [safe = juce::Component::SafePointer<TrossSeqEditor> (this), from] (juce::String to)
             {
                 if (safe == nullptr || to == from) return;
                 if (! TrossSeqProcessor::renameMap (from, to))
                     safe->confirm ("Couldn't rename the map", "There may already be a map called \"" + to + "\".", "OK", [] {});
                 else if (safe->proc.getMapName() == from && ! safe->proc.isMapModified())
                     safe->proc.loadMap (to);   // keep showing the renamed map as the current one
                 safe->content.repaint (0, 0, kWidth, kTopH);
             });
}

void TrossSeqEditor::deleteMap (const juce::String& name)
{
    confirm ("Delete \"" + name + "\"?", "The map file goes to the bin / trash. Lanes keep their current settings.", "Delete",
             [safe = juce::Component::SafePointer<TrossSeqEditor> (this), name]
             {
                 TrossSeqProcessor::deleteMap (name);
                 if (safe != nullptr) safe->content.repaint (0, 0, kWidth, kTopH);
             });
}

void TrossSeqEditor::paint (juce::Graphics& g) { g.fillAll (col::darkBottom); }

void TrossSeqEditor::updateHelp()
{
    if (helpBtn.getToggleState())
    {
        if (tooltips == nullptr) tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
        helpBtn.setTooltip ("Hover tips on. Click to switch them off");
    }
    else
    {
        tooltips.reset();   // no tooltip window, no tips (knob value read-outs still show)
        helpBtn.setTooltip ({});
    }
}

void TrossSeqEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

void TrossSeqEditor::paintPanel (juce::Graphics& g)
{
    // ---- brushed silver top
    g.setGradientFill (juce::ColourGradient::vertical (col::silverTop, 0.0f, col::silverBottom, (float) kTopH));
    g.fillRect (0, 0, kWidth, kTopH);
    for (int x = 0; x < kWidth; x += 3)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (x, 0, 1, kTopH);
        g.setColour (juce::Colours::black.withAlpha (0.025f));
        g.fillRect (x + 1, 0, 2, kTopH);
    }

    // header: name, subtitle, build version
    g.setColour (col::ink);
    auto logo = font (30.0f, true, -0.03f);
    g.setFont (logo);
    const float troW = juce::GlyphArrangement::getStringWidth (logo, "TRoss ");
    g.drawText ("TRoss ", juce::Rectangle<float> ((float) kMargin, 12.0f, troW + 4.0f, 34.0f), juce::Justification::bottomLeft);
    const float seqW = juce::GlyphArrangement::getStringWidth (logo, "SEQ");
    g.setColour (col::orange);
    g.drawText ("SEQ", juce::Rectangle<float> ((float) kMargin + troW, 12.0f, seqW + 4.0f, 34.0f), juce::Justification::bottomLeft);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.setFont (font (10.5f, false, 0.3f));
    g.drawText ("16-STEP MIDI DRUM SEQUENCER", juce::Rectangle<float> ((float) kMargin + troW + seqW + 14.0f, 26.0f, 400.0f, 16.0f),
                juce::Justification::bottomLeft);
    g.setColour (col::softText);
    g.setFont (font (10.0f, false, 0.2f));
    g.drawText ("v" JucePlugin_VersionString, kWidth - kMargin - 200, 26, 200, 16, juce::Justification::bottomRight);
    g.setColour (juce::Colour (0xff2b2b2b));
    g.fillRect (kMargin, 51, kWidth - 2 * kMargin, 2);

    auto label = [&] (const juce::String& text, int x, int y, int w, juce::Justification j = juce::Justification::centred)
    {
        g.setColour (col::printText);
        g.setFont (font (8.5f, false, 0.1f));
        g.drawText (text, x, y, w, 11, j);
    };
    auto divider = [&] (int x)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (x, 60, 1, 56);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillRect (x + 1, 60, 1, 56);
    };

    label ("VELOCITY", 12, 61, 60);
    label ("ACCENT", 62, 61, 60);
    label ("GATE", 112, 61, 60);
    label ("SWING", 162, 61, 60);
    label ("FLAM", 212, 61, 60);
    label ("FLAM LVL", 262, 61, 60);
    divider (324);

    label ("MIDI OUT PORT", 342, 61, 210, juce::Justification::centredLeft);
    {
        juce::String status;
        auto c = col::softText;
        if (proc.directOut.isOpen())                 { status = "CONNECTED"; c = col::okGreen; }
        else if (proc.getPortError().isNotEmpty())   { status = proc.getPortError().toUpperCase(); c = col::orange; }
        else if (proc.getPortName().isNotEmpty())    { status = "NOT CONNECTED: PLUG IN / PICK ANOTHER"; c = col::orange; }
        else                                         status = "NONE: ONLY THE HOST'S MIDI OUT";
        g.setColour (c);
        g.setFont (font (8.0f, true, 0.08f));
        g.drawText (status, 342, 103, 220, 12, juce::Justification::centredLeft);
    }
    label ("PORT DELAY", 660, 61, 60);
    divider (726);

    label ("DRUM MAP", 742, 61, 110, juce::Justification::centredLeft);
    g.setColour (col::softText);
    g.setFont (font (8.0f, false, 0.06f));
    {
        auto name = proc.getMapName();
        const bool mod = proc.isMapModified();
        if (name.isEmpty()) name = "CUSTOM";
        g.setFont (font (8.5f, true, 0.06f));
        g.setColour (col::printText);
        const juce::String txt = "MAP: " + name.toUpperCase();
        g.drawText (txt, 742, 104, 210, 12, juce::Justification::centredLeft, true);
        if (mod)   // changed since it was picked or saved
        {
            const float w = juce::GlyphArrangement::getStringWidth (font (8.5f, true, 0.06f), txt);
            g.setColour (col::orange);
            g.drawText ("*  EDITED", 742 + (int) juce::jmin (w, 150.0f) + 4, 104, 60, 12, juce::Justification::centredLeft);
        }
    }

    // right section: pattern display, and PLAY / BPM in the standalone app
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    g.fillRoundedRectangle ((float) kMasterX, 58.0f, (float) kMasterW, 60.0f, 6.0f);
    label (proc.isStandalone() ? "TRANSPORT" : "FOLLOWS HOST", kMasterX, 61, kMasterW);
    if (! proc.isStandalone())
    {
        g.setColour (col::softText);
        g.setFont (font (8.0f, false, 0.06f));
        g.drawText (juce::String (juce::roundToInt (proc.currentBpm.load())) + " BPM", kMasterX, 102, kMasterW, 12, juce::Justification::centred);
    }

    // ---- dark sequencer section
    g.setColour (col::orange);
    g.fillRect (0, kTopH, kWidth, kSeqY - kTopH);
    g.setGradientFill (juce::ColourGradient::vertical (col::darkTop, (float) kSeqY, col::darkBottom, (float) kHeight));
    g.fillRect (0, kSeqY, kWidth, kHeight - kSeqY);

    g.setColour (col::darkLabel);
    g.setFont (font (8.5f, false, 0.15f));
    g.drawText ("LENGTH", 1018, kSeqY + 12, 80, 12, juce::Justification::centred);

    // randomiser block
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (926, kRndY - 4, 178, 1);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (926, kRndY - 3, 178, 1);
    g.setColour (col::darkLabel);
    g.setFont (font (8.5f, false, 0.15f));
    g.drawText ("DENSITY", 920, kRndY + 6, 60, 12, juce::Justification::centred);
    g.drawText ("RANDOM STYLE", 990, kRndY + 6, 114, 12, juce::Justification::centredLeft);
}

void TrossSeqEditor::timerCallback()
{
    const int v = proc.patterns.getVersion(), st = proc.getPlayStep();
    const int slot = proc.getCurrentSlot(), pend = proc.getPendingSlot();
    const int len = (int) proc.apvts.getRawParameterValue ("seqlen")->load();
    const int sel = (int) proc.apvts.getRawParameterValue ("pattern")->load();

    if (v != lastVersion || st != lastStep || slot != lastSlot || len != lastLen)
        grid.repaint();
    if (v != lastVersion || slot != lastSlot || pend != lastPending || pend >= 0 || sel != lastSel)
    {
        patternPad.repaint();
        lcd.repaint();
    }

    // track map: follow presets, MIDI learn and state loads (but not while the user drags a read-out)
    const int mv = proc.map.getVersion();
    {
        const auto label = proc.getMapName() + (proc.isMapModified() ? "*" : "");
        if (label != lastMapLabel) { content.repaint (730, 58, 240, 62); lastMapLabel = label; }
    }
    if (mv != lastMapVersion)
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (! noteBox[t]->isMouseButtonDown()) noteBox[t]->setValue (proc.map.note (t), juce::dontSendNotification);
            if (! chanBox[t]->isMouseButtonDown()) chanBox[t]->setValue (proc.map.channel (t), juce::dontSendNotification);
            noteBox[t]->repaint(); chanBox[t]->repaint();
        }
        lastMapVersion = mv;
    }
    const int learn = proc.learnTrack.load();
    if (learn != lastLearn)
    {
        for (int t = 0; t < kNumTracks; ++t) learnBtn[t]->setToggleState (t == learn, juce::dontSendNotification);
        lastLearn = learn;
    }
    else if (learn >= 0)   // blink while waiting for a note
        learnBtn[learn]->setToggleState ((juce::Time::getMillisecondCounter() / 250) % 2 == 0, juce::dontSendNotification);

    const int nv = proc.getNameVersion(), solo = proc.momentarySolo.load();
    const auto now = juce::Time::getMillisecondCounter();
    for (int t = 0; t < kNumTracks; ++t)
        if (nv != lastNameVersion || solo != lastSolo || now - strips[(size_t) t]->clickTime < 240) strips[(size_t) t]->repaint();
    lastNameVersion = nv; lastSolo = solo;

    // randomiser: RANDOM blinks while a result waits for step 1; UNDO only when there's something to undo
    const bool pendingRnd = proc.isRandomPending();
    randomBtn.setToggleState (pendingRnd && (now / 200) % 2 == 0, juce::dontSendNotification);
    undoBtn.setEnabled (proc.canUndo());
    undoBtn.setAlpha (proc.canUndo() ? 1.0f : 0.55f);
    if (pendingRnd != lastPendingRnd) { lcd.repaint(); lastPendingRnd = pendingRnd; }
    const int lv = proc.lockVersion.load();
    if (lv != lastLockVersion) { for (auto& strip : strips) strip->repaint(); lastLockVersion = lv; }

    // MIDI port status line + host tempo
    const auto portText = proc.getPortName() + (proc.directOut.isOpen() ? "1" : "0") + proc.getPortError()
                          + juce::String (juce::roundToInt (proc.currentBpm.load()));
    if (portText != lastPortText)
    {
        if (! portBox.isPopupActive()) portBox.refresh();
        content.repaint (0, 0, kWidth, kTopH);
        lastPortText = portText;
    }

    lastVersion = v; lastStep = st; lastSlot = slot; lastPending = pend; lastLen = len; lastSel = sel;
}
