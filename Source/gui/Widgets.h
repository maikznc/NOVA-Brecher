#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace bm::ui
{
//==============================================================================
namespace col
{
    inline const juce::Colour chassis     { 0xff0c0d10 };
    inline const juce::Colour chassisHi   { 0xff16181c };
    inline const juce::Colour panelTop    { 0xff1b1d22 };
    inline const juce::Colour panelBottom { 0xff141619 };
    inline const juce::Colour inset       { 0xff0b0c0e };
    inline const juce::Colour control     { 0xff24272d };
    inline const juce::Colour controlHi   { 0xff2e3138 };
    inline const juce::Colour text        { 0xffece9e2 };
    inline const juce::Colour textDim     { 0xff80838b };
    inline const juce::Colour textFaint   { 0xff4a4d55 };

    inline const juce::Colour iso   { 0xff47c6f5 };
    inline const juce::Colour build { 0xffffc23a };
    inline const juce::Colour beat  { 0xffff5f1f };
    inline const juce::Colour brake { 0xffff3150 };
}

inline juce::Font font (float h, bool bold = true, float tracking = 0.0f)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain).withKerningFactor (tracking));
}

inline juce::Font monoFont (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
}

/** Soft light around a rounded rectangle. */
inline void drawGlow (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float amount, float spread = 10.0f)
{
    if (amount <= 0.001f) return;
    constexpr int steps = 7;
    for (int i = steps; i >= 1; --i)
    {
        const float t = (float) i / (float) steps;
        g.setColour (c.withAlpha (amount * 0.10f * (1.0f - t) + amount * 0.012f));
        g.fillRoundedRectangle (r.expanded (spread * t), corner + spread * t);
    }
}

inline void drawScrew (juce::Graphics& g, float cx, float cy)
{
    const float r = 3.6f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d44), cx - r, cy - r,
                                             juce::Colour (0xff121316), cx + r, cy + r, false));
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (cx - r * 0.6f, cy - r * 0.6f, cx + r * 0.6f, cy + r * 0.6f, 1.1f);
}

