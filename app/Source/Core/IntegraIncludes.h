#pragma once

// libIntegra's public API, plus small conversions between its types and JUCE's.

#include "api/integra_session.h"
#include "api/server_startup_info.h"
#include "api/server_lock.h"
#include "api/server.h"
#include "api/command.h"
#include "api/command_result.h"
#include "api/command_source.h"
#include "api/interface_definition.h"
#include "api/module_manager.h"
#include "api/guid_helper.h"
#include "api/notification_sink.h"
#include "api/node.h"
#include "api/node_endpoint.h"
#include "api/error.h"
#include "api/trace.h"
#include "api/path.h"
#include "api/value.h"

#include <juce_core/juce_core.h>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace integra
{
    using namespace integra_api;

    inline juce::String toString (const std::string& s)     { return juce::String::fromUTF8 (s.c_str()); }
    inline std::string toStd (const juce::String& s)        { return s.toStdString(); }
    inline juce::String toString (const CPath& p)           { return toString (p.get_string()); }
    inline CPath toPath (const juce::String& s)             { return CPath (s.toStdString()); }
    inline juce::String toString (const GUID& g)            { return toString (CGuidHelper::guid_to_string (g)); }

    inline bool guidsEqual (const GUID& a, const GUID& b)   { return std::memcmp (&a, &b, sizeof (GUID)) == 0; }

    /** Joins path elements with '.', skipping empty ones. */
    inline juce::String joinPath (std::initializer_list<juce::String> elements)
    {
        juce::StringArray parts;
        for (auto& e : elements)
            if (e.isNotEmpty())
                parts.add (e);
        return parts.joinIntoString (".");
    }

    inline juce::String parentPath (const juce::String& path)  { return path.upToLastOccurrenceOf (".", false, false); }
    inline juce::String lastElement (const juce::String& path) { return path.fromLastOccurrenceOf (".", false, false); }

    /** CValue -> juce::var (int / double / String). Null CValue gives a void var. */
    inline juce::var toVar (const CValue* value)
    {
        if (value == nullptr)
            return {};

        switch (value->get_type())
        {
            case CValue::INTEGER:   return (int) *value;
            case CValue::FLOAT:     return (double) (float) *value;
            case CValue::STRING:    return toString ((const std::string&) *value);
        }
        return {};
    }

    /** juce::var -> a newly allocated CValue of the requested type (caller owns). */
    inline std::unique_ptr<CValue> toValue (const juce::var& v, CValue::type type)
    {
        switch (type)
        {
            case CValue::INTEGER:   return std::make_unique<CIntegerValue> (v.isString() ? v.toString().getIntValue() : (int) std::lround ((double) v));
            case CValue::FLOAT:     return std::make_unique<CFloatValue> (v.isString() ? v.toString().getFloatValue() : (float) (double) v);
            case CValue::STRING:    return std::make_unique<CStringValue> (toStd (v.toString()));
        }
        return {};
    }

    /** libIntegra packs string lists as "<length>:<text>" runs (CStringHelper::string_vector_to_string). */
    inline juce::StringArray unpackStringList (const juce::String& packed)
    {
        juce::StringArray result;
        std::string_view text (packed.toRawUTF8());
        size_t pos = 0;
        while (pos < text.size())
        {
            const auto colon = text.find (':', pos);
            if (colon == std::string_view::npos)
                break;
            const auto length = (size_t) std::strtoul (std::string (text.substr (pos, colon - pos)).c_str(), nullptr, 10);
            result.add (juce::String::fromUTF8 (text.data() + colon + 1, (int) juce::jmin (length, text.size() - colon - 1)));
            pos = colon + 1 + length;
        }
        return result;
    }

    inline juce::String packStringList (const juce::StringArray& strings)
    {
        juce::String packed;
        for (auto& s : strings)
            packed << (int) s.getNumBytesAsUTF8() << ":" << s;
        return packed;
    }
}
