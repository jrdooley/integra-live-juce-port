#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"

namespace integra
{
    /**
        Module Manager (Flex: views/ModuleManager/). Two tabs:

        * Install / Uninstall - installed 3rd party modules (uninstall), modules embedded
          in the project (install to disk), install from a .module file, and an info panel
          for the selected module.
        * Manage Versions - modules in the project that have other versions loaded, their
          alternative versions, a report of what switching would change, Switch Versions,
          Upgrade All and the Always Upgrade setting.

        Lists hold module GUIDs, not catalogue entries: the catalogue is rebuilt whenever
        modules are installed or uninstalled.
    */
    class ModuleManager  : public juce::Component
    {
    public:
        ModuleManager (AppContext&, std::function<void()> onModulesChanged);
        ~ModuleManager() override;

        void resized() override;
        void paint (juce::Graphics&) override;

        enum class Tab { install = 0, versions = 1 };
        void showTab (Tab);

        /** Opens the Module Manager window (one at a time). */
        static void show (AppContext&, std::function<void()> onModulesChanged, Tab = Tab::install);

        /** "Always Upgrade" (Flex stored it with the app's persistent state). */
        static bool isAlwaysUpgrade (AppContext&);
        static void setAlwaysUpgrade (AppContext&, bool);

        /** The upgrade report shown after Upgrade All (ModuleManagementInfoGenerator.getUpgradeReport). */
        static juce::String describeUpgrade (AppContext&, const ProjectController::UpgradeResult&);

        /** Shows a scrollable Markdown report in its own window. */
        static void showReport (const juce::String& title, const juce::String& markdown);

        /** After loading a project or installing modules (PollForUpgradableModules): if any
            module isn't its latest official version, upgrade straight away with Always
            Upgrade set, otherwise offer the Upgrade dialog. */
        static void checkForUpgrades (AppContext&, std::function<void()> onModulesChanged);

        class InstallTab;
        class VersionsTab;

    private:
        AppContext& context;
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        InstallTab* installTab = nullptr;
        VersionsTab* versionsTab = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleManager)
    };
}