inline void drawSection (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, const juce::String& title)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (0, 3), 12.0f);

    g.setGradientFill (juce::ColourGradient (col::panelTop, r.getX(), r.getY(), col::panelBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);

    g.setColour (juce::Colours::white.withAlpha (0.055f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 12, r.getRight() - 12);

    // accent tab + title
    drawGlow (g, { r.getX() + 16, r.getY() + 16, 4, 14 }, 2.0f, accent, 0.7f, 6.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (r.getX() + 16, r.getY() + 16, 4, 14, 2.0f);
    g.setColour (col::text);
    g.setFont (font (13.0f, true, 0.22f));
    g.drawText (title, (int) r.getX() + 28, (int) r.getY() + 11, 220, 24, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.drawHorizontalLine ((int) r.getY() + 42, r.getX() + 16, r.getRight() - 16);

    drawScrew (g, r.getRight() - 14, r.getY() + 14);
    drawScrew (g, r.getRight() - 14, r.getBottom() - 14);
    drawScrew (g, r.getX() + 14, r.getBottom() - 14);
}

inline void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                         juce::Justification j = juce::Justification::centred, juce::Colour c = col::textDim)
{
    g.setColour (c);
    g.setFont (font (10.0f, true, 0.18f));
    g.drawText (text, r, j);
}

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::BubbleComponent::backgroundColourId, col::inset);
        setColour (juce::BubbleComponent::outlineColourId, juce::Colours::white.withAlpha (0.15f));
        setColour (juce::TooltipWindow::textColourId, col::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        const auto accent = juce::Colour ((juce::uint32) (juce::int64) s.getProperties().getWithDefault ("accent", (juce::int64) col::beat.getARGB()));
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float ghost = (float) (double) s.getProperties().getWithDefault ("ghost", -1.0);

        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (size, size);
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float thick = juce::jmax (3.0f, size * 0.055f);
        const bool hasGhost = s.getProperties().contains ("ghost");
        const float rArc = size * 0.5f - 4.0f - (hasGhost ? thick * 1.6f + 1.0f : 0.0f);

        // track
        juce::Path track;
        track.addCentredArc (cx, cy, rArc, rArc, 0.0f, start, end, true);
        g.setColour (juce::Colour (0xff2a2d33));
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        const float a = start + pos * (end - start);
        const float from = bipolar ? (start + end) * 0.5f : start;
        if (std::abs (a - from) > 0.001f)
        {
            juce::Path val;
            val.addCentredArc (cx, cy, rArc, rArc, 0.0f, juce::jmin (from, a), juce::jmax (from, a), true);
            g.setColour (accent.withAlpha (0.22f));
            g.strokePath (val, juce::PathStrokeType (thick * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // ghost arc (effective value, e.g. the automatic rise)
        if (ghost > 0.001f)
        {
            juce::Path gp;
            const float rg = rArc + thick * 1.6f;
            gp.addCentredArc (cx, cy, rg, rg, 0.0f, start, start + ghost * (end - start), true);
            g.setColour (accent.withAlpha (0.85f));
            g.strokePath (gp, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // knob body
        const float rk = rArc - thick * 1.8f;
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (cx - rk, cy - rk + 2.5f, rk * 2, rk * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff40444c), cx - rk * 0.5f, cy - rk,
                                                 juce::Colour (0xff17191d), cx + rk * 0.4f, cy + rk, false));
        g.fillEllipse (cx - rk, cy - rk, rk * 2, rk * 2);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (cx - rk + 0.5f, cy - rk + 0.5f, rk * 2 - 1, rk * 2 - 1, 1.0f);

        // knurling
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (int i = 0; i < 24; ++i)
        {
            const float t = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            g.drawLine (cx + std::sin (t) * rk * 0.84f, cy - std::cos (t) * rk * 0.84f,
                        cx + std::sin (t) * rk * 0.97f, cy - std::cos (t) * rk * 0.97f, 1.0f);
        }

        // pointer
        juce::Path p;
        p.addRoundedRectangle (-1.6f, -rk * 0.86f, 3.2f, rk * 0.48f, 1.6f);
        g.setColour (col::text);
        g.fillPath (p, juce::AffineTransform::rotation (a).translated (cx, cy));
    }

    juce::Font getSliderPopupFont (juce::Slider&) override { return font (12.0f); }
};

//==============================================================================
class Knob : public juce::Slider
{
public:
    Knob (juce::Colour accent, bool bipolar = false)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
    {
        getProperties().set ("accent", (juce::int64) accent.getARGB());
        getProperties().set ("bipolar", bipolar);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setPopupDisplayEnabled (true, true, nullptr);
        setMouseDragSensitivity (220);
    }

    void setGhost (float v)
    {
        const double old = (double) getProperties().getWithDefault ("ghost", -1.0);
        if (! getProperties().contains ("ghost") || std::abs (old - (double) v) > 0.002)
        {
            getProperties().set ("ghost", (double) v);
            repaint();
        }
    }
};

//==============================================================================
/** On/off button bound to a bool parameter. */
class ParamToggle : public juce::Component
{
public:
    ParamToggle (juce::RangedAudioParameter& p, juce::String txt, juce::Colour c)
        : text (std::move (txt)), accent (c),
          attachment (p, [this] (float v) { value = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setExtraLit (bool b) { if (b != extra) { extra = b; repaint(); } }

    void mouseDown (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (value ? 0.0f : 1.0f); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = value || extra;
        if (on)
        {
            drawGlow (g, r, 6.0f, accent, 0.55f, 7.0f);
            g.setGradientFill (juce::ColourGradient (accent.brighter (0.25f), r.getX(), r.getY(), accent.darker (0.2f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colour (0xff120a06));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (col::controlHi, r.getX(), r.getY(), col::control, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (r.getX() + 9, r.getCentreY() - 2.5f, 5, 5);
            g.setColour (col::textDim);
        }
        g.setFont (font (11.0f, true, 0.14f));
        g.drawText (text, getLocalBounds().withTrimmedLeft (on ? 0 : 8), juce::Justification::centred);
    }

private:
    juce::String text;
    juce::Colour accent;
    bool value = false, extra = false;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Segmented switch bound to a choice parameter. */
class SegmentSelector : public juce::Component
{
public:
    SegmentSelector (juce::RangedAudioParameter& p, juce::StringArray l, juce::Colour c, int numRows = 1)
        : labels (std::move (l)), accent (c), rows (numRows),
          attachment (p, [this] (float v) { index = (int) std::lround (v); repaint(); })
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getIndex() const { return index; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < labels.size(); ++i)
            if (segment (i).contains (e.position))
            {
                attachment.setValueAsCompleteGesture ((float) i);
                return;
            }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::inset);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        for (int i = 0; i < labels.size(); ++i)
        {
            auto s = segment (i);
            if (i == index)
            {
                drawGlow (g, s, 4.0f, accent, 0.35f, 5.0f);
                g.setGradientFill (juce::ColourGradient (accent.brighter (0.2f), s.getX(), s.getY(), accent.darker (0.15f), s.getX(), s.getBottom(), false));
                g.fillRoundedRectangle (s, 4.0f);
                g.setColour (juce::Colour (0xff120a06));
            }
            else
            {
                g.setColour (col::textDim);
            }
            // shrink the label until it fits its segment
            float fh = 10.5f;
            auto f = font (fh, true, 0.06f);
            while (fh > 8.0f && juce::GlyphArrangement::getStringWidth (f, labels[i]) > s.getWidth() - 6.0f)
                f = font (fh -= 0.5f, true, 0.02f);
            g.setFont (f);
            g.drawText (labels[i], s.toNearestInt(), juce::Justification::centred);
        }
    }

private:
    juce::Rectangle<float> segment (int i) const
    {
        const int perRow = (labels.size() + rows - 1) / rows;
        const int row = i / perRow, colIdx = i % perRow;
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float w = r.getWidth() / (float) perRow;
        const float h = r.getHeight() / (float) rows;
        return { r.getX() + w * colIdx + 1.0f, r.getY() + h * row + 1.0f, w - 2.0f, h - 2.0f };
    }

    juce::StringArray labels;
    juce::Colour accent;
    int rows = 1, index = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Glowing performance pad. Behaviour (hold/latch/timed) lives in the editor. */
class Pad : public juce::Component
{
public:
    Pad (juce::String l, juce::String sub, juce::Colour c, float labelSize = 24.0f)
        : label (std::move (l)), subLabel (std::move (sub)), accent (c), labelHeight (labelSize)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onDown, onUp;

    void setLabel (const juce::String& l) { if (l != label) { label = l; repaint(); } }

    void setState (float newLit, bool newSelected, float newProgress, bool newBadge = false)
    {
        if (std::abs (newLit - lit) > 0.01f || newSelected != selected
            || std::abs (newProgress - progress) > 0.002f || newBadge != badge)
        {
            lit = newLit; selected = newSelected; progress = newProgress; badge = newBadge;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); if (onDown) onDown(); }
    void mouseUp (const juce::MouseEvent&) override   { pressed = false; repaint(); if (onUp) onUp(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (7.0f);
        if (pressed) r = r.reduced (1.0f);
        const float corner = 9.0f;

        drawGlow (g, r, corner, accent, lit, 12.0f);

        // body
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0, 2), corner);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff292c32), r.getX(), r.getY(),
                                                 juce::Colour (0xff1a1c20), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);

        if (lit > 0.0f)
        {
            juce::ColourGradient lg (accent.brighter (0.45f), r.getCentreX(), r.getY(),
                                     accent.darker (0.25f), r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (lg);
            g.setOpacity (lit);
            g.fillRoundedRectangle (r, corner);
            g.setOpacity (1.0f);

            // hot centre
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f * lit), r.getCentreX(), r.getCentreY(),
                                                     juce::Colours::transparentWhite, r.getCentreX(), r.getY() - r.getHeight() * 0.2f, true));
            g.fillRoundedRectangle (r, corner);
        }

        // rim
        g.setColour (accent.withAlpha (selected ? 0.75f : 0.20f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, selected ? 1.5f : 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) r.getY() + 2, r.getX() + corner, r.getRight() - corner);

        // labels
        const auto ink = lit > 0.5f ? juce::Colour (0xff140904) : (selected ? col::text : col::text.withAlpha (0.75f));
        g.setColour (ink);
        g.setFont (font (labelHeight, true, 0.02f));
        auto textArea = r.toNearestInt();
        if (subLabel.isNotEmpty())
            textArea.removeFromBottom (14);
        g.drawText (label, textArea, juce::Justification::centred);

        if (subLabel.isNotEmpty())
        {
            g.setColour (ink.withAlpha (0.6f));
            g.setFont (font (9.0f, true, 0.2f));
            g.drawText (subLabel, r.toNearestInt().removeFromBottom (22), juce::Justification::centred);
        }

        if (badge)
        {
            auto bd = juce::Rectangle<float> (r.getRight() - 22, r.getY() + 6, 16, 14);
            g.setColour (lit > 0.5f ? juce::Colour (0xff140904) : accent);
            g.fillRoundedRectangle (bd, 3.0f);
            g.setColour (lit > 0.5f ? accent : juce::Colour (0xff140904));
            g.setFont (font (10.0f));
            g.drawText ("T", bd.toNearestInt(), juce::Justification::centred);
        }

        if (progress >= 0.0f)
        {
            auto bar = juce::Rectangle<float> (r.getX() + 8, r.getBottom() - 6, (r.getWidth() - 16), 2.5f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (bar, 1.2f);
            g.setColour (lit > 0.5f ? juce::Colour (0xff140904) : accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 1.2f);
        }
    }

private:
    juce::String label, subLabel;
    juce::Colour accent;
    float labelHeight;
    float lit = 0.0f, progress = -1.0f;
    bool selected = false, pressed = false, badge = false;
};

//==============================================================================
/** Spinning record that follows the brake/spin playback speed. */
class Platter : public juce::Component
{
public:
    void advance (double seconds, float rate, bool engaged, float bpm)
    {
        juce::ignoreUnused (bpm);
        angle += rate * seconds * juce::MathConstants<double>::twoPi * (100.0 / 3.0 / 60.0);
        angle = std::fmod (angle, juce::MathConstants<double>::twoPi);
        currentRate = rate;
        isEngaged = engaged;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 6.0f;
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const auto accent = col::brake;

        // speed ring
        const float ring = R + 3.0f;
        juce::Path track;
        track.addCentredArc (cx, cy, ring, ring, 0, -2.4f, 2.4f, true);
        g.setColour (juce::Colour (0xff24262b));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float sp = juce::jlimit (-1.0f, 1.0f, currentRate / (currentRate < 0 ? 3.0f : 1.0f));
        if (std::abs (sp) > 0.005f)
        {
            juce::Path val;
            val.addCentredArc (cx, cy, ring, ring, 0, sp < 0 ? sp * 2.4f : -2.4f, sp < 0 ? 0.0f : -2.4f + sp * 4.8f, true);
            g.setColour ((isEngaged ? accent : col::textDim).withAlpha (isEngaged ? 1.0f : 0.5f));
            g.strokePath (val, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // disc
        const float rd = R - 6.0f;
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillEllipse (cx - rd, cy - rd + 3, rd * 2, rd * 2);
        g.setColour (juce::Colour (0xff09090b));
        g.fillEllipse (cx - rd, cy - rd, rd * 2, rd * 2);

        for (float rr = rd * 0.40f; rr < rd * 0.97f; rr += 2.6f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.028f + 0.012f * std::sin (rr * 0.7f)));
            g.drawEllipse (cx - rr, cy - rr, rr * 2, rr * 2, 0.8f);
        }

        // sheen (fixed light source) and rotating highlight
        const auto rot = juce::AffineTransform::rotation ((float) angle, cx, cy);
        for (int k = 0; k < 2; ++k)
        {
            juce::Path wedge;
            wedge.addPieSegment (cx - rd, cy - rd, rd * 2, rd * 2, -0.35f + k * juce::MathConstants<float>::pi,
                                 0.35f + k * juce::MathConstants<float>::pi, 0.42f);
            g.setColour (juce::Colours::white.withAlpha (0.045f));
            g.fillPath (wedge);
        }

        // label
        const float rl = rd * 0.36f;
        g.setGradientFill (juce::ColourGradient (accent.brighter (0.2f), cx, cy - rl, accent.darker (0.5f), cx, cy + rl, false));
        g.fillEllipse (cx - rl, cy - rl, rl * 2, rl * 2);

        juce::Path mark;
        mark.addRoundedRectangle (-1.5f, -rd * 0.95f, 3.0f, rd * 0.5f, 1.5f);
        g.setColour (col::text.withAlpha (0.85f));
        g.fillPath (mark, rot);

        juce::Path labelText;
        g.setColour (juce::Colour (0xff1a0508).withAlpha (0.8f));
        juce::Path dot;
        dot.addEllipse (-rl * 0.55f, -rl * 0.55f, rl * 0.25f, rl * 0.25f);
        g.fillPath (dot, rot);

        // spindle
        g.setColour (juce::Colour (0xffd8d8d8));
        g.fillEllipse (cx - 3.5f, cy - 3.5f, 7, 7);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (cx - 3.5f, cy - 3.5f, 7, 7, 1.0f);
    }

private:
    double angle = 0.0;
    float currentRate = 1.0f;
    bool isEngaged = false;
};

} // namespace bm::ui
