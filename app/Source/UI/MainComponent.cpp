#include "MainComponent.h"
#include "ModuleManager.h"
#include "PreferencesPanel.h"
#include "IntegraLookAndFeel.h"

namespace integra
{
    MainComponent::MainComponent (AppContext& c)
        : context (c), ribbon (c), arrange (c), properties (c), moduleView (c), liveView (c)
    {
        addAndMakeVisible (ribbon);
        addAndMakeVisible (arrange);
        addAndMakeVisible (properties);
        addChildComponent (moduleView);
        addChildComponent (liveView);

        arrangeLayout.setItemLayout (0, 150, -1.0, -0.62);
        arrangeLayout.setItemLayout (1, 6, 6, 6);
        arrangeLayout.setItemLayout (2, 100, -0.8, -0.38);
        arrangeDivider = std::make_unique<juce::StretchableLayoutResizerBar> (&arrangeLayout, 1, false);
        addAndMakeVisible (*arrangeDivider);

        ribbon.onViewRequested = [this] (const juce::String& v) { showView (v); };
        ribbon.onPreferences = [this] { commands.invokeDirectly (cmdPreferences, true); };
        arrange.onOpenBlock = [this] (const juce::String& block) { openBlock (block); };
        arrange.onSelectionChanged = [this] { properties.selectionChanged(); };
        moduleView.onBack = [this] { showView ("arrange"); };

        commands.registerAllCommandsForTarget (this);
        commands.setFirstCommandTarget (this);
        addKeyListener (commands.getKeyMappings());
        setWantsKeyboardFocus (true);

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (this);
       #else
        menuBar = std::make_unique<juce::MenuBarComponent> (this);
        addAndMakeVisible (*menuBar);
       #endif

        context.model.addChangeListener (this);
        context.controller.getUndoManager().addChangeListener (this);

        changeListenerCallback (nullptr);
        showView (context.model.get().userData.viewMode == "live" ? "live" : "arrange");
        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #endif
        context.controller.getUndoManager().removeChangeListener (this);
        context.model.removeChangeListener (this);
    }

