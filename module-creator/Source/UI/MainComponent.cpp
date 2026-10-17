#include "MainComponent.h"
#include "EndpointsPage.h"
#include "ImplementationPage.h"
#include "InterfaceInfoPage.h"
#include "MessageBox.h"
#include "WidgetLayoutPage.h"
#include "Core/ModuleFile.h"

namespace modulecreator
{
    namespace
    {
        juce::File defaultWorkingDirectoryParent()
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);     // DirectoryStore's default
        }

        juce::String pageName (ValidationIssue::Page page)
        {
            switch (page)
            {
                case ValidationIssue::Page::interfaceInfo:  return "Interface Info";
                case ValidationIssue::Page::endpoints:      return "Endpoints";
                case ValidationIssue::Page::widgetLayout:   return "Widget Layout";
                case ValidationIssue::Page::implementation: return "Implementation";
            }
            return {};
        }

        /** A scrolling Markdown text with optional Accept / Decline buttons (LicenseDialog, AboutBox). */
        class MarkdownPanel  : public juce::Component
        {
        public:
            MarkdownPanel (const juce::String& markdown, bool withAcceptance, std::function<void (bool)> onDone)
                : done (std::move (onDone))
            {
                text.setMarkdown ({}, markdown);
                viewport.setViewedComponent (&text, false);
                viewport.setScrollBarsShown (true, false);
                addAndMakeVisible (viewport);
                if (withAcceptance)
                {
                    addAndMakeVisible (accept);
                    addAndMakeVisible (decline);
                    accept.onClick = [this] { finish (true); };
                    decline.onClick = [this] { finish (false); };
                }
                else
                {
                    addAndMakeVisible (close);
                    close.onClick = [this] { finish (true); };
                }
                markdownSource = markdown;
                setSize (640, 520);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (10);
                auto buttons = r.removeFromBottom (rowHeight);
                r.removeFromBottom (10);
                if (close.isVisible())
                    close.setBounds (buttons.removeFromRight (100));
                else
                {
                    accept.setBounds (buttons.removeFromRight (120));
                    buttons.removeFromRight (8);
                    decline.setBounds (buttons.removeFromRight (120));
                }
                viewport.setBounds (r);
                const int w = viewport.getMaximumVisibleWidth();
                auto attributed = integra::renderMarkdown (markdownSource, Theme::get().text, Theme::get().accent);
                juce::TextLayout layout;
                layout.createLayout (attributed, (float) w - 16.0f);
                text.setSize (w, (int) layout.getHeight() + 24);
            }

        private:
            MarkdownView text;
            juce::Viewport viewport;
            juce::TextButton accept { "Accept" }, decline { "Decline" }, close { "Close" };
            juce::String markdownSource;
            std::function<void (bool)> done;

            void finish (bool result)
            {
                auto callback = done;
                if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                    w->exitModalState (result ? 1 : 0);
                if (callback)
                    callback (result);
            }
        };
    }

    //==============================================================================
    MainComponent::MainComponent (const CreatorConfig& c, juce::ApplicationProperties& p, bool developerMode)
        : config (c), properties (p),
          document (c, juce::File (p.getUserSettings()->getValue ("workingDirectoryParent", defaultWorkingDirectoryParent().getFullPathName())))
    {
        document.developerMode = document.developerMode || developerMode;

        interfaceInfo = new InterfaceInfoPage (document);
        endpoints = new EndpointsPage (document);
        widgetLayout = new WidgetLayoutPage (document);
        implementation = new ImplementationPage (document, host, settings());
        const auto tabColour = Theme::get().panel;
        tabs.addTab ("Interface Info", tabColour, interfaceInfo, true);
        tabs.addTab ("Endpoints", tabColour, endpoints, true);
        tabs.addTab ("Widget Layout", tabColour, widgetLayout, true);
        tabs.addTab ("Implementation", tabColour, implementation, true);
        tabs.setTabBarDepth (30);
        addAndMakeVisible (tabs);

        status.setFont (juce::FontOptions (12.0f));
        status.setColour (juce::Label::textColourId, Theme::get().textDim);
        addAndMakeVisible (status);

        commands.registerAllCommandsForTarget (this);
        commands.registerAllCommandsForTarget (juce::JUCEApplication::getInstance());   // quit
        commands.setFirstCommandTarget (this);
        addKeyListener (commands.getKeyMappings());
        setWantsKeyboardFocus (true);

        document.addChangeListener (this);
        updateStatus();
        setSize (1100, 720);
    }

    MainComponent::~MainComponent()
    {
        document.removeChangeListener (this);
        host.kill();
        for (auto& d : testDirectories)
            d.deleteRecursively();
        if (licenseWindow != nullptr)  delete licenseWindow.getComponent();
        if (aboutWindow != nullptr)    delete aboutWindow.getComponent();
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::get().background);
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds();
        status.setBounds (r.removeFromBottom (22).reduced (8, 0));
        tabs.setBounds (r.reduced (4));
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateStatus();
    }

    void MainComponent::updateStatus()
    {
        status.setText ("module id: " + document.definition.moduleGuid + "         origin id: " + document.definition.originGuid
                            + (document.developerMode ? "         (libIntegra developer mode)" : ""),
                        juce::dontSendNotification);
        if (onTitleChanged)
            onTitleChanged (document.getTitle());
    }

    juce::File MainComponent::rememberedFolder (const juce::String& key) const
    {
        juce::File f (properties.getUserSettings()->getValue (key, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName()));
        return f.isDirectory() ? f : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    void MainComponent::rememberFolder (const juce::String& key, const juce::File& folder)
    {
        settings().setValue (key, folder.getFullPathName());
    }

    //==============================================================================
    void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
    {
        ids.addArray ({ fileNew, fileOpen, fileSave, fileSaveAs, fileBrowseTemplate, testInIntegraLive, locateIntegraLive,
                        createBundle, extractBundle, editInPdCommand, licenseCommand, aboutCommand });
    }

    void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
    {
        const auto cmd = juce::ModifierKeys::commandModifier;
        switch (id)
        {
            case fileNew:               info.setInfo ("New", "Start a new module", "File", 0); info.addDefaultKeypress ('n', cmd); break;
            case fileOpen:              info.setInfo ("Open...", "Open a module", "File", 0); info.addDefaultKeypress ('o', cmd); break;
            case fileSave:              info.setInfo ("Save", "Save the module", "File", 0); info.addDefaultKeypress ('s', cmd); break;
            case fileSaveAs:            info.setInfo ("Save As...", "Save the module to a new file", "File", 0);
                                        info.addDefaultKeypress ('s', cmd | juce::ModifierKeys::shiftModifier); break;
            case fileBrowseTemplate:    info.setInfo ("Browse...", "Start a new module from any module", "File", 0); break;
            case testInIntegraLive:     info.setInfo ("Test in Integra Live", "Load the module into Integra Live as the in-development module", "Test", 0);
                                        info.addDefaultKeypress ('t', cmd); break;
            case locateIntegraLive:     info.setInfo ("Locate Integra Live...", "Choose the Integra Live application to test modules in", "Test", 0); break;
            case editInPdCommand:       info.setInfo ("Edit Implementation in Pd", "Open the main patch in Pd", "Test", 0);
                                        info.setActive (document.getMainPatch().existsAsFile()); break;
            case createBundle:          info.setInfo ("Create Module Bundle...", "Pack several modules into one .bundle file", "Module Bundles", 0); break;
            case extractBundle:         info.setInfo ("Extract Contents From Module Bundle...", "Unpack the modules of a .bundle file", "Module Bundles", 0); break;
            case licenseCommand:        info.setInfo ("License Agreement", "Show the SDK licence agreement", "Help", 0); break;
            case aboutCommand:          info.setInfo ("About Integra Module Creator...", "About this application", "Help", 0); break;
            default: break;
        }
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        switch (info.commandID)
        {
            case fileNew:               withUnsavedChangesCheck ("Create New Interface", [this] { host.kill(); document.newFile(); tabs.setCurrentTabIndex (0); }); break;
            case fileOpen:              withUnsavedChangesCheck ("Load Interface", [this] { open(); }); break;
            case fileSave:              save(); break;
            case fileSaveAs:            saveAs(); break;
            case fileBrowseTemplate:    withUnsavedChangesCheck ("Import Template Interface", [this] { browseForTemplate(); }); break;
            case testInIntegraLive:     withValidModule ("Test in Integra Live", [this] { testInIntegraLiveNow(); }); break;
            case locateIntegraLive:     chooseIntegraLive(); break;
            case editInPdCommand:       tabs.setCurrentTabIndex (3); implementation->editInPd(); break;
            case createBundle:          createModuleBundle(); break;
            case extractBundle:         extractModuleBundle(); break;
            case licenseCommand:        showLicense (false); break;
            case aboutCommand:          showAbout(); break;
            default:                    return false;
        }
        return true;
    }

    //==============================================================================
    juce::StringArray MainComponent::getMenuBarNames()
    {
        return { "File", "Test", "Module Bundles", "Help" };
    }

    void MainComponent::addTemplateMenus (juce::PopupMenu& menu, const juce::File& directory, std::vector<juce::File>& files)
    {
        // directories first, then files, by name (compareTemplateFiles)
        auto dirs = directory.findChildFiles (juce::File::findDirectories, false);
        auto modules = directory.findChildFiles (juce::File::findFiles, false, "*.module");
        dirs.sort();
        modules.sort();
        for (auto& d : dirs)
        {
            juce::PopupMenu sub;
            addTemplateMenus (sub, d, files);
            if (sub.getNumItems() > 0)
                menu.addSubMenu (d.getFileName(), sub);
        }
        for (auto& f : modules)
        {
            files.push_back (f);
            menu.addItem (templateMenuBase + (int) files.size() - 1, f.getFileNameWithoutExtension());
        }
    }

    juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
    {
        juce::PopupMenu menu;
        if (index == 0)
        {
            menu.addCommandItem (&commands, fileNew);
            juce::PopupMenu templates;
            templateMenuFiles.clear();
            addTemplateMenus (templates, config.getTemplatesDirectory(), templateMenuFiles);
            templates.addSeparator();
            templates.addCommandItem (&commands, fileBrowseTemplate);
            menu.addSubMenu ("Import Template", templates);
            menu.addSeparator();
            menu.addCommandItem (&commands, fileOpen);
            menu.addSeparator();
            menu.addCommandItem (&commands, fileSave);
            menu.addCommandItem (&commands, fileSaveAs);
           #if ! JUCE_MAC
            menu.addSeparator();
            menu.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
           #endif
        }
        else if (index == 1)
        {
            menu.addCommandItem (&commands, testInIntegraLive);
            menu.addCommandItem (&commands, editInPdCommand);
            menu.addSeparator();
            menu.addCommandItem (&commands, locateIntegraLive);
        }
        else if (index == 2)
        {
            menu.addCommandItem (&commands, createBundle);
            menu.addCommandItem (&commands, extractBundle);
        }
        else if (index == 3)
        {
            for (size_t i = 0; i < config.helpLinks.size(); ++i)
            {
                if (config.helpLinks[i].second.startsWith ("http") && i > 0)
                    menu.addSeparator();
                menu.addItem (helpMenuBase + (int) i, config.helpLinks[i].first);
            }
            menu.addSeparator();
            menu.addCommandItem (&commands, licenseCommand);
           #if ! JUCE_MAC
            menu.addSeparator();
            menu.addCommandItem (&commands, aboutCommand);
           #endif
        }
        return menu;
    }

    void MainComponent::menuItemSelected (int id, int)
    {
        if (id >= templateMenuBase && id < templateMenuBase + (int) templateMenuFiles.size())
        {
            auto file = templateMenuFiles[(size_t) (id - templateMenuBase)];
            withUnsavedChangesCheck ("Import Template Interface", [this, file] { openTemplate (file); });
        }
        else if (id >= helpMenuBase && id < helpMenuBase + (int) config.helpLinks.size())
        {
            openHelpLink (config.helpLinks[(size_t) (id - helpMenuBase)].second);
        }
    }

    void MainComponent::openHelpLink (const juce::String& link)
    {
        if (link.startsWith ("http://") || link.startsWith ("https://"))
            juce::URL (link).launchInDefaultBrowser();
        else
        {
            auto file = config.resourcesDirectory.getChildFile (link);
            if (! file.startAsProcess())
                integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Help", "Can't open " + file.getFullPathName());
        }
    }

    //==============================================================================
    void MainComponent::withUnsavedChangesCheck (const juce::String& caption, std::function<void()> action)
    {
        if (! document.modified)
            return action();
        if (showingSavePrompt)
            return;

        showingSavePrompt = true;
        const auto name = document.definition.name.isNotEmpty() ? document.definition.name : juce::String ("untitled");
        juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, caption,
                                               "The module \"" + name + "\" has changed.\n\nWould you like to save your changes?",
                                               "Save", "Don't Save", "Cancel", this,
                                               juce::ModalCallbackFunction::create ([this, action] (int result)
        {
            showingSavePrompt = false;
            if (result == 1)
                save (action);
            else if (result == 2)
                action();
        }));
    }

    void MainComponent::requestQuit (std::function<void()> quit)
    {
        withUnsavedChangesCheck ("Quit", [this, quit]
        {
            host.kill();
            document.newFile();     // empties the working directory (Implementation.shutdown)
            quit();
        });
    }

    bool MainComponent::validateForSave (const juce::String& caption)
    {
        auto issues = document.validate();
        if (issues.empty())
            return true;

        // reportValidationFailure: up to 10 errors, and the page of the first one
        juce::String message ("Validation Error(s):\n\n");
        for (size_t i = 0; i < juce::jmin<size_t> (10, issues.size()); ++i)
            message << "* [" << pageName (issues[i].page) << "] " << issues[i].message << "\n";
        if (issues.size() > 10)
            message << "...(truncated)\n";
        message << "\n\nYou must fix all errors before saving.";

        tabs.setCurrentTabIndex ((int) issues[0].page);
        if (issues[0].page == ValidationIssue::Page::endpoints && issues[0].endpointIndex >= 0)
            endpoints->selectEndpoint (issues[0].endpointIndex);
        integra::showMessage (juce::MessageBoxIconType::WarningIcon, caption, message);
        return false;
    }

    void MainComponent::withValidModule (const juce::String& caption, std::function<void()> action)
    {
        if (! validateForSave (caption))
            return;
        if (! host.isRunning())
            return action();

        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, caption,
                                            "You are currently editing this module's implementation in PD.  You must save your changes in PD before saving your module.\n\nContinue?",
                                            "Continue", "Cancel", this,
                                            juce::ModalCallbackFunction::create ([action] (int result) { if (result != 0) action(); }));
    }

    //==============================================================================
    void MainComponent::openFile (const juce::File& file)
    {
        withUnsavedChangesCheck ("Load Interface", [this, file] { loadFile (file); });
    }

    void MainComponent::loadFile (const juce::File& file)
    {
        host.kill();
        rememberFolder ("moduleDirectory", file.getParentDirectory());
        auto r = document.open (file);
        if (r.failed())
            integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Open", "Error loading module file: " + r.getErrorMessage());
        endpoints->selectEndpoint (document.definition.endpoints.empty() ? -1 : 0);
    }

    void MainComponent::open()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Module", rememberedFolder ("moduleDirectory"), "*.module");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File())
                loadFile (fc.getResult());
        });
    }

    void MainComponent::openTemplate (const juce::File& file)
    {
        host.kill();
        auto r = document.importTemplate (file);
        if (r.failed())
            integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Import Template", "Error loading module file: " + r.getErrorMessage());
        endpoints->selectEndpoint (document.definition.endpoints.empty() ? -1 : 0);
        tabs.setCurrentTabIndex (0);
        interfaceInfo->getNameEditor().grabKeyboardFocus();
    }

    void MainComponent::browseForTemplate()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Module as Template", rememberedFolder ("templateDirectory"), "*.module");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File())
                return;
            rememberFolder ("templateDirectory", fc.getResult().getParentDirectory());
            openTemplate (fc.getResult());
        });
    }

    void MainComponent::save (std::function<void()> afterSave)
    {
        withValidModule ("Save Module", [this, afterSave]
        {
            if (document.file != juce::File())
                saveTo (document.file, afterSave);
            else
                saveAs (afterSave);
        });
    }

    void MainComponent::saveAs (std::function<void()> afterSave)
    {
        withValidModule ("Save Module", [this, afterSave]
        {
            auto suggestion = document.file != juce::File()
                                  ? document.file
                                  : rememberedFolder ("moduleDirectory").getChildFile ((document.definition.name.isNotEmpty() ? document.definition.name : juce::String ("Untitled")) + ".module");
            chooser = std::make_unique<juce::FileChooser> ("Save Module", suggestion, "*.module");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, afterSave] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                if (! file.hasFileExtension (ModuleFile::moduleExtension))
                    file = file.withFileExtension (ModuleFile::moduleExtension);
                rememberFolder ("moduleDirectory", file.getParentDirectory());
                saveTo (file, afterSave);
            });
        });
    }

    void MainComponent::saveTo (const juce::File& file, std::function<void()> afterSave)
    {
        auto r = document.save (file);
        if (r.failed())
            return integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Save Module", r.getErrorMessage());
        if (afterSave)
            afterSave();
    }

    //==============================================================================
    juce::File MainComponent::findIntegraLive() const
    {
        juce::File configured (properties.getUserSettings()->getValue ("integraLiveExecutable"));
        if (configured.exists())
            return configured;

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        juce::Array<juce::File> candidates;
       #if JUCE_MAC
        // Integra Module Creator.app beside Integra Live.app
        auto bundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
        candidates.add (bundle.getSiblingFile ("Integra Live.app"));
        candidates.add (juce::File ("/Applications/Integra Live.app"));
       #elif JUCE_WINDOWS
        candidates.add (exe.getSiblingFile ("Integra Live.exe"));
       #else
        candidates.add (exe.getSiblingFile ("integra-live"));
       #endif
        if (juce::String (MODULE_CREATOR_BUILT_INTEGRA_LIVE).isNotEmpty())
            candidates.add (juce::File (MODULE_CREATOR_BUILT_INTEGRA_LIVE));

        for (auto& c : candidates)
            if (c.exists())
                return c;
        return {};
    }

    void MainComponent::chooseIntegraLive (std::function<void()> then)
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate Integra Live", findIntegraLive());
        const int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                         | ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0 ? juce::FileBrowserComponent::canSelectDirectories : 0);
        chooser->launchAsync (chooserFlags, [this, then] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File())
                return;
            settings().setValue ("integraLiveExecutable", fc.getResult().getFullPathName());
            if (then)
                then();
        });
    }

    void MainComponent::testInIntegraLiveNow()
    {
        auto integraLive = findIntegraLive();
        if (! integraLive.exists())
            return chooseIntegraLive ([this] { withValidModule ("Test in Integra Live", [this] { testInIntegraLiveNow(); }); });

        // doTestInIntegraLive: a fresh module id, saved under the name Integra Live loads as
        // the in-development module; the file stays until the Module Creator quits
        document.definition.regenerateModuleGuid();
        document.refresh();
        auto directory = juce::File::createTempFile ("module-creator-test");
        directory.createDirectory();
        testDirectories.add (directory);
        auto moduleFile = directory.getChildFile (ModuleFile::testingModuleFileName);
        auto r = ModuleFile::save (moduleFile, document.definition, document.getWorkingDirectory(), config.getLicenseFile());
        if (r.failed())
            return integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Test in Integra Live", r.getErrorMessage());

        bool started = false;
       #if JUCE_MAC
        if (integraLive.isDirectory())
        {
            juce::ChildProcess open;
            started = open.start (juce::StringArray { "open", "-a", integraLive.getFullPathName(), moduleFile.getFullPathName() }, 0);
        }
        else
       #endif
        {
            // a running Integra Live receives the file from the new instance and installs it
            started = integraLive.startAsProcess (moduleFile.getFullPathName().quoted());
        }
        if (! started)
            integra::showMessage (juce::MessageBoxIconType::WarningIcon, "Test in Integra Live", "Can't start " + integraLive.getFullPathName());
    }

    //==============================================================================
    void MainComponent::createModuleBundle()
    {
        chooser = std::make_unique<juce::FileChooser> ("Select Modules to include in your Bundle...", rememberedFolder ("moduleDirectory"), "*.module");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
        {
            auto modules = fc.getResults();
            if (modules.isEmpty())
                return;
            rememberFolder ("moduleDirectory", modules[0].getParentDirectory());
            chooser = std::make_unique<juce::FileChooser> ("Export Bundle As...", rememberedFolder ("bundleDirectory").getChildFile ("Modules.bundle"), "*.bundle");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this, modules] (const juce::FileChooser& saveChooser)
            {
                auto bundle = saveChooser.getResult();
                if (bundle == juce::File())
                    return;
                if (! bundle.hasFileExtension (ModuleFile::bundleExtension))
                    bundle = bundle.withFileExtension (ModuleFile::bundleExtension);
                rememberFolder ("bundleDirectory", bundle.getParentDirectory());
                auto r = ModuleFile::createBundle (bundle, modules);
                juce::StringArray names;
                for (auto& m : modules)
                    names.add ("    " + m.getFileName());
                integra::showMessage (r.wasOk() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                      r.wasOk() ? "Bundle Created Successfully" : "Create Module Bundle",
                                      r.wasOk() ? "Added Modules:\n\n" + names.joinIntoString ("\n") : r.getErrorMessage());
            });
        });
    }

    void MainComponent::extractModuleBundle()
    {
        chooser = std::make_unique<juce::FileChooser> ("Select Bundle To Extract...", rememberedFolder ("bundleDirectory"), "*.bundle");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto bundle = fc.getResult();
            if (bundle == juce::File())
                return;
            rememberFolder ("bundleDirectory", bundle.getParentDirectory());
            chooser = std::make_unique<juce::FileChooser> ("Choose Module Extraction Directory...", rememberedFolder ("moduleDirectory"));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, bundle] (const juce::FileChooser& dirChooser)
            {
                auto folder = dirChooser.getResult();
                if (folder == juce::File())
                    return;
                rememberFolder ("moduleDirectory", folder);
                juce::StringArray report;
                auto r = ModuleFile::extractBundle (bundle, folder, report);
                integra::showMessage (r.wasOk() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                      "Bundle Extraction Complete",
                                      r.wasOk() ? "Extracted Modules:\n\n" + report.joinIntoString ("\n") : r.getErrorMessage());
            });
        });
    }

    //==============================================================================
    void MainComponent::showLicense (bool forAcceptance, std::function<void (bool)> done)
    {
        if (licenseWindow != nullptr)
        {
            licenseWindow->toFront (true);
            return;
        }
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new MarkdownPanel (config.getAgreementFile().loadFileAsString(), forAcceptance, std::move (done)));
        o.content->setSize (juce::jmin (720, getWidth() - 40), juce::jmin (600, getHeight() - 40));
        o.dialogTitle = "License Agreement";
        o.dialogBackgroundColour = Theme::get().panel;
        o.componentToCentreAround = this;
        o.escapeKeyTriggersCloseButton = ! forAcceptance;
        o.useNativeTitleBar = true;
        o.resizable = true;
        licenseWindow = o.launchAsync();
    }

    void MainComponent::showLicenseOnStartupIfNeeded (std::function<void()> declined)
    {
        // persistentLicenceData: accepted once per version
        const auto key = "licenseAccepted-" + juce::String (JUCE_APPLICATION_VERSION_STRING);
        if (settings().getBoolValue (key, false))
            return;
        showLicense (true, [this, key, declined] (bool accepted)
        {
            settings().setValue (key, accepted);
            if (! accepted && declined)
                declined();
        });
    }

    void MainComponent::showAbout()
    {
        if (aboutWindow != nullptr)
        {
            aboutWindow->toFront (true);
            return;
        }
        juce::String text;
        text << "# Integra Module Creator\n\n"
             << "Version " << JUCE_APPLICATION_VERSION_STRING << "\n\n"
             << "Creates modules for **Integra Live**: an interface definition (the module's endpoints and widgets) "
             << "and a Pure Data implementation, packed into a .module file.\n\n"
             << "A port of the Integra Module Creator from the Integra Module Developer SDK "
             << "(Birmingham Conservatoire, Birmingham City University) to JUCE.\n\n"
             << "- Original: [github.com/BirminghamConservatoire/IntegraLive](https://github.com/BirminghamConservatoire/IntegraLive)\n"
             << "- Modules are dedicated to the public domain (see Help > License Agreement)\n";
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new MarkdownPanel (text, false, {}));
        o.content->setSize (520, 340);
        o.dialogTitle = "About Integra Module Creator";
        o.dialogBackgroundColour = Theme::get().panel;
        o.componentToCentreAround = this;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        aboutWindow = o.launchAsync();
    }
}
