#pragma once

#include "SavedDivider.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "Core/AppContext.h"
#include "RibbonBar.h"
#include "ArrangeView.h"
#include "ModuleView.h"
#include "LiveView.h"
#include "PropertiesPanel.h"
#include "InfoView.h"

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

        /** Opens a project (.integra / .ixd), or installs modules (.module / .bundle), as
            the Flex GUI did for files it was asked to open (doAutoloadIfNecessary). */
        void openFile (const juce::File&);
        /** Loads a project behind a "Loading..." message (no unsaved-changes check). */
        void loadProject (const juce::File&);
        /** Installs .module and .bundle files and reports what was installed. */
        void installFiles (const juce::Array<juce::File>&);
        /** Runs work after showing a progress message over the window (Flex: displayProgressPopup). */
        void runWithProgressMessage (const juce::String& message, std::function<void()> work);
        bool isShowingProgressMessage() const noexcept      { return progress != nullptr && progress->isVisible(); }
        /** Where file dialogs start: the current project's folder, else the last one used. */
        juce::File getDialogFolder() const;
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
        InfoView& getInfoView() noexcept            { return infoView; }
        /** View > Info View (Flex: Ctrl+3; the shown state is remembered). */
        void setInfoViewShown (bool);
        const juce::String& getCurrentView() const noexcept { return currentView; }
        bool invokeCommand (int commandID)          { return commands.invokeDirectly (commandID, false); }

        enum Commands
        {
            cmdNew = 1, cmdOpen, cmdSave, cmdSaveAs, cmdImportBlock, cmdExportBlock,
            cmdUndo, cmdRedo, cmdDelete, cmdAddTrack, cmdAddScene,
            cmdPlayPause, cmdHome, cmdNextScene, cmdPrevScene,
            cmdViewArrange, cmdViewLive, cmdViewModule, cmdPreferences, cmdAbout, cmdDemo, cmdInstallModule, cmdModuleManager,
            cmdInfoView, cmdInfoLock, cmdLighting, cmdHighContrast, cmdTextLarger, cmdTextSmaller, cmdTextReset, cmdHelp,
            cmdImportTrack, cmdExportTrack, cmdCreateBlock, cmdDeleteTrack, cmdModuleDocs
        };

        /** View > Text Size (Flex FontSize: 7 / 9 / 11 / 15 / 20 pt). The port scales the
            whole window, as its controls are laid out in pixels. */
        static constexpr float textScales[] = { 0.75f, 0.875f, 1.0f, 1.25f, 1.5f };
        int getTextSizeIndex() const noexcept       { return textSizeIndex; }
        void setTextSizeIndex (int);

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
        InfoView infoView { context };
        void layoutInfoView();
        juce::StretchableLayoutManager arrangeLayout;
        std::unique_ptr<SavedDivider> arrangeDivider;
        bool propertiesHeightRestored = false;
        juce::TooltipWindow tooltips { this, 600 };
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::Component> progress;
        void rememberFolder (const juce::File&);
        void projectLoaded (const juce::Result&);
        juce::String currentView = "arrange";
        juce::String appliedScheme;
        int appliedContrast = -1;
        int textSizeIndex = 2;
        void applyHighContrast (bool on, bool storeInProject);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
