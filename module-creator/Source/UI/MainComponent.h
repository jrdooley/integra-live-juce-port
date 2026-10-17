#pragma once

#include "Common.h"
#include "Core/PdHost.h"
#include <juce_data_structures/juce_data_structures.h>

namespace modulecreator
{
    class InterfaceInfoPage;
    class EndpointsPage;
    class WidgetLayoutPage;
    class ImplementationPage;

    /**
        The Module Creator's window content (ModuleCreator.mxml): the four tabs, the menus,
        file handling, "Test in Integra Live", module bundles, and the licence and about boxes.
    */
    class MainComponent  : public juce::Component,
                           public juce::ApplicationCommandTarget,
                           public juce::MenuBarModel,
                           private juce::ChangeListener
    {
    public:
        MainComponent (const CreatorConfig&, juce::ApplicationProperties&, bool developerMode);
        ~MainComponent() override;

        enum CommandIDs
        {
            fileNew = 0x2001, fileOpen, fileSave, fileSaveAs, fileBrowseTemplate,
            testInIntegraLive, locateIntegraLive, createBundle, extractBundle,
            editInPdCommand, licenseCommand, aboutCommand
        };

        juce::ApplicationCommandManager& getCommandManager()    { return commands; }
        Document& getDocument()                                  { return document; }
        juce::TabbedComponent& getTabs()                         { return tabs; }
        InterfaceInfoPage& getInterfaceInfoPage()                { return *interfaceInfo; }
        EndpointsPage& getEndpointsPage()                        { return *endpoints; }
        WidgetLayoutPage& getWidgetLayoutPage()                  { return *widgetLayout; }
        ImplementationPage& getImplementationPage()              { return *implementation; }

        /** Opens a module given on the command line or dropped on the app. */
        void openFile (const juce::File&);
        /** Asks to save a modified module, then runs `action` unless cancelled (saveModified). */
        void withUnsavedChangesCheck (const juce::String& caption, std::function<void()> action);
        void requestQuit (std::function<void()> quit);
        /** Validates, and shows the problems on the first page that has any (doValidation). */
        bool validateForSave (const juce::String& caption);
        void showLicenseOnStartupIfNeeded (std::function<void()> declined);

        juce::File findIntegraLive() const;

        void resized() override;
        void paint (juce::Graphics&) override;

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override  { return juce::JUCEApplication::getInstance(); }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;

        // MenuBarModel
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
        void menuItemSelected (int, int) override;

        std::function<void (const juce::String&)> onTitleChanged;

    private:
        const CreatorConfig& config;
        juce::ApplicationProperties& properties;
        Document document;
        PdHost host;
        juce::ApplicationCommandManager commands;
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        InterfaceInfoPage* interfaceInfo = nullptr;
        EndpointsPage* endpoints = nullptr;
        WidgetLayoutPage* widgetLayout = nullptr;
        ImplementationPage* implementation = nullptr;
        juce::Label status;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::TooltipWindow tooltips { this, 500 };
        juce::Component::SafePointer<juce::DialogWindow> licenseWindow, aboutWindow;
        juce::Array<juce::File> testDirectories;
        bool showingSavePrompt = false;

        juce::PropertySet& settings()               { return *properties.getUserSettings(); }
        juce::File rememberedFolder (const juce::String& key) const;
        void rememberFolder (const juce::String& key, const juce::File&);

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateStatus();

        void open();
        void loadFile (const juce::File&);
        void openTemplate (const juce::File&);
        void browseForTemplate();
        void save (std::function<void()> afterSave = {});
        void saveAs (std::function<void()> afterSave = {});
        void saveTo (const juce::File&, std::function<void()> afterSave);
        /** Runs `action` after checking validity and that Pd isn't editing the implementation. */
        void withValidModule (const juce::String& caption, std::function<void()> action);
        void testInIntegraLiveNow();
        void chooseIntegraLive (std::function<void()> then = {});
        void createModuleBundle();
        void extractModuleBundle();
        void addTemplateMenus (juce::PopupMenu&, const juce::File& directory, std::vector<juce::File>& files);
        void openHelpLink (const juce::String& link);
        void showLicense (bool forAcceptance, std::function<void (bool accepted)> done = {});
        void showAbout();

        std::vector<juce::File> templateMenuFiles;
        static constexpr int templateMenuBase = 0x3000;
        static constexpr int helpMenuBase = 0x4000;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
