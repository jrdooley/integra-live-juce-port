#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "RibbonBar.h"
#include "ArrangeView.h"
#include "ModuleView.h"
#include "LiveView.h"
#include "PropertiesPanel.h"

namespace integra
{
    /**
        The main window content (Flex: IntegraLive.mxml + MainViewContainer + MenuHandlers):
        ribbon bar, then either Arrange view over the properties panel, the Module View
        of an opened block, or the Live view. Owns the menu bar and keyboard commands.
    */
    class MainComponent  : public juce::Component,
                           public juce::ApplicationCommandTarget,
                           public juce::MenuBarModel,
                           private juce::ChangeListener
    {
    public:
        explicit MainComponent (AppContext&);
        ~MainComponent() override;

        void resized() override;
        void paint (juce::Graphics&) override;

        /** "arrange", "live" or "module" (module view of the selected block). */
        void showView (const juce::String& name);
        void openBlock (const juce::String& blockPath);

        void openFile (const juce::File&);
        /** Asks to save unsaved changes, then calls proceed (unless cancelled). */
        void requestQuit (std::function<void()> proceed);

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;

        // MenuBarModel
        juce::StringArray getMenuBarNames() override;
        juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
        void menuItemSelected (int, int) override {}

        ArrangeView& getArrangeView() noexcept      { return arrange; }
        ModuleView& getModuleView() noexcept        { return moduleView; }
        LiveView& getLiveView() noexcept            { return liveView; }
        RibbonBar& getRibbon() noexcept            { return ribbon; }
        PropertiesPanel& getProperties() noexcept   { return properties; }
        const juce::String& getCurrentView() const noexcept { return currentView; }
        bool invokeCommand (int commandID)          { return commands.invokeDirectly (commandID, false); }

        enum Commands
        {
            cmdNew = 1, cmdOpen, cmdSave, cmdSaveAs, cmdImportBlock, cmdExportBlock,
            cmdUndo, cmdRedo, cmdDelete, cmdAddTrack, cmdAddScene,
            cmdPlayPause, cmdHome, cmdNextScene, cmdPrevScene,
            cmdViewArrange, cmdViewLive, cmdViewModule, cmdPreferences, cmdAbout, cmdDemo, cmdInstallModule, cmdModuleManager
        };

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateTitle();
        void applyColourScheme (bool dark, bool storeInProject);
        void withUnsavedChangesCheck (std::function<void()> proceed);
        void saveAs (std::function<void (bool saved)> done = {});
        void save (std::function<void (bool saved)> done = {});
        void showError (const juce::String& title, const juce::Result&);
        void modulesChanged();
        void projectOpened();

        AppContext& context;
        juce::ApplicationCommandManager commands;
        std::unique_ptr<juce::MenuBarComponent> menuBar;
        RibbonBar ribbon;
        ArrangeView arrange;
        PropertiesPanel properties;
        ModuleView moduleView;
        LiveView liveView;
        juce::StretchableLayoutManager arrangeLayout;
        std::unique_ptr<juce::StretchableLayoutResizerBar> arrangeDivider;
        juce::TooltipWindow tooltips { this, 600 };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String currentView = "arrange";
        juce::String appliedScheme;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
