#pragma once

#include "IntegraSession.h"

namespace integra
{
    /**
        Everything a control needs to know about one control endpoint, copied out of its
        IEndpointDefinition so it can be used without holding the server lock.
    */
    struct EndpointInfo
    {
        enum class Kind { missing, bang, integer, real, text };
        enum class Scale { linear, exponential, decibel };

        Kind kind = Kind::missing;
        Scale scale = Scale::linear;
        juce::String name, label, description;
        bool hasRange = false;
        double minimum = 0.0, maximum = 1.0;
        juce::var defaultValue;
        juce::StringArray allowedStates;                    // for enumerations
        std::vector<std::pair<juce::String, juce::var>> stateLabels;
        bool canBeSource = false, canBeTarget = false;
        bool isInputFile = false;

        bool isNumeric() const noexcept     { return kind == Kind::integer || kind == Kind::real; }

        /** Reads the definition of endpoint `endpointName` of module definition `def`. */
        static EndpointInfo fromDefinition (const IEndpointDefinition&);
        static EndpointInfo find (IntegraSession&, const juce::String& endpointPath);

        /** Endpoint value <-> 0..1 control position, honouring the endpoint's value scale.
            Same maths as the Flex GUI's ControlScaler.as. */
        double toUnit (double value) const;
        double fromUnit (double unit) const;

        /** Formats a value for display (integers without decimals, floats to 3 significant places). */
        juce::String format (const juce::var& value) const;
    };
}