    //==============================================================================
    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().background);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        if (menuBar != nullptr)
            menuBar->setBounds (area.removeFromTop (24));
        ribbon.setBounds (area.removeFromTop (48));

        moduleView.setBounds (area);
        liveView.setBounds (area);

        juce::Component* comps[] = { &arrange, arrangeDivider.get(), &properties };
        arrangeLayout.layOutComponents (comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);

        if (progress != nullptr)
            progress->setBounds (getLocalBounds());
    }

    void MainComponent::showView (const juce::String& name)
    {
        auto view = name;
        if (view == "module" && moduleView.getBlock().isEmpty())
        {
            if (context.model.selectedBlock.isEmpty())
                view = "arrange";
            else
                moduleView.setBlock (context.model.selectedBlock);
        }

        currentView = view;
        const bool isArrange = view == "arrange";
        arrange.setVisible (isArrange);
        arrangeDivider->setVisible (isArrange);
        properties.setVisible (isArrange);
        moduleView.setVisible (view == "module");
        liveView.setVisible (view == "live");
        ribbon.setCurrentView (view);

        if (view == "live")
            liveView.grabKeyboardFocus();
        else if (isArrange)
            arrange.getLanes().grabKeyboardFocus();

        // Flex stored only arrange/live in the project
        const auto stored = view == "live" ? "live" : "arrange";
        if (context.model.get().userData.viewMode != stored)
            context.controller.updateProjectUserData ([stored] (ProjectUserData& d) { d.viewMode = stored; });

        commands.commandStatusChanged();
    }

    void MainComponent::openBlock (const juce::String& blockPath)
    {
        context.model.selectedBlock = blockPath;
        context.model.selectedTrack = parentPath (blockPath);
        moduleView.setBlock (blockPath);
        showView ("module");
    }

    //==============================================================================
    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto& p = context.model.get();
        const auto scheme = p.userData.colorScheme;
        if (scheme != appliedScheme)
        {
            appliedScheme = scheme;
            applyColourScheme (scheme == "dark", false);
        }

        if (currentView == "module" && p.findBlock (moduleView.getBlock()) == nullptr)
            showView ("arrange");

        updateTitle();
        commands.commandStatusChanged();
    }

    void MainComponent::applyColourScheme (bool dark, bool storeInProject)
    {
        Theme::setDark (dark);
        if (auto* lf = dynamic_cast<IntegraLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
            lf->applyTheme();

        if (storeInProject)
        {
            appliedScheme = dark ? "dark" : "light";
            context.controller.updateProjectUserData ([dark] (ProjectUserData& d) { d.colorScheme = dark ? "dark" : "light"; });
        }

        if (auto* top = getTopLevelComponent())
        {
            top->sendLookAndFeelChange();
            top->repaint();
        }
    }

    void MainComponent::updateTitle()
    {
        auto file = context.controller.getProjectFile();
        auto title = (file == juce::File() ? juce::String ("Untitled") : file.getFileNameWithoutExtension())
                   + (context.controller.hasUnsavedChanges() ? " *" : "") + " - Integra Live";
        if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
            window->setName (title);
    }

    void MainComponent::showError (const juce::String& title, const juce::Result& r)
    {
        if (r.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, r.getErrorMessage());
    }

    //==============================================================================
    void MainComponent::withUnsavedChangesCheck (std::function<void()> proceed)
    {
        if (! context.controller.hasUnsavedChanges())
        {
            proceed();
            return;
        }

        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Unsaved changes")
                           .withMessage ("Save changes to the current project?")
                           .withButton ("Save")
                           .withButton ("Don't save")
                           .withButton ("Cancel")
                           .withAssociatedComponent (this);

        juce::AlertWindow::showAsync (options, [this, proceed] (int result)
        {
            if (result == 1)
                save ([proceed] (bool saved) { if (saved) proceed(); });
            else if (result == 2)
                proceed();
        });
    }

    void MainComponent::save (std::function<void (bool)> done)
    {
        auto file = context.controller.getProjectFile();
        if (file == juce::File())
        {
            saveAs (done);
            return;
        }

        auto r = context.controller.saveProject (file);
        showError ("Save", r);
        updateTitle();
        if (done) done (r.wasOk());
    }

    void MainComponent::saveAs (std::function<void (bool)> done)
    {
        // IntegraLive.saveAs: the current file (an .ixd becomes .integra), else the project's name
        auto start = context.controller.getProjectFile();
        if (start == juce::File())
            start = getDialogFolder().getChildFile (context.model.get().path + ".integra");
        else
            start = start.withFileExtension ("integra");

        chooser = std::make_unique<juce::FileChooser> ("Save project", start, "*.integra");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, done] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
            {
                if (done) done (false);
                return;
            }

            // the project takes the file's name (onSelectFileToSave)
            auto r = context.controller.saveProjectAs (file);
            showError ("Save", r);
            rememberFolder (file);
            if (currentView == "module")
                moduleView.setBlock (context.model.selectedBlock);
            properties.selectionChanged();
            updateTitle();
            if (done) done (r.wasOk());
        });
    }

    void MainComponent::openFile (const juce::File& file)
    {
        if (file.hasFileExtension ("module;bundle"))
        {
            installFiles ({ file });
            return;
        }
        withUnsavedChangesCheck ([this, file] { loadProject (file); });
    }

    void MainComponent::loadProject (const juce::File& file)
    {
        rememberFolder (file);
        runWithProgressMessage ("Loading: " + file.getFileName() + "...", [this, file]
        {
            projectLoaded (context.controller.openProject (file));
        });
    }

    void MainComponent::projectLoaded (const juce::Result& r)
    {
        showError ("Open", r);
        showView (context.model.get().userData.viewMode == "live" ? "live" : "arrange");
        properties.selectionChanged();
        updateTitle();
        if (r.wasOk())
            projectOpened();
    }

    void MainComponent::installFiles (const juce::Array<juce::File>& files)
    {
        // InstallModules: .module files, and the modules inside .bundle files
        juce::StringArray installed;
        auto r = context.controller.installModules (files, installed);
        modulesChanged();
        if (! installed.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Install modules",
                                                    "Installed and available in the module library:\n" + installed.joinIntoString ("\n")
                                                        + (r.failed() ? "\n\nNot installed:\n" + r.getErrorMessage() : juce::String()));
        else
            showError ("Install modules", r);
        if (! installed.isEmpty())
            ModuleManager::checkForUpgrades (context, [this] { modulesChanged(); });
    }

    namespace
    {
        /** "Loading: name..." over the window while something blocks (Flex: ProgressPopup). */
        class ProgressMessage  : public juce::Component
        {
        public:
            explicit ProgressMessage (const juce::String& m) : message (m)  { setInterceptsMouseClicks (true, true); }

            void paint (juce::Graphics& g) override
            {
                auto& t = Theme::get();
                g.fillAll (juce::Colours::black.withAlpha (0.25f));
                auto box = getLocalBounds().withSizeKeepingCentre (juce::jmin (420, getWidth() - 40), 70).toFloat();
                g.setColour (t.panel);
                g.fillRoundedRectangle (box, 8.0f);
                g.setColour (t.outline);
                g.drawRoundedRectangle (box, 8.0f, 1.0f);
                g.setColour (t.text);
                g.setFont (juce::FontOptions (15.0f));
                g.drawFittedText (message, box.reduced (16.0f).toNearestInt(), juce::Justification::centred, 2);
            }

            juce::String message;
        };
    }

    void MainComponent::runWithProgressMessage (const juce::String& message, std::function<void()> work)
    {
        // show the message first: loading blocks the message thread
        progress = std::make_unique<ProgressMessage> (message);
        addAndMakeVisible (*progress);
        progress->setBounds (getLocalBounds());
        progress->repaint();
        juce::Timer::callAfterDelay (40, [safe = juce::Component::SafePointer<MainComponent> (this), work]
        {
            if (safe == nullptr)
                return;
            work();
            safe->progress.reset();
        });
    }

    juce::File MainComponent::getDialogFolder() const
    {
        // IntegraLive.doLoad: start where the current project is
        auto file = context.controller.getProjectFile();
        if (file.existsAsFile())
            return file.getParentDirectory();
        if (auto* settings = context.properties.getUserSettings())
        {
            juce::File last (settings->getValue ("lastProjectFolder"));
            if (last.isDirectory())
                return last;
        }
        auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        return documents.isDirectory() ? documents : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    }

    void MainComponent::rememberFolder (const juce::File& file)
    {
        if (auto* settings = context.properties.getUserSettings())
            settings->setValue ("lastProjectFolder", file.getParentDirectory().getFullPathName());
    }

    void MainComponent::requestQuit (std::function<void()> proceed)
    {
        withUnsavedChangesCheck (std::move (proceed));
    }

    //==============================================================================
    void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
    {
        ids.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdImportBlock, cmdExportBlock,
                        cmdUndo, cmdRedo, cmdDelete, cmdAddTrack, cmdAddScene,
                        cmdPlayPause, cmdHome, cmdNextScene, cmdPrevScene,
                        cmdViewArrange, cmdViewLive, cmdViewModule, cmdPreferences, cmdAbout, cmdDemo, cmdInstallModule, cmdModuleManager });
    }

    void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
    {
        auto& undo = context.controller.getUndoManager();
        const auto cmd = juce::ModifierKeys::commandModifier;
        const auto shift = juce::ModifierKeys::shiftModifier;

        switch (id)
        {
            case cmdNew:          info.setInfo ("New Project", "", "File", 0);              info.addDefaultKeypress ('n', cmd); break;
            case cmdOpen:         info.setInfo ("Open Project...", "", "File", 0);          info.addDefaultKeypress ('o', cmd); break;
            case cmdSave:         info.setInfo ("Save Project", "", "File", 0);             info.addDefaultKeypress ('s', cmd); break;
            case cmdSaveAs:       info.setInfo ("Save Project As...", "", "File", 0);       info.addDefaultKeypress ('s', cmd | shift); break;
            case cmdImportBlock:  info.setInfo ("Import Block...", "", "File", 0);
                                  info.setActive (context.model.get().findTrack (context.model.selectedTrack) != nullptr || ! context.model.get().tracks.empty()); break;
            case cmdExportBlock:  info.setInfo ("Export Block...", "", "File", 0);
                                  info.setActive (context.model.get().findBlock (context.model.selectedBlock) != nullptr); break;
            case cmdUndo:         info.setInfo (undo.canUndo() ? "Undo " + undo.getUndoDescription() : "Undo", "", "Edit", 0);
                                  info.addDefaultKeypress ('z', cmd); info.setActive (undo.canUndo()); break;
            case cmdRedo:         info.setInfo (undo.canRedo() ? "Redo " + undo.getRedoDescription() : "Redo", "", "Edit", 0);
                                  info.addDefaultKeypress ('z', cmd | shift); info.setActive (undo.canRedo()); break;
            case cmdDelete:       info.setInfo ("Delete", "", "Edit", 0); break;
            case cmdAddTrack:     info.setInfo ("Add Track", "", "Edit", 0);                info.addDefaultKeypress ('t', cmd); break;
            case cmdAddScene:     info.setInfo ("Add Scene At Playhead", "", "Edit", 0); break;
            case cmdPlayPause:    info.setInfo ("Play / Pause", "", "Transport", 0);        info.addDefaultKeypress (juce::KeyPress::spaceKey, 0); break;
            case cmdHome:         info.setInfo ("Return To Start", "", "Transport", 0);     info.addDefaultKeypress (juce::KeyPress::homeKey, 0); break;
            case cmdNextScene:    info.setInfo ("Next Scene", "", "Transport", 0);          info.addDefaultKeypress (juce::KeyPress::rightKey, cmd); break;
            case cmdPrevScene:    info.setInfo ("Previous Scene", "", "Transport", 0);      info.addDefaultKeypress (juce::KeyPress::leftKey, cmd); break;
            case cmdViewArrange:  info.setInfo ("Arrange View", "", "View", 0);             info.addDefaultKeypress ('1', cmd);
                                  info.setTicked (currentView == "arrange"); break;
            case cmdViewLive:     info.setInfo ("Live View", "", "View", 0);                info.addDefaultKeypress ('2', cmd);
                                  info.setTicked (currentView == "live"); break;
            case cmdViewModule:   info.setInfo ("Module View (Selected Block)", "", "View", 0); info.addDefaultKeypress ('3', cmd);
                                  info.setTicked (currentView == "module");
                                  info.setActive (context.model.get().findBlock (context.model.selectedBlock) != nullptr); break;
            case cmdPreferences:  info.setInfo ("Settings...", "", "View", 0);              info.addDefaultKeypress (',', cmd); break;
            case cmdAbout:        info.setInfo ("About Integra Live", "", "Help", 0); break;
            case cmdDemo:         info.setInfo ("Open Demo Project", "", "Help", 0); break;
            case cmdInstallModule: info.setInfo ("Install Third-Party Module...", "", "File", 0); break;
            case cmdModuleManager: info.setInfo ("Module Manager...", "", "View", 0); info.addDefaultKeypress ('4', cmd); break;
            default: break;
        }
    }

    bool MainComponent::perform (const InvocationInfo& invocation)
    {
        switch (invocation.commandID)
        {
            case cmdNew:
                withUnsavedChangesCheck ([this]
                {
                    runWithProgressMessage ("Starting new Project...", [this]
                    {
                        showError ("New project", context.controller.newProject());
                        showView ("arrange");
                        properties.selectionChanged();
                        updateTitle();
                    });
                });
                return true;

            case cmdOpen:
                withUnsavedChangesCheck ([this]
                {
                    // IntegraLive.doLoad: projects (.integra) and bare .ixd files, from the project's folder
                    chooser = std::make_unique<juce::FileChooser> ("Open project", getDialogFolder(), "*.integra;*.ixd");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                    {
                        if (fc.getResult() != juce::File())
                            loadProject (fc.getResult());
                    });
                });
                return true;

            case cmdSave:    save();   return true;
            case cmdSaveAs:  saveAs(); return true;

            case cmdImportBlock:
            {
                auto track = context.model.selectedTrack;
                if (context.model.get().findTrack (track) == nullptr && ! context.model.get().tracks.empty())
                    track = context.model.get().tracks.front().path;

                chooser = std::make_unique<juce::FileChooser> ("Import block", context.session.getLocations().blockLibrary, "*.integra");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, track] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() == juce::File())
                        return;
                    int start = 0;
                    if (auto* t = context.model.get().findTrack (track))
                        for (auto& b : t->blocks)
                            start = juce::jmax (start, b.end);
                    showError ("Import block", context.controller.importBlock (fc.getResult(), track, start));
                });
                return true;
            }

            case cmdExportBlock:
            {
                const auto block = context.model.selectedBlock;
                chooser = std::make_unique<juce::FileChooser> ("Export block",
                    context.session.getLocations().userBlockLibrary.getChildFile (lastElement (block) + ".integra"), "*.integra");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, block] (const juce::FileChooser& fc)
                {
                    if (fc.getResult() != juce::File())
                        showError ("Export block", context.controller.exportNode (block, fc.getResult()));
                });
                return true;
            }

            case cmdUndo:   context.controller.getUndoManager().undo(); context.model.rebuild(); return true;
            case cmdRedo:   context.controller.getUndoManager().redo(); context.model.rebuild(); return true;

            case cmdDelete:
                if (currentView == "module")
                    moduleView.getGraph().deleteSelection();
                else if (currentView == "arrange")
                    arrange.deleteSelection();
                return true;

            case cmdAddTrack:
                context.controller.addTrack();
                properties.selectionChanged();
                return true;

            case cmdAddScene:
            {
                const int rate = juce::jmax (1, context.model.get().player.rate);
                context.controller.addScene (context.controller.getPlayPosition(), rate * 10);
                return true;
            }

            case cmdPlayPause:  context.controller.play (! context.controller.isPlaying()); return true;
            case cmdHome:       context.controller.returnToStart(); return true;
            case cmdNextScene:  context.controller.nextScene(); return true;
            case cmdPrevScene:  context.controller.previousScene(); return true;

            case cmdViewArrange: showView ("arrange"); return true;
            case cmdViewLive:    showView ("live"); return true;
            case cmdViewModule:  openBlock (context.model.selectedBlock); return true;

            case cmdPreferences:
                PreferencesPanel::show (context, [this] (bool dark) { applyColourScheme (dark, true); });
                return true;

            case cmdInstallModule:
                // InstallModules: "Modules and Module Bundles", several at once
                chooser = std::make_unique<juce::FileChooser> ("Install modules", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.module;*.bundle");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc)
                {
                    if (! fc.getResults().isEmpty())
                        installFiles (fc.getResults());
                });
                return true;

            case cmdModuleManager:
                ModuleManager::show (context, [this] { modulesChanged(); });
                return true;

            case cmdDemo:
                withUnsavedChangesCheck ([this]
                {
                    runWithProgressMessage ("Loading the demo project...", [this]
                    {
                        showError ("Demo project", context.controller.newDemoProject());
                        showView ("arrange");
                        properties.selectionChanged();
                        updateTitle();
                    });
                });
                return true;

            case cmdAbout:
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Integra Live",
                    "Integra Live " + juce::String (JUCE_APPLICATION_VERSION_STRING) + "\n\n"
                    "JUCE port of Integra Live (Birmingham Conservatoire).\n"
                    "Audio processing: libIntegra " + toString (context.session.lock()->get_libintegra_version())
                    + " with libpd (Pure Data 0.43).\n\n"
                    "libIntegra and the JUCE front end are free software under the GNU GPL.");
                return true;

            default:
                return false;
        }
    }

    //==============================================================================
    juce::StringArray MainComponent::getMenuBarNames()
    {
        return { "File", "Edit", "Transport", "View", "Help" };
    }

    juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
    {
        juce::PopupMenu menu;
        switch (index)
        {
            case 0:
                menu.addCommandItem (&commands, cmdNew);
                menu.addCommandItem (&commands, cmdOpen);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdSave);
                menu.addCommandItem (&commands, cmdSaveAs);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdImportBlock);
                menu.addCommandItem (&commands, cmdExportBlock);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdInstallModule);
               #if ! JUCE_MAC
                menu.addSeparator();
                menu.addItem ("Quit", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
               #endif
                break;
            case 1:
                menu.addCommandItem (&commands, cmdUndo);
                menu.addCommandItem (&commands, cmdRedo);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdDelete);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdAddTrack);
                menu.addCommandItem (&commands, cmdAddScene);
                break;
            case 2:
                menu.addCommandItem (&commands, cmdPlayPause);
                menu.addCommandItem (&commands, cmdHome);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdPrevScene);
                menu.addCommandItem (&commands, cmdNextScene);
                break;
            case 3:
                menu.addCommandItem (&commands, cmdViewArrange);
                menu.addCommandItem (&commands, cmdViewLive);
                menu.addCommandItem (&commands, cmdViewModule);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdModuleManager);
                menu.addCommandItem (&commands, cmdPreferences);
                break;
            case 4:
                menu.addCommandItem (&commands, cmdDemo);
                menu.addSeparator();
                menu.addCommandItem (&commands, cmdAbout);
                break;
            default:
                break;
        }
        return menu;
    }

    void MainComponent::modulesChanged()
    {
        moduleView.getLibrary().refresh();
        properties.selectionChanged();
        repaint();
    }

    void MainComponent::projectOpened()
    {
        // IntegraController: PollForUpgradableModules after loading a project
        ModuleManager::checkForUpgrades (context, [this] { modulesChanged(); });
    }
}
