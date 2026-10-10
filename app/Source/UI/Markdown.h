#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace integra
{
    /** A [text](url) link in rendered Markdown: its characters in the AttributedString. */
    struct MarkdownLink { juce::Range<int> characters; juce::String url; };

    /** Headings, bullets, **bold**, *italic* and [links](url) as an AttributedString
        (defined in PropertiesPanel.cpp). */
    juce::AttributedString renderMarkdown (const juce::String& markdown, juce::Colour text, juce::Colour accent,
                                           std::vector<MarkdownLink>* links = nullptr);

    /** The character of a laid-out AttributedString under a point (-1 if none). */
    int characterAt (const juce::TextLayout&, juce::Point<float>);
}
