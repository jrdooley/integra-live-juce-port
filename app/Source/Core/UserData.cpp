#include "UserData.h"

namespace integra
{
    namespace
    {
        juce::String childText (const juce::XmlElement& xml, const char* tag)
        {
            if (auto* child = xml.getChildByName (tag))
                return child->getAllSubText().trim();
            return {};
        }

        bool hasChild (const juce::XmlElement& xml, const char* tag)  { return xml.getChildByName (tag) != nullptr; }

        void addText (juce::XmlElement& xml, const char* tag, const juce::String& text)
        {
            xml.createNewChildElement (tag)->addTextElement (text);
        }

        juce::String number (double v)
        {
            // Flex wrote plain Number.toString(); keep integers integral
            if (juce::exactlyEqual (v, std::floor (v)) && std::abs (v) < 1.0e9)
                return juce::String ((juce::int64) v);
            return juce::String (v, 10).trimCharactersAtEnd ("0");
        }

        juce::String boolText (bool b)    { return b ? "true" : "false"; }

        juce::String serialise (const juce::XmlElement& xml)
        {
            return xml.toString (juce::XmlElement::TextFormat().withoutHeader().singleLine());
        }
    }

    //==============================================================================
    std::unique_ptr<juce::XmlElement> UserDataBase::parse (const juce::String& text)
    {
        if (text.trim().isEmpty())
            return {};

        auto xml = juce::parseXML (text);
        if (xml != nullptr && xml->hasTagName ("userdata"))
            return xml;

        return {};
    }

    void UserDataBase::readBase (const juce::XmlElement& xml, const juce::StringArray& handledTags)
    {
        primarySelectedChild = childText (xml, "primarySelectedChild");

        for (auto* child : xml.getChildWithTagNameIterator ("selectedChild"))
            selectedChildren.add (child->getAllSubText().trim());

        for (auto* child : xml.getChildIterator())
        {
            auto tag = child->getTagName();
            if (tag != "primarySelectedChild" && tag != "selectedChild" && ! handledTags.contains (tag))
                unknownElements.add (serialise (*child));
        }
    }

    void UserDataBase::writeBase (juce::XmlElement& xml) const
    {
        if (primarySelectedChild.isNotEmpty())
            addText (xml, "primarySelectedChild", primarySelectedChild);

        for (auto& s : selectedChildren)
            addText (xml, "selectedChild", s);

        for (auto& unknown : unknownElements)
            if (auto parsed = juce::parseXML (unknown))
                xml.addChildElement (parsed.release());
    }

    //==============================================================================
    BlockUserData BlockUserData::fromString (const juce::String& text)
    {
        BlockUserData d;
        auto xml = parse (text);
        if (xml == nullptr)
            return d;

        d.readBase (*xml, { "modulePositions", "liveViewControls", "envelopeLock", "curvatureMode" });

        if (auto* positions = xml->getChildByName ("modulePositions"))
        {
            for (auto* p : positions->getChildWithTagNameIterator ("modulePosition"))
            {
                d.modulePositions[p->getStringAttribute ("name")] = {
                    childText (*p, "x").getFloatValue(), childText (*p, "y").getFloatValue(),
                    childText (*p, "width").getFloatValue(), childText (*p, "height").getFloatValue() };
            }
        }

        if (auto* controls = xml->getChildByName ("liveViewControls"))
        {
            for (auto* c : controls->getChildWithTagNameIterator ("liveViewControl"))
            {
                LiveViewControl lvc;
                lvc.moduleName = childText (*c, "moduleName");
                lvc.widgetLabel = childText (*c, "controlInstanceName");
                lvc.bounds = { childText (*c, "left").getFloatValue(), childText (*c, "top").getFloatValue(),
                               childText (*c, "width").getFloatValue(), childText (*c, "height").getFloatValue() };
                d.liveViewControls.push_back (lvc);
            }
        }

        // Flex wrote <envelopeLock> but (by a typo) read xml.envelopLock, so it never
        // restored the lock. Read the element it actually wrote.
        d.envelopeLock = childText (*xml, "envelopeLock") == "true";
        d.curvatureMode = childText (*xml, "curvatureMode") == "true";
        return d;
    }

    juce::String BlockUserData::toString() const
    {
        juce::XmlElement xml ("userdata");
        writeBase (xml);

        auto* positions = xml.createNewChildElement ("modulePositions");
        for (auto& [name, r] : modulePositions)
        {
            auto* p = positions->createNewChildElement ("modulePosition");
            p->setAttribute ("name", name);
            addText (*p, "x", number (r.getX()));
            addText (*p, "y", number (r.getY()));
            addText (*p, "width", number (r.getWidth()));
            addText (*p, "height", number (r.getHeight()));
        }

        auto* controls = xml.createNewChildElement ("liveViewControls");
        for (auto& lvc : liveViewControls)
        {
            auto* c = controls->createNewChildElement ("liveViewControl");
            addText (*c, "moduleName", lvc.moduleName);
            addText (*c, "controlInstanceName", lvc.widgetLabel);
            addText (*c, "left", number (lvc.bounds.getX()));
            addText (*c, "top", number (lvc.bounds.getY()));
            addText (*c, "width", number (lvc.bounds.getWidth()));
            addText (*c, "height", number (lvc.bounds.getHeight()));
        }

        if (envelopeLock)   addText (xml, "envelopeLock", "true");
        if (curvatureMode)  addText (xml, "curvatureMode", "true");

        return serialise (xml);
    }

