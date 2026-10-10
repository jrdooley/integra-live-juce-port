#include "EndpointInfo.h"

namespace integra
{
    EndpointInfo EndpointInfo::fromDefinition (const IEndpointDefinition& def)
    {
        EndpointInfo info;
        info.name = toString (def.get_name());
        info.label = toString (def.get_label());
        info.description = toString (def.get_description());
        if (info.description == "None")
            info.description = {};

        info.isAudioStream = def.is_audio_stream();
        info.isAudioInput = info.isAudioStream && def.get_stream_info()->get_direction() == IStreamInfo::INPUT;

        auto* control = def.get_control_info();
        if (control == nullptr)
            return info;    // audio stream: Kind::missing

        info.canBeSource = control->get_can_be_source();
        info.canBeTarget = control->get_can_be_target();

        if (control->get_type() == IControlInfo::BANG)
        {
            info.kind = Kind::bang;
            return info;
        }

        auto* state = control->get_state_info();
        switch (state->get_type())
        {
            case CValue::INTEGER:   info.kind = Kind::integer; break;
            case CValue::FLOAT:     info.kind = Kind::real; break;
            case CValue::STRING:    info.kind = Kind::text; break;
        }

        info.defaultValue = toVar (&state->get_default_value());

        if (auto* range = state->get_constraint().get_value_range())
        {
            info.hasRange = true;
            info.minimum = (float) range->get_minimum();
            info.maximum = (float) range->get_maximum();
        }

        if (auto* allowed = state->get_constraint().get_allowed_states())
        {
            for (auto* v : *allowed)
                info.allowedStates.add (toVar (v).toString());

            // numeric enumerations sort numerically, strings keep a stable order
            if (info.isNumeric())
                std::sort (info.allowedStates.begin(), info.allowedStates.end(),
                           [] (const juce::String& a, const juce::String& b) { return a.getDoubleValue() < b.getDoubleValue(); });
            else
                info.allowedStates.sort (true);
        }

        for (auto& stateLabel : state->get_state_labels())
            info.stateLabels.emplace_back (toString (stateLabel.first), toVar (stateLabel.second));

        if (auto* scale = state->get_value_scale())
        {
            switch (scale->get_scale_type())
            {
                case IValueScale::EXPONENTIAL: info.scale = Scale::exponential; break;
                case IValueScale::DECIBEL:     info.scale = Scale::decibel; break;
                case IValueScale::LINEAR:      info.scale = Scale::linear; break;
            }
        }

        // guard against definitions the maths can't handle
        if (info.scale == Scale::exponential && (info.minimum <= 0.0 || info.maximum <= info.minimum))
            info.scale = Scale::linear;

        return info;
    }

    EndpointInfo EndpointInfo::find (IntegraSession& session, const juce::String& endpointPath)
    {
        auto server = session.lock();
        if (auto* endpoint = server->find_node_endpoint (toPath (endpointPath)))
            return fromDefinition (endpoint->get_endpoint_definition());
        return {};
    }

    namespace
    {
        double dbToPower (double db)    { return std::pow (10.0, db * 0.1); }
        double powerToDb (double p)     { return 10.0 * std::log10 (juce::jmax (1.0e-12, p)); }
    }

    double EndpointInfo::toUnit (double value) const
    {
        if (! hasRange || maximum <= minimum)
            return 0.0;

        value = juce::jlimit (minimum, maximum, value);
        double unit = 0.0;

        switch (scale)
        {
            case Scale::linear:
                unit = (value - minimum) / (maximum - minimum);
                break;

            case Scale::exponential:
                unit = (std::log2 (value) - std::log2 (minimum)) / (std::log2 (maximum) - std::log2 (minimum));
                break;

            case Scale::decibel:
            {
                const double lo = dbToPower (minimum), hi = dbToPower (maximum);
                unit = (dbToPower (value) - lo) / (hi - lo);
                break;
            }
        }

        return juce::jlimit (0.0, 1.0, unit);
    }

    double EndpointInfo::fromUnit (double unit) const
    {
        if (! hasRange)
            return unit;

        unit = juce::jlimit (0.0, 1.0, unit);
        double value = minimum;

        switch (scale)
        {
            case Scale::linear:
                value = minimum + unit * (maximum - minimum);
                break;

            case Scale::exponential:
                value = std::exp2 (std::log2 (minimum) + unit * (std::log2 (maximum) - std::log2 (minimum)));
                break;

            case Scale::decibel:
            {
                const double lo = dbToPower (minimum), hi = dbToPower (maximum);
                value = powerToDb (lo + unit * (hi - lo));
                break;
            }
        }

        value = juce::jlimit (minimum, maximum, value);
        if (kind == Kind::integer)
            value = std::round (value);
        return value;
    }

    juce::String EndpointInfo::format (const juce::var& value) const
    {
        for (auto& [stateLabel, v] : stateLabels)
            if (v.toString() == value.toString())
                return stateLabel;

        switch (kind)
        {
            case Kind::integer: return juce::String ((int) value);
            case Kind::real:
            {
                const double d = value;
                const double magnitude = std::abs (d);
                const int decimals = magnitude >= 100.0 ? 1 : magnitude >= 10.0 ? 2 : 3;
                return juce::String (d, decimals);
            }
            case Kind::text:
            case Kind::bang:
            case Kind::missing: return value.toString();
        }
        return value.toString();
    }
}
