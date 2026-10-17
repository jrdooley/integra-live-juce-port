#include "ModuleDefinition.h"
#include "CreatorConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace modulecreator
{
    //==============================================================================
    juce::String toDisplayName (StateType t)
    {
        switch (t)
        {
            case StateType::floatType:      return "Float";
            case StateType::integerType:    return "Integer";
            case StateType::stringType:     return "String";
        }
        return {};
    }

    juce::String toXmlName (StateType t)
    {
        switch (t)
        {
            case StateType::floatType:      return "float";
            case StateType::integerType:    return "integer";
            case StateType::stringType:     return "string";
        }
        return {};
    }

    juce::String createGuid()
    {
        // GUID.create: a random (version 4) uuid
        juce::Random& r = juce::Random::getSystemRandom();
        juce::uint8 b[16];
        for (auto& x : b)
            x = (juce::uint8) r.nextInt (256);
        b[6] = (juce::uint8) ((b[6] & 0x0f) | 0x40);
        b[8] = (juce::uint8) ((b[8] & 0x3f) | 0x80);

        juce::String s;
        for (int i = 0; i < 16; ++i)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                s << "-";
            s << juce::String::toHexString (b[i]).paddedLeft ('0', 2);
        }
        return s;
    }

    juce::String dateToString (juce::Time t)
    {
        return juce::String (t.getYear()).paddedLeft ('0', 4) + "-"
             + juce::String (t.getMonth() + 1).paddedLeft ('0', 2) + "-"
             + juce::String (t.getDayOfMonth()).paddedLeft ('0', 2) + " "
             + juce::String (t.getHours()).paddedLeft ('0', 2) + ":"
             + juce::String (t.getMinutes()).paddedLeft ('0', 2) + ":"
             + juce::String (t.getSeconds()).paddedLeft ('0', 2) + "."
             + juce::String (t.getMilliseconds()).paddedLeft ('0', 3) + "000";
    }

    juce::Time stringToDate (const juce::String& s)
    {
        // "YYYY-MM-DD HH:MM:SS[.ffffff]", with 'T' allowed in place of the space
        if (s.length() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' || s[16] != ':')
            return juce::Time::getCurrentTime();

        // milliseconds from the digits, not through a double (0.729 * 1000 floors to 728)
        const auto fraction = s.substring (20).retainCharacters ("0123456789");
        return juce::Time (s.substring (0, 4).getIntValue(), s.substring (5, 7).getIntValue() - 1, s.substring (8, 10).getIntValue(),
                           s.substring (11, 13).getIntValue(), s.substring (14, 16).getIntValue(), s.substring (17, 19).getIntValue(),
                           s[19] == '.' ? (fraction + "000").substring (0, 3).getIntValue() : 0, true);
    }

    bool parseNumber (const juce::String& text, double& result)
    {
        const auto trimmed = text.trim();
        if (trimmed.isEmpty())
            return false;
        const auto utf8 = trimmed.toStdString();
        char* end = nullptr;
        result = std::strtod (utf8.c_str(), &end);
        return end != nullptr && *end == 0 && std::isfinite (result)
            && trimmed.containsOnly ("+-.0123456789eE");     // no "inf", "nan" or hex
    }

    bool doTypesMatch (const juce::String& value, StateType type)
    {
        double number = 0;
        switch (type)
        {
            case StateType::floatType:      return parseNumber (value, number);
            case StateType::integerType:    return parseNumber (value, number) && juce::exactlyEqual (number, std::round (number));
            case StateType::stringType:     return true;
        }
        return false;
    }

    bool isTagValid (const juce::String& tag)
    {
        return tag.containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789 ");
    }

    namespace
    {
        bool isLowerCamelCase (const juce::String& s)   { return s.isNotEmpty() && juce::CharacterFunctions::isLowerCase (s[0]) && s[0] < 128; }
        bool isUpperCamelCase (const juce::String& s)   { return s.isNotEmpty() && s[0] >= 'A' && s[0] <= 'Z'; }

        juce::String childText (const juce::XmlElement& parent, const char* tag)
        {
            if (auto* e = parent.getChildByName (tag))
                return e->getAllSubText();
            return {};
        }

        bool hasChild (const juce::XmlElement& parent, const char* tag)    { return parent.getChildByName (tag) != nullptr; }
        bool childBool (const juce::XmlElement& parent, const char* tag, bool fallback)
        {
            return hasChild (parent, tag) ? childText (parent, tag).trim() == "true" : fallback;
        }

        juce::XmlElement* addText (juce::XmlElement& parent, const juce::String& tag, const juce::String& text)
        {
            auto* e = parent.createNewChildElement (tag);
            e->addTextElement (text);
            return e;
        }

        juce::String boolText (bool b)  { return b ? "true" : "false"; }

        /** The canonical form of a number, for duplicate checks (AS3: Number( value ).toString()). */
        juce::String canonical (const juce::String& value, StateType type)
        {
            double number = 0;
            if (type != StateType::stringType && parseNumber (value, number))
                return juce::String (number, 10).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return value;
        }

        //==============================================================================
        juce::Result loadEndpoint (const juce::XmlElement& xml, Endpoint& e)
        {
            if (! hasChild (xml, "Name"))
                return juce::Result::fail ("endpoint without a name");
            e.name = childText (xml, "Name");
            e.label = childText (xml, "Label");
            e.description = childText (xml, "Description");

            const auto type = childText (xml, "Type");
            if (type == "control")
            {
                e.isControl = true;
                auto* control = xml.getChildByName ("ControlInfo");
                if (control == nullptr)
                    return juce::Result::fail ("control endpoint " + e.name + " has no ControlInfo");

                const auto controlType = childText (*control, "ControlType");
                if (controlType == "state")
                {
                    e.isState = true;
                    auto* state = control->getChildByName ("StateInfo");
                    if (state == nullptr)
                        return juce::Result::fail ("state endpoint " + e.name + " has no StateInfo");

                    const auto stateType = childText (*state, "StateType");
                    if (stateType == "float")           e.stateType = StateType::floatType;
                    else if (stateType == "integer")    e.stateType = StateType::integerType;
                    else if (stateType == "string")     e.stateType = StateType::stringType;
                    else return juce::Result::fail ("endpoint " + e.name + " has an unknown state type");

                    e.isSavedToFile = childBool (*state, "IsSavedToFile", e.isSavedToFile);
                    e.isInputFile = childBool (*state, "IsInputFile", e.isInputFile);

                    auto* constraint = state->getChildByName ("Constraint");
                    if (constraint == nullptr)
                        return juce::Result::fail ("endpoint " + e.name + " has no Constraint");
                    auto* range = constraint->getChildByName ("Range");
                    auto* allowed = constraint->getChildByName ("AllowedStates");
                    if ((range == nullptr) == (allowed == nullptr))
                        return juce::Result::fail ("endpoint " + e.name + " needs either a Range or AllowedStates constraint");

                    if (range != nullptr)
                    {
                        if (! hasChild (*range, "Minimum") || ! hasChild (*range, "Maximum"))
                            return juce::Result::fail ("endpoint " + e.name + " has an incomplete Range");
                        e.useRange = true;
                        e.minimum = childText (*range, "Minimum");
                        e.maximum = childText (*range, "Maximum");
                    }
                    else
                    {
                        e.useRange = false;
                        e.allowedStates.clear();
                        for (auto* s : allowed->getChildWithTagNameIterator ("State"))
                            e.allowedStates.add (s->getAllSubText());
                    }

                    if (! hasChild (*state, "Default"))
                        return juce::Result::fail ("endpoint " + e.name + " has no Default");
                    e.defaultValue = childText (*state, "Default");

                    if (auto* scale = state->getChildByName ("Scale"))
                    {
                        const auto scaleType = childText (*scale, "ScaleType");
                        if (scaleType != "linear" && scaleType != "exponential" && scaleType != "decibel")
                            return juce::Result::fail ("endpoint " + e.name + " has an unknown scale type");
                        e.scale = scaleType;
                    }

                    e.stateLabels.clear();
                    if (auto* labels = state->getChildByName ("StateLabels"))
                        for (auto* l : labels->getChildWithTagNameIterator ("Label"))
                        {
                            if (! hasChild (*l, "State"))
                                return juce::Result::fail ("endpoint " + e.name + " has a state label without a state");
                            e.stateLabels.push_back ({ childText (*l, "State"), childText (*l, "Text") });
                        }
                }
                else if (controlType == "bang")
                {
                    e.isState = false;
                }
                else
                {
                    return juce::Result::fail ("endpoint " + e.name + " has an unknown control type");
                }

                e.canBeSource = childBool (*control, "CanBeSource", e.canBeSource);
                e.canBeTarget = childBool (*control, "CanBeTarget", e.canBeTarget);
                e.isSentToHost = childBool (*control, "IsSentToHost", e.isSentToHost);
            }
            else if (type == "stream")
            {
                e.isControl = false;
                auto* stream = xml.getChildByName ("StreamInfo");
                if (stream == nullptr || childText (*stream, "StreamType") != "audio")
                    return juce::Result::fail ("stream endpoint " + e.name + " is not an audio stream");
                const auto direction = childText (*stream, "StreamDirection");
                if (direction != "input" && direction != "output")
                    return juce::Result::fail ("stream endpoint " + e.name + " has no direction");
                e.isInputStream = direction == "input";
            }
            else
            {
                return juce::Result::fail ("endpoint " + e.name + " has an unknown type");
            }
            return juce::Result::ok();
        }

        void saveEndpoint (const Endpoint& e, juce::XmlElement& parent)
        {
            auto* xml = parent.createNewChildElement ("Endpoint");
            addText (*xml, "Name", e.name);
            if (e.label.isNotEmpty())
                addText (*xml, "Label", e.label);
            if (e.description.isNotEmpty())
                addText (*xml, "Description", e.description);

            if (e.isControl)
            {
                addText (*xml, "Type", "control");
                auto* control = xml->createNewChildElement ("ControlInfo");
                addText (*control, "ControlType", e.isState ? "state" : "bang");
                if (e.isState)
                {
                    auto* state = control->createNewChildElement ("StateInfo");
                    addText (*state, "StateType", toXmlName (e.stateType));
                    addText (*state, "IsSavedToFile", boolText (e.isSavedToFile));
                    addText (*state, "IsInputFile", boolText (e.isInputFile));
                    auto* constraint = state->createNewChildElement ("Constraint");
                    if (e.useRange)
                    {
                        auto* range = constraint->createNewChildElement ("Range");
                        addText (*range, "Minimum", e.minimum);
                        addText (*range, "Maximum", e.maximum);
                    }
                    else
                    {
                        auto* allowed = constraint->createNewChildElement ("AllowedStates");
                        for (auto& s : e.allowedStates)
                            addText (*allowed, "State", s);
                    }
                    addText (*state, "Default", e.defaultValue);
                    if (e.hasScale())
                        addText (*state->createNewChildElement ("Scale"), "ScaleType", e.scale);
                    if (! e.stateLabels.empty())
                    {
                        auto* labels = state->createNewChildElement ("StateLabels");
                        for (auto& l : e.stateLabels)
                        {
                            auto* label = labels->createNewChildElement ("Label");
                            addText (*label, "State", l.value);
                            addText (*label, "Text", l.text);
                        }
                    }
                }
                addText (*control, "CanBeSource", boolText (e.canBeSource));
                addText (*control, "CanBeTarget", boolText (e.canBeTarget));
                addText (*control, "IsSentToHost", boolText (e.isSentToHost));
            }
            else
            {
                addText (*xml, "Type", "stream");
                auto* stream = xml->createNewChildElement ("StreamInfo");
                addText (*stream, "StreamType", "audio");
                addText (*stream, "StreamDirection", e.isInputStream ? "input" : "output");
            }
        }

        //==============================================================================
        /** StateInfo.validateValue: type, then the range or allowed values. */
        bool validateValue (const Endpoint& e, const juce::String& value, juce::String& error)
        {
            if (! doTypesMatch (value, e.stateType))
            {
                error = "'" + value + "' is not of type '" + toDisplayName (e.stateType) + "'";
                return false;
            }

            double minimum = 0, maximum = 0;
            if (e.useRange)
            {
                bool ok = parseNumber (e.minimum, minimum) && parseNumber (e.maximum, maximum);
                if (ok)
                {
                    if (e.stateType == StateType::stringType)
                        ok = value.length() >= minimum && value.length() <= maximum;
                    else
                    {
                        double number = 0;
                        ok = parseNumber (value, number) && number >= minimum && number <= maximum;
                    }
                }
                if (! ok)
                {
                    error = "'" + value + "' is not within the allowed "
                          + (e.stateType == StateType::stringType ? "string length constraint" : "range constraint");
                    return false;
                }
            }
            else
            {
                bool found = false;
                for (auto& allowed : e.allowedStates)
                {
                    double a = 0, b = 0;
                    if (e.stateType == StateType::stringType ? value == allowed
                                                             : (parseNumber (value, a) && parseNumber (allowed, b) && juce::exactlyEqual (a, b)))
                        found = true;
                }
                if (! found)
                {
                    error = "'" + value + "' is not a member of the set of allowed values";
                    return false;
                }
            }
            return true;
        }
    }

    //==============================================================================
    int Widget::findAttribute (const juce::String& attribute) const
    {
        for (size_t i = 0; i < mappings.size(); ++i)
            if (mappings[i].first == attribute)
                return (int) i;
        return -1;
    }

    int Widget::endpointFor (const juce::String& attribute) const
    {
        const int i = findAttribute (attribute);
        return i >= 0 ? mappings[(size_t) i].second : 0;
    }

    juce::String Widget::attributeFor (int endpointUid) const
    {
        for (auto& m : mappings)
            if (m.second == endpointUid)
                return m.first;
        return {};
    }

    //==============================================================================
    ModuleDefinition ModuleDefinition::createNew()
    {
        ModuleDefinition d;
        d.moduleGuid = createGuid();
        d.originGuid = createGuid();
        d.createdDate = d.modifiedDate = juce::Time::getCurrentTime();
        return d;
    }

    juce::Result ModuleDefinition::fromXml (const juce::XmlElement& xml, const CreatorConfig& config, ModuleDefinition& result)
    {
        ModuleDefinition d = createNew();

        if (! xml.hasAttribute ("moduleGuid"))
            return juce::Result::fail ("no module guid");
        d.moduleGuid = xml.getStringAttribute ("moduleGuid");
        if (xml.hasAttribute ("originGuid"))
            d.originGuid = xml.getStringAttribute ("originGuid");

        auto* info = xml.getChildByName ("InterfaceInfo");
        if (info == nullptr)
            return juce::Result::fail ("no InterfaceInfo");
        if (! hasChild (*info, "Name"))
            return juce::Result::fail ("error reading InterfaceInfo section");
        d.name = childText (*info, "Name");
        d.label = childText (*info, "Label");
        d.description = childText (*info, "Description");
        if (auto* tags = info->getChildByName ("Tags"))
            for (auto* tag : tags->getChildWithTagNameIterator ("Tag"))
                d.tags.add (tag->getAllSubText());
        d.implementedInLibIntegra = childBool (*info, "ImplementedInLibIntegra", false);
        d.author = childText (*info, "Author");
        if (hasChild (*info, "CreatedDate"))
            d.createdDate = stringToDate (childText (*info, "CreatedDate"));
        if (hasChild (*info, "ModifiedDate"))
            d.modifiedDate = stringToDate (childText (*info, "ModifiedDate"));

        auto* endpointInfo = xml.getChildByName ("EndpointInfo");
        if (endpointInfo == nullptr)
            return juce::Result::fail ("no EndpointInfo");
        for (auto* endpointXml : endpointInfo->getChildWithTagNameIterator ("Endpoint"))
        {
            Endpoint e;
            e.uid = d.nextUid++;
            auto r = loadEndpoint (*endpointXml, e);
            if (r.failed())
                return juce::Result::fail ("error reading EndpointInfo section: " + r.getErrorMessage());
            d.endpoints.push_back (e);
        }

        if (auto* widgetInfo = xml.getChildByName ("WidgetInfo"))
            for (auto* w : widgetInfo->getChildWithTagNameIterator ("Widget"))
            {
                // WidgetLayoutElement.loadFromXML: a broken widget is skipped, not an error
                Widget widget;
                widget.uid = d.nextUid++;
                widget.type = childText (*w, "WidgetType");
                if (config.findWidget (widget.type) == nullptr || ! hasChild (*w, "Label"))
                    continue;
                widget.ownLabel = childText (*w, "Label");
                auto* position = w->getChildByName ("Position");
                if (position == nullptr || ! hasChild (*position, "X") || ! hasChild (*position, "Y")
                     || ! hasChild (*position, "Width") || ! hasChild (*position, "Height"))
                    continue;
                widget.bounds = { (int) std::lround (childText (*position, "X").getDoubleValue()),
                                  (int) std::lround (childText (*position, "Y").getDoubleValue()),
                                  (int) std::lround (childText (*position, "Width").getDoubleValue()),
                                  (int) std::lround (childText (*position, "Height").getDoubleValue()) };
                bool ok = true;
                if (auto* mappings = w->getChildByName ("AttributeMappings"))
                    for (auto* m : mappings->getChildWithTagNameIterator ("AttributeMapping"))
                    {
                        auto* endpoint = d.findEndpointNamed (childText (*m, "Endpoint"));
                        if (endpoint == nullptr || ! hasChild (*m, "WidgetAttribute"))
                            ok = false;
                        else
                            widget.mappings.emplace_back (childText (*m, "WidgetAttribute"), endpoint->uid);
                    }
                if (! ok || widget.mappings.empty())
                    continue;
                if (widget.mappings.size() == 1 && d.findEndpoint (widget.mappings[0].second)->name != widget.ownLabel)
                    continue;
                d.widgets.push_back (widget);
            }

        if (auto* implementation = xml.getChildByName ("ImplementationInfo"))
        {
            if (! hasChild (*implementation, "PatchName"))
                return juce::Result::fail ("error reading ImplementationInfo section");
            d.patchName = childText (*implementation, "PatchName");
        }

        result = std::move (d);
        return juce::Result::ok();
    }

    std::unique_ptr<juce::XmlElement> ModuleDefinition::toXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> ("InterfaceDeclaration");
        xml->setAttribute ("moduleGuid", moduleGuid);
        xml->setAttribute ("originGuid", originGuid);
        xml->setAttribute ("infoSchemaVersionMajor", 1);
        xml->setAttribute ("infoSchemaVersionMinor", 0);

        auto* info = xml->createNewChildElement ("InterfaceInfo");
        addText (*info, "Name", name);
        if (label.isNotEmpty())
            addText (*info, "Label", label);
        if (description.isNotEmpty())
            addText (*info, "Description", description);
        if (! tags.isEmpty())
        {
            auto* tagsXml = info->createNewChildElement ("Tags");
            for (auto& t : tags)
                addText (*tagsXml, "Tag", t);
        }
        addText (*info, "ImplementedInLibIntegra", boolText (implementedInLibIntegra));
        if (author.isNotEmpty())
            addText (*info, "Author", author);
        addText (*info, "CreatedDate", dateToString (createdDate));
        addText (*info, "ModifiedDate", dateToString (modifiedDate));

        auto* endpointInfo = xml->createNewChildElement ("EndpointInfo");
        for (auto& e : endpoints)
            saveEndpoint (e, *endpointInfo);

        if (! widgets.empty())
        {
            auto* widgetInfo = xml->createNewChildElement ("WidgetInfo");
            for (auto& w : widgets)
            {
                auto* widget = widgetInfo->createNewChildElement ("Widget");
                addText (*widget, "WidgetType", w.type);
                addText (*widget, "Label", labelOf (w));
                auto* position = widget->createNewChildElement ("Position");
                addText (*position, "X", juce::String (w.bounds.getX()));
                addText (*position, "Y", juce::String (w.bounds.getY()));
                addText (*position, "Width", juce::String (w.bounds.getWidth()));
                addText (*position, "Height", juce::String (w.bounds.getHeight()));
                auto* mappings = widget->createNewChildElement ("AttributeMappings");
                for (auto& [attribute, uid] : w.mappings)
                    if (auto* e = findEndpoint (uid))
                    {
                        auto* m = mappings->createNewChildElement ("AttributeMapping");
                        addText (*m, "WidgetAttribute", attribute);
                        addText (*m, "Endpoint", e->name);
                    }
            }
        }

        if (patchName.isNotEmpty())
            addText (*xml->createNewChildElement ("ImplementationInfo"), "PatchName", patchName);

        return xml;
    }

    juce::String ModuleDefinition::toXmlString() const
    {
        juce::XmlElement::TextFormat format;
        format.customHeader = "<?xml version=\"1.0\" ?>";
        format.lineWrapLength = 1000000;
        return toXml()->toString (format);
    }

    //==============================================================================
    std::vector<ValidationIssue> ModuleDefinition::validate (bool developerMode) const
    {
        using Page = ValidationIssue::Page;
        std::vector<ValidationIssue> issues;
        auto add = [&] (Page page, int endpoint, const juce::String& message) { issues.push_back ({ page, endpoint, message }); };

        // InterfaceInfo.validateName / validateTags
        if (name.isEmpty())
            add (Page::interfaceInfo, -1, "You must provide an Interface Name");
        else if (! isUpperCamelCase (name))
            add (Page::interfaceInfo, -1, "Interface Name must use UpperCamelCase");
        else if (! name.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") || name.length() > maximumNameLength)
            add (Page::interfaceInfo, -1, "Interface Name may only contain up to " + juce::String (maximumNameLength) + " letters");

        {
            std::set<juce::String> seen;
            bool hasCore = false;
            juce::String tagError;
            for (auto& tag : tags)
            {
                if (tag.isEmpty())                      tagError = "Tags must not be empty";
                else if (! isTagValid (tag))            tagError = "Tags may only contain lowercase letters, numbers and spaces";
                else if (! seen.insert (tag).second)    tagError = "Duplicate Tags are not allowed";
                if (tagError.isNotEmpty())
                    break;
                hasCore = hasCore || tag == coreTag;
            }
            if (tagError.isEmpty() && tags.size() > maximumNumberOfTags)
                tagError = "No more than " + juce::String (maximumNumberOfTags) + " Tags are allowed";
            if (tagError.isEmpty() && hasCore && ! developerMode)
                tagError = "The tag '" + coreTag + "' is reserved for libIntegra developers";
            if (tagError.isEmpty() && ! hasCore && implementedInLibIntegra)
                tagError = "If module is implemented in libIntegra, it must also have the tag 'core'";
            if (tagError.isNotEmpty())
                add (Page::interfaceInfo, -1, tagError);
        }

        // EndpointList.validateMyOwnContent
        if (endpoints.empty())
            add (Page::endpoints, -1, "You must create at least one Endpoint");
        else if (! developerMode && std::none_of (endpoints.begin(), endpoints.end(), [] (const Endpoint& e) { return ! e.isControl; }))
            add (Page::endpoints, -1, "At least one Endpoint must be an input or output stream");

        const bool hasDataDirectory = findEndpointNamed (dataDirectoryName) != nullptr;

        for (int i = 0; i < (int) endpoints.size(); ++i)
        {
            auto& e = endpoints[(size_t) i];
            auto addE = [&] (const juce::String& message) { add (Page::endpoints, i, e.name + ": " + message); };

            // Endpoint.validateName
            bool unique = true;
            for (int j = 0; j < i; ++j)
                unique = unique && endpoints[(size_t) j].name != e.name;

            if (e.name.isEmpty())
                add (Page::endpoints, i, "You must provide an Endpoint Name");
            else if (! isLowerCamelCase (e.name))
                addE ("Endpoint Name must use lowerCamelCase");
            else if (! e.name.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") || e.name.length() > maximumNameLength)
                addE ("Endpoint Name may only contain up to " + juce::String (maximumNameLength) + " letters and numbers");
            else if (! unique)
                addE ("Endpoint Name must be unique");
            else if (e.name == dataDirectoryName
                     && ! (e.isControl && e.isState && e.stateType == StateType::stringType && ! e.isSavedToFile && ! e.canBeTarget && e.isSentToHost))
                addE ("Endpoint Name \"" + dataDirectoryName + "\" is reserved.  It must be a string with IsSavedToFile=false, CanBeTarget=false and IsSentToHost=true.");
            else if (reservedEndpointNames.contains (e.name))
                addE ("Endpoint Name \"" + e.name + "\" is reserved.  Choose a different name.");

            if (! e.isControl || ! e.isState)
            {
                if (e.isControl)
                    if (auto* w = widgetFor (e.uid); w != nullptr && w->mappings.size() > 1)
                        if (! isLowerCamelCase (labelOf (*w)))
                            addE (labelOf (*w).isEmpty() ? "Shared Widgets must have labels" : "Shared Widget labels must use lowerCamelCase");
                continue;
            }

            // StateInfo.validateInputFile
            if (e.isInputFile)
            {
                if (e.stateType != StateType::stringType)   addE ("Input File Endpoints must be strings");
                else if (! hasDataDirectory)                addE ("Cannot use input files unless interface has a 'dataDirectory' Endpoint");
                else if (! e.isSavedToFile)                 addE ("Cannot use input files unless IsSavedToFile = true");
                else if (! e.isSentToHost)                  addE ("Cannot use input files unless IsSentToHost = true");
                else if (e.canBeTarget)                     addE ("Cannot use input files unless CanBeTarget = false");
            }

            bool constraintValid = true;
            if (e.useRange)
            {
                // Range.validateContent
                const auto expected = e.stateType == StateType::stringType ? StateType::integerType : e.stateType;
                double minimum = 0, maximum = 0;
                if (! doTypesMatch (e.minimum, expected))
                    { addE ("Minimum must be of type '" + toDisplayName (expected) + "'"); constraintValid = false; }
                if (! doTypesMatch (e.maximum, expected))
                    { addE ("Maximum must be of type '" + toDisplayName (expected) + "'"); constraintValid = false; }
                if (constraintValid && parseNumber (e.minimum, minimum) && parseNumber (e.maximum, maximum))
                {
                    if (minimum > maximum)
                        { addE ("Maximum must greater or equal to Minimum"); constraintValid = false; }
                    else if (e.stateType == StateType::stringType && minimum < 0)
                        { addE ("Strings cannot have negative length"); constraintValid = false; }
                    else if (e.hasScale() && e.scale == "exponential")
                    {
                        // Scale.validateContent
                        if (minimum <= 0)
                            addE ("Exponential Scales can only be used when minimum range is greater than 0");
                        else if (maximum <= minimum)
                            addE ("Exponential Scales can only be used when maximum value is greater than minimum value");
                    }
                }
            }
            else
            {
                // AllowedValues.validateContent
                std::set<juce::String> seen;
                if (e.allowedStates.isEmpty())
                    { addE ("You must provide at least one allowed value"); constraintValid = false; }
                for (auto& v : e.allowedStates)
                {
                    if (! doTypesMatch (v, e.stateType))
                        { addE ("Allowed Value '" + v + "' is not of type '" + toDisplayName (e.stateType) + "'"); constraintValid = false; break; }
                    if (! seen.insert (canonical (v, e.stateType)).second)
                        { addE ("Allowed Value list must not contain duplicates.  Found duplicate: '" + v + "'"); constraintValid = false; break; }
                }
            }

            if (constraintValid)
            {
                juce::String error;
                if (! validateValue (e, e.defaultValue, error))
                    addE ("Default Value " + error);

                // StateLabels.validateContent
                std::set<juce::String> seen;
                for (auto& l : e.stateLabels)
                {
                    if (! validateValue (e, l.value, error))
                        { addE ("State Label " + error); break; }
                    if (! seen.insert (canonical (l.value, e.stateType)).second)
                        { addE ("State Labels must not contain duplicates.  Found duplicate: '" + l.value + "'"); break; }
                }
            }

            // WidgetSelection.validateContent
            if (auto* w = widgetFor (e.uid); w != nullptr && w->mappings.size() > 1)
                if (! isLowerCamelCase (labelOf (*w)))
                    addE (labelOf (*w).isEmpty() ? "Shared Widgets must have labels" : "Shared Widget labels must use lowerCamelCase");
        }

        return issues;
    }

    //==============================================================================
    Endpoint* ModuleDefinition::findEndpoint (int uid)
    {
        for (auto& e : endpoints)
            if (e.uid == uid)
                return &e;
        return nullptr;
    }

    const Endpoint* ModuleDefinition::findEndpoint (int uid) const
    {
        return const_cast<ModuleDefinition*> (this)->findEndpoint (uid);
    }

    const Endpoint* ModuleDefinition::findEndpointNamed (const juce::String& endpointName) const
    {
        for (auto& e : endpoints)
            if (e.name == endpointName)
                return &e;
        return nullptr;
    }

    int ModuleDefinition::indexOfEndpoint (int uid) const
    {
        for (size_t i = 0; i < endpoints.size(); ++i)
            if (endpoints[i].uid == uid)
                return (int) i;
        return -1;
    }

    Endpoint& ModuleDefinition::addEndpoint()
    {
        Endpoint e;
        e.uid = nextUid++;
        // EndpointList.newEndpointName gave "<New Endpoint #n>", which never validated;
        // a valid placeholder is friendlier
        do
            e.name = "newEndpoint" + juce::String (++newEndpointNumber);
        while (findEndpointNamed (e.name) != nullptr);
        endpoints.push_back (e);
        return endpoints.back();
    }

    void ModuleDefinition::removeEndpoint (int uid)
    {
        unassignWidget (uid);
        endpoints.erase (std::remove_if (endpoints.begin(), endpoints.end(), [uid] (const Endpoint& e) { return e.uid == uid; }), endpoints.end());
    }

    Widget* ModuleDefinition::widgetFor (int endpointUid)
    {
        for (auto& w : widgets)
            if (w.attributeFor (endpointUid).isNotEmpty())
                return &w;
        return nullptr;
    }

    const Widget* ModuleDefinition::widgetFor (int endpointUid) const
    {
        return const_cast<ModuleDefinition*> (this)->widgetFor (endpointUid);
    }

    juce::String ModuleDefinition::labelOf (const Widget& w) const
    {
        if (w.mappings.size() == 1)
            if (auto* e = findEndpoint (w.mappings[0].second))
                return e->name;
        return w.ownLabel;
    }

    bool ModuleDefinition::unassignWidget (int endpointUid)
    {
        for (size_t i = 0; i < widgets.size(); ++i)
        {
            auto& m = widgets[i].mappings;
            const auto before = m.size();
            m.erase (std::remove_if (m.begin(), m.end(), [endpointUid] (auto& p) { return p.second == endpointUid; }), m.end());
            if (m.size() != before)
            {
                if (m.empty())
                    widgets.erase (widgets.begin() + (long) i);
                return true;
            }
        }
        return false;
    }

    void ModuleDefinition::assignWidget (int endpointUid, const juce::String& widgetType, const juce::String& attribute,
                                         int widgetToShareUid, juce::Point<int> defaultSize)
    {
        unassignWidget (endpointUid);
        if (widgetType.isEmpty() || attribute.isEmpty())
            return;

        for (auto& w : widgets)
            if (widgetToShareUid != 0 && w.uid == widgetToShareUid)
            {
                const int i = w.findAttribute (attribute);
                if (i >= 0)
                    w.mappings[(size_t) i].second = endpointUid;
                else
                    w.mappings.emplace_back (attribute, endpointUid);
                return;
            }

        Widget w;
        w.uid = nextUid++;
        w.type = widgetType;
        w.ownLabel = widgetType.toLowerCase();
        // a free spot to the right of the existing widgets
        int x = 10;
        for (auto& other : widgets)
            x = juce::jmax (x, other.bounds.getRight() + 10);
        w.bounds = { x, 10, defaultSize.x, defaultSize.y };
        w.mappings.emplace_back (attribute, endpointUid);
        widgets.push_back (w);
    }

    void ModuleDefinition::sortEndpoints()
    {
        // Endpoint.getOrder as a sort key: streams, controls with widgets (by widget x, then
        // attribute), controls without widgets; names with a common prefix by their number
        auto naturalLess = [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b) < 0; };

        std::stable_sort (endpoints.begin(), endpoints.end(), [&] (const Endpoint& a, const Endpoint& b)
        {
            if (a.isControl != b.isControl)
                return ! a.isControl;
            if (! a.isControl)
                return naturalLess (a.name, b.name);

            auto* wa = widgetFor (a.uid);
            auto* wb = widgetFor (b.uid);
            if ((wa != nullptr) != (wb != nullptr))
                return wa != nullptr;
            if (wa == nullptr)
                return naturalLess (a.name, b.name);
            if (wa->bounds.getX() != wb->bounds.getX())
                return wa->bounds.getX() < wb->bounds.getX();
            if (wa == wb)
                return wa->attributeFor (a.uid) < wa->attributeFor (b.uid);
            return naturalLess (a.name, b.name);
        });
    }

    void ModuleDefinition::autoArrangeWidgets (int margin)
    {
        std::vector<Widget*> order;
        for (auto& w : widgets)
            order.push_back (&w);
        std::stable_sort (order.begin(), order.end(), [] (Widget* a, Widget* b) { return a->bounds.getCentreX() < b->bounds.getCentreX(); });
        int x = margin;
        for (auto* w : order)
        {
            w->bounds.setPosition (x, margin);
            x += w->bounds.getWidth() + margin;
        }
    }

    juce::StringArray ModuleDefinition::controlsWithoutWidgets() const
    {
        juce::StringArray result;
        for (auto& e : endpoints)
            if (e.isControl && widgetFor (e.uid) == nullptr)
                result.add (e.name);
        return result;
    }

    void ModuleDefinition::regenerateModuleGuid()  { moduleGuid = createGuid(); }
    void ModuleDefinition::touch()                 { modifiedDate = juce::Time::getCurrentTime(); }
}