    const BlockUserData::LiveViewControl* BlockUserData::findLiveViewControl (const juce::String& module, const juce::String& widget) const
    {
        for (auto& c : liveViewControls)
            if (c.moduleName == module && c.widgetLabel == widget)
                return &c;
        return nullptr;
    }

    void BlockUserData::removeModule (const juce::String& moduleName)
    {
        modulePositions.erase (moduleName);
        liveViewControls.erase (std::remove_if (liveViewControls.begin(), liveViewControls.end(),
                                                [&] (const LiveViewControl& c) { return c.moduleName == moduleName; }),
                                liveViewControls.end());
        selectedChildren.removeString (moduleName);
        if (primarySelectedChild == moduleName)
            primarySelectedChild = {};
    }

    void BlockUserData::renameModule (const juce::String& oldName, const juce::String& newName)
    {
        auto it = modulePositions.find (oldName);
        if (it != modulePositions.end())
        {
            auto r = it->second;
            modulePositions.erase (it);
            modulePositions[newName] = r;
        }

        for (auto& c : liveViewControls)
            if (c.moduleName == oldName)
                c.moduleName = newName;

        for (auto& s : selectedChildren)
            if (s == oldName)
                s = newName;

        if (primarySelectedChild == oldName)
            primarySelectedChild = newName;
    }

    //==============================================================================
    TrackUserData TrackUserData::fromString (const juce::String& text)
    {
        TrackUserData d;
        auto xml = parse (text);
        if (xml == nullptr)
            return d;

        d.readBase (*xml, { "color", "arrangeViewHeight", "arrangeViewExpanded", "liveViewExpanded" });

        if (hasChild (*xml, "color"))               d.colour = (juce::uint32) childText (*xml, "color").getLargeIntValue();
        if (hasChild (*xml, "arrangeViewHeight"))   d.arrangeViewHeight = childText (*xml, "arrangeViewHeight").getIntValue();
        if (hasChild (*xml, "arrangeViewExpanded")) d.arrangeViewExpanded = childText (*xml, "arrangeViewExpanded") == "true";
        if (hasChild (*xml, "liveViewExpanded"))    d.liveViewExpanded = childText (*xml, "liveViewExpanded") == "true";
        return d;
    }

    juce::String TrackUserData::toString() const
    {
        juce::XmlElement xml ("userdata");
        writeBase (xml);
        addText (xml, "color", juce::String ((juce::int64) (colour & 0xffffff)));
        addText (xml, "arrangeViewHeight", juce::String (arrangeViewHeight));
        addText (xml, "arrangeViewExpanded", boolText (arrangeViewExpanded));
        addText (xml, "liveViewExpanded", boolText (liveViewExpanded));
        return serialise (xml);
    }

    //==============================================================================
    ProjectUserData ProjectUserData::fromString (const juce::String& text)
    {
        ProjectUserData d;
        auto xml = parse (text);
        if (xml == nullptr)
            return d;

        d.readBase (*xml, { "viewMode", "blockPropertiesOpen", "popups", "timelineScroll", "timelineZoom",
                            "colorScheme", "highContrast", "showSceneInTitlebar" });

        if (hasChild (*xml, "viewMode"))            d.viewMode = childText (*xml, "viewMode");
        if (hasChild (*xml, "blockPropertiesOpen")) d.blockPropertiesOpen = childText (*xml, "blockPropertiesOpen") == "true";
        if (auto* popups = xml->getChildByName ("popups"))
            for (auto* p : popups->getChildWithTagNameIterator ("popup"))
                d.popups.add (p->getAllSubText().trim());
        if (hasChild (*xml, "timelineScroll"))      d.timelineScroll = childText (*xml, "timelineScroll").getDoubleValue();
        if (hasChild (*xml, "timelineZoom"))        d.timelineZoom = juce::jlimit (0.04, 25.0, childText (*xml, "timelineZoom").getDoubleValue());
        if (hasChild (*xml, "colorScheme"))         d.colorScheme = childText (*xml, "colorScheme");
        d.highContrast = childText (*xml, "highContrast") == "true";
        d.showSceneInTitlebar = childText (*xml, "showSceneInTitlebar") == "true";
        return d;
    }

    juce::String ProjectUserData::toString() const
    {
        juce::XmlElement xml ("userdata");
        addText (xml, "viewMode", viewMode);
        addText (xml, "blockPropertiesOpen", boolText (blockPropertiesOpen));
        auto* popupStack = xml.createNewChildElement ("popups");
        for (auto& p : popups)
            addText (*popupStack, "popup", p);
        addText (xml, "timelineScroll", number (timelineScroll));
        addText (xml, "timelineZoom", number (timelineZoom));
        addText (xml, "colorScheme", colorScheme);
        if (highContrast)           addText (xml, "highContrast", "true");
        if (showSceneInTitlebar)    addText (xml, "showSceneInTitlebar", "true");
        writeBase (xml);
        return serialise (xml);
    }

    //==============================================================================
    SceneUserData SceneUserData::fromString (const juce::String& text)
    {
        SceneUserData d;
        auto xml = parse (text);
        if (xml == nullptr)
            return d;

        d.readBase (*xml, { "keybinding" });
        d.keybinding = childText (*xml, "keybinding").toUpperCase();   // early port versions stored lower-case
        return d;
    }

    juce::String SceneUserData::toString() const
    {
        juce::XmlElement xml ("userdata");
        writeBase (xml);
        if (keybinding.isNotEmpty())
            addText (xml, "keybinding", keybinding);
        return serialise (xml);
    }
}
