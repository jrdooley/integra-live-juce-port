#include "Markdown.h"

namespace integra
{
    //==============================================================================
    juce::AttributedString renderMarkdown (const juce::String& markdown, juce::Colour text, juce::Colour accent, std::vector<MarkdownLink>* links)
    {
        juce::AttributedString out;
        out.setWordWrap (juce::AttributedString::byWord);

        auto appendInline = [&] (const juce::String& line, float size, bool forceBold)
        {
            // **bold**, *italic*, [text](link)
            int i = 0;
            bool bold = forceBold, italic = false;
            juce::String run;
            auto flush = [&]
            {
                if (run.isEmpty())
                    return;
                juce::FontOptions options (size, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0));
                out.append (run, juce::Font (options), text);
                run.clear();
            };

            while (i < line.length())
            {
                if (line.substring (i).startsWith ("**"))         { flush(); bold = forceBold || ! bold; i += 2; continue; }
                if (line[i] == '*' || line[i] == '_')             { flush(); italic = ! italic; ++i; continue; }
                if (line[i] == '[')
                {
                    const int close = line.indexOf (i, "](");
                    const int end = close > 0 ? line.indexOf (close, ")") : -1;
                    if (close > 0 && end > 0)
                    {
                        flush();
                        const auto linkText = line.substring (i + 1, close);
                        const int startChar = out.getText().length();
                        out.append (linkText, juce::Font (juce::FontOptions (size, juce::Font::underlined)), accent);
                        if (links != nullptr)
                            links->push_back ({ { startChar, startChar + linkText.length() }, line.substring (close + 2, end).trim() });
                        i = end + 1;
                        continue;
                    }
                }
                run << juce::String::charToString (line[i]);
                ++i;
            }
            flush();
        };

        for (auto line : juce::StringArray::fromLines (markdown))
        {
            auto trimmed = line.trimStart();
            if (trimmed.startsWith ("### "))        appendInline (trimmed.substring (4), 14.0f, true);
            else if (trimmed.startsWith ("## "))    appendInline (trimmed.substring (3), 16.0f, true);
            else if (trimmed.startsWith ("# "))     appendInline (trimmed.substring (2), 19.0f, true);
            else if (trimmed.startsWith ("- ") || trimmed.startsWith ("* "))
            {
                out.append (juce::String::fromUTF8 ("  \xe2\x80\xa2 "), juce::Font (juce::FontOptions (13.0f)), text);
                appendInline (trimmed.substring (2), 13.0f, false);
            }
            else                                    appendInline (line, 13.0f, false);

            out.append ("\n", juce::Font (juce::FontOptions (13.0f)), text);
        }

        return out;
    }

    int characterAt (const juce::TextLayout& layout, juce::Point<float> p)
    {
        for (int l = 0; l < layout.getNumLines(); ++l)
        {
            auto& line = layout.getLine (l);
            if (p.y < line.lineOrigin.y - line.ascent || p.y > line.lineOrigin.y + line.descent)
                continue;
            for (auto* run : line.runs)
                for (int g = 0; g < run->glyphs.size(); ++g)
                {
                    auto& glyph = run->glyphs.getReference (g);
                    const float x = line.lineOrigin.x + glyph.anchor.x;
                    if (p.x >= x && p.x < x + glyph.width)
                        return run->stringRange.getStart() + g;
                }
        }
        return -1;
    }
}
