#include "ModuleManager.h"
#include "IntegraLookAndFeel.h"
#include "PropertiesPanel.h"

namespace integra
{
    namespace
    {
        juce::String dateText (juce::Time t)    { return t.formatted ("%d %b %Y"); }

        bool containsGuid (const std::vector<GUID>& guids, const GUID& g)
        {
            return std::any_of (guids.begin(), guids.end(), [&] (const GUID& x) { return guidsEqual (x, g); });
        }

        //==========================================================================
        /** Scrollable Markdown text (the Flex info and report panels). */
        class MarkdownPanel  : public juce::Component
        {
        public:
            MarkdownPanel()
            {
                viewport.setViewedComponent (&content, false);
                viewport.setScrollBarsShown (true, false);
                addAndMakeVisible (viewport);
            }

            void setMarkdown (const juce::String& md)
            {
                content.markdown = md;
                resized();
                content.repaint();
            }

            const juce::String& getMarkdown() const noexcept   { return content.markdown; }

            void paint (juce::Graphics& g) override
            {
                g.setColour (Theme::get().panelAlt);
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
            }

            void resized() override
            {
                viewport.setBounds (getLocalBounds().reduced (2));
                const int width = juce::jmax (50, viewport.getMaximumVisibleWidth());
                juce::TextLayout layout;
                layout.createLayout (content.text(), (float) (width - 20));
                content.setSize (width, juce::jmax (viewport.getMaximumVisibleHeight(), (int) layout.getHeight() + 20));
            }

        private:
            struct Content  : public juce::Component
            {
                juce::String markdown;
                juce::AttributedString text() const   { return renderMarkdown (markdown, Theme::get().text, Theme::get().accent); }
                void paint (juce::Graphics& g) override  { text().draw (g, getLocalBounds().reduced (10).toFloat()); }
            };

            juce::Viewport viewport;
            Content content;
        };

        //==========================================================================
        /** A titled list of modules, identified by GUID. */
        class ModuleList  : public juce::Component,
                            private juce::ListBoxModel
        {
        public:
            ModuleList (const juce::String& title, bool multipleSelection)
            {
                heading.setText (title, juce::dontSendNotification);
                heading.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                addAndMakeVisible (heading);
                list.setModel (this);
                list.setMultipleSelectionEnabled (multipleSelection);
                list.setRowHeight (22);
                addAndMakeVisible (list);
            }

            void setItems (std::vector<GUID> newItems, std::function<juce::String (const GUID&)> textFn)
            {
                auto selected = getSelected();
                items = std::move (newItems);
                text = std::move (textFn);
                list.updateContent();
                list.deselectAllRows();
                for (int i = 0; i < (int) items.size(); ++i)
                    if (containsGuid (selected, items[(size_t) i]))
                        list.selectRow (i, false, false);
                list.repaint();
            }

            std::vector<GUID> getSelected() const
            {
                std::vector<GUID> result;
                for (int i = 0; i < (int) items.size(); ++i)
                    if (list.isRowSelected (i))
                        result.push_back (items[(size_t) i]);
                return result;
            }

            void select (const GUID& g)
            {
                for (int i = 0; i < (int) items.size(); ++i)
                    if (guidsEqual (items[(size_t) i], g))
                        list.selectRow (i);
            }

            juce::ListBox& getListBox() noexcept   { return list; }
            std::function<void()> onSelectionChanged;

            void resized() override
            {
                auto area = getLocalBounds();
                heading.setBounds (area.removeFromTop (22));
                list.setBounds (area);
            }

        private:
            int getNumRows() override  { return (int) items.size(); }

            void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
            {
                if (row < 0 || row >= (int) items.size())
                    return;
                auto& t = Theme::get();
                if (selected)
                    g.fillAll (t.selection);
                g.setColour (t.text);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (text ? text (items[(size_t) row]) : juce::String(), 6, 0, width - 8, height, juce::Justification::centredLeft);
            }

            void selectedRowsChanged (int) override   { if (onSelectionChanged) onSelectionChanged(); }

            juce::Label heading;
            juce::ListBox list;
            std::vector<GUID> items;
            std::function<juce::String (const GUID&)> text;
        };

        juce::String moduleInfoMarkdown (AppContext& context, const GUID& guid)
        {
            auto* e = context.session.getCatalogue().findByGuid (guid);
            if (e == nullptr)
                return {};

            juce::String md;
            md << "# " << e->label << "\n\n";
            md << "**Source:** " << e->getSourceLabel() << "\n\n";
            if (e->author.isNotEmpty())
                md << "**Author:** " << e->author << "\n\n";
            md << "**Created:** " << dateText (e->created) << "  **Updated:** " << dateText (e->modified) << "\n\n";
            if (! e->tags.isEmpty())
                md << "**Tags:** " << e->tags.joinIntoString (", ") << "\n\n";
            const int instances = context.controller.findInstances (guid).size();
            if (instances > 0)
                md << "**In use:** " << instances << (instances == 1 ? " instance" : " instances") << " in this project\n\n";
            md << e->description << "\n";
            return md;
        }

        void showWindow (std::unique_ptr<juce::Component> content, const juce::String& title, juce::Component::SafePointer<juce::DialogWindow>* holder = nullptr)
        {
            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = title;
            options.dialogBackgroundColour = Theme::get().panel;
            options.content.setOwned (content.release());
            options.escapeKeyTriggersCloseButton = true;
            options.useNativeTitleBar = true;
            options.resizable = true;
            auto* window = options.launchAsync();
            if (holder != nullptr)
                *holder = window;
        }
    }

    //==============================================================================
    class ModuleManager::InstallTab  : public juce::Component,
                                       private juce::ChangeListener
    {
    public:
        InstallTab (AppContext& c, std::function<void()> changed)
            : context (c), onModulesChanged (std::move (changed))
        {
            addAndMakeVisible (thirdParty);
            addAndMakeVisible (embedded);
            addAndMakeVisible (info);
            for (auto* b : { &uninstall, &installToDisk, &installFromFile })
                addAndMakeVisible (b);

            uninstall.setTooltip ("Uninstall the selected 3rd party modules");
            installToDisk.setTooltip ("Install the selected embedded modules as 3rd party modules");
            installFromFile.setTooltip ("Install a 3rd party module from a .module file");

            thirdParty.onSelectionChanged = [this] { showInfo (thirdParty.getSelected()); updateButtons(); };
            embedded.onSelectionChanged = [this] { showInfo (embedded.getSelected()); updateButtons(); };
            uninstall.onClick = [this] { uninstallSelected(); };
            installToDisk.onClick = [this] { installEmbedded(); };
            installFromFile.onClick = [this] { chooseFile(); };

            context.model.addChangeListener (this);
            refresh();
        }

        ~InstallTab() override   { context.model.removeChangeListener (this); }

        void refresh()
        {
            std::vector<GUID> installed, inProject;
            for (auto& e : context.session.getCatalogue().getEntries())
            {
                if (e.source == IInterfaceDefinition::MODULE_3RD_PARTY)
                    installed.push_back (e.guid);
                else if (e.source == IInterfaceDefinition::MODULE_EMBEDDED && ! e.isCore)
                    inProject.push_back (e.guid);
            }
            auto label = [this] (const GUID& g)
            {
                auto* e = context.session.getCatalogue().findByGuid (g);
                return e != nullptr ? e->label + "  (updated " + dateText (e->modified) + ")" : juce::String ("?");
            };
            thirdParty.setItems (installed, label);
            embedded.setItems (inProject, label);
            updateButtons();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10);
            auto left = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (8);

            auto buttons = left.removeFromBottom (28);
            installFromFile.setBounds (buttons.removeFromRight (150));
            left.removeFromBottom (8);

            auto top = left.removeFromTop (left.getHeight() / 2);
            uninstall.setBounds (top.removeFromBottom (28).removeFromRight (120));
            top.removeFromBottom (4);
            thirdParty.setBounds (top);

            left.removeFromTop (8);
            installToDisk.setBounds (left.removeFromBottom (28).removeFromRight (120));
            left.removeFromBottom (4);
            embedded.setBounds (left);

            info.setBounds (area);
        }

        ModuleList thirdParty { "Installed 3rd Party Modules", true }, embedded { "Embedded Modules", true };
        MarkdownPanel info;
        juce::TextButton uninstall { "Uninstall" }, installToDisk { "Install To Disk" }, installFromFile { "Install From File..." };

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }

        void updateButtons()
        {
            uninstall.setEnabled (! thirdParty.getSelected().empty());
            installToDisk.setEnabled (! embedded.getSelected().empty());
        }

        void showInfo (const std::vector<GUID>& selected)
        {
            info.setMarkdown (selected.empty() ? juce::String ("*Select a module to see its details.*")
                                               : moduleInfoMarkdown (context, selected.back()));
        }

        void modulesChanged()
        {
            refresh();
            if (onModulesChanged)
                onModulesChanged();
        }

        void uninstallSelected()
        {
            // UninstallModules.doFileDialog
            const auto selected = thirdParty.getSelected();
            if (selected.empty())
                return;

            auto* first = context.session.getCatalogue().findByGuid (selected.front());
            juce::String message = "You have chosen to uninstall "
                                 + (selected.size() == 1 && first != nullptr ? first->label : juce::String ((int) selected.size()) + " modules")
                                 + ".\n\nWarning: this operation cannot be undone.";

            bool inUse = false;
            for (auto& g : selected)
                inUse |= ! context.controller.findInstances (g).isEmpty();

            auto run = [this, selected] (bool deleteInstances)
            {
                juce::StringArray errors;
                for (auto& g : selected)
                {
                    auto r = context.controller.uninstallModule (g, deleteInstances);
                    if (r.failed())
                        errors.add (r.getErrorMessage());
                }
                modulesChanged();
                if (! errors.isEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Uninstall", errors.joinIntoString ("\n"));
            };

            if (inUse)
            {
                message << "\n\nNote: there are instances of these modules in the current project. "
                           "Delete them, or keep them as modules embedded in the project?";
                juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Uninstall Modules", message,
                                                       "Delete", "Embed", "Cancel", this,
                                                       juce::ModalCallbackFunction::create ([run] (int result)
                                                       {
                                                           if (result == 1) run (true);
                                                           else if (result == 2) run (false);
                                                       }));
            }
            else
            {
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Uninstall Modules",
                                                    message + "\n\nWould you like to proceed?", "OK", "Cancel", this,
                                                    juce::ModalCallbackFunction::create ([run] (int result) { if (result == 1) run (false); }));
            }
        }

        void installEmbedded()
        {
            juce::StringArray report;
            for (auto& g : embedded.getSelected())
            {
                auto* e = context.session.getCatalogue().findByGuid (g);
                const auto label = e != nullptr ? e->label : juce::String ("module");
                auto r = context.controller.installEmbeddedModule (g);
                report.add ("* " + label + (r.wasOk() ? juce::String (": installed") : ": " + r.getErrorMessage()));
            }
            modulesChanged();
            showReport ("Installation Report", report.joinIntoString ("\n"));
        }

        void chooseFile()
        {
            chooser = std::make_unique<juce::FileChooser> ("Install 3rd party modules", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.module");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResults().isEmpty())
                    return;
                juce::StringArray report;
                for (auto& f : fc.getResults())
                {
                    auto r = context.controller.installModule (f);
                    report.add ("* " + f.getFileNameWithoutExtension() + (r.wasOk() ? juce::String (": installed") : ": " + r.getErrorMessage()));
                }
                modulesChanged();
                showReport ("Installation Report", report.joinIntoString ("\n"));
                ModuleManager::checkForUpgrades (context, onModulesChanged);
            });
        }

        AppContext& context;
        std::function<void()> onModulesChanged;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    //==============================================================================
    class ModuleManager::VersionsTab  : public juce::Component,
                                        private juce::ChangeListener
    {
    public:
        VersionsTab (AppContext& c, std::function<void()> changed)
            : context (c), onModulesChanged (std::move (changed))
        {
            addAndMakeVisible (inUse);
            addAndMakeVisible (alternatives);
            addAndMakeVisible (report);
            for (auto* b : { &switchVersions, &upgradeAllButton })
                addAndMakeVisible (b);
            addAndMakeVisible (alwaysUpgrade);

            switchVersions.setTooltip ("Switch every instance of the selected module to the selected alternative version");
            upgradeAllButton.setTooltip ("Upgrade every module in the project to its latest official version");
            alwaysUpgrade.setTooltip ("Automatically upgrade modules to the latest official version when projects are loaded");
            alwaysUpgrade.setToggleState (ModuleManager::isAlwaysUpgrade (context), juce::dontSendNotification);

            inUse.onSelectionChanged = [this] { refreshAlternatives(); };
            alternatives.onSelectionChanged = [this] { updateReport(); };
            switchVersions.onClick = [this] { doSwitch(); };
            upgradeAllButton.onClick = [this] { doUpgradeAll(); };
            alwaysUpgrade.onClick = [this] { ModuleManager::setAlwaysUpgrade (context, alwaysUpgrade.getToggleState()); };

            context.model.addChangeListener (this);
            refresh();
        }

        ~VersionsTab() override   { context.model.removeChangeListener (this); }

        void refresh()
        {
            // SwitchVersionsTab: module versions in use that have alternatives
            auto& catalogue = context.session.getCatalogue();
            std::vector<GUID> used;
            for (auto& t : context.model.get().tracks)
                for (auto& b : t.blocks)
                    for (auto& m : b.modules)
                        if (m.entry != nullptr && catalogue.getVersions (m.entry->originGuid).size() > 1 && ! containsGuid (used, m.entry->guid))
                            used.push_back (m.entry->guid);

            inUse.setItems (used, [this] (const GUID& g)
            {
                auto* e = context.session.getCatalogue().findByGuid (g);
                if (e == nullptr)
                    return juce::String ("?");
                const int n = context.controller.findInstances (g).size();
                return e->label + "  (" + e->getSourceLabel() + ", " + juce::String (n) + (n == 1 ? " instance)" : " instances)");
            });
            refreshAlternatives();
            upgradeAllButton.setEnabled (context.controller.hasUpgradableModules());
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10);
            auto bottom = area.removeFromBottom (28);
            alwaysUpgrade.setBounds (bottom.removeFromLeft (160));
            upgradeAllButton.setBounds (bottom.removeFromRight (120));
            bottom.removeFromRight (8);
            switchVersions.setBounds (bottom.removeFromRight (130));
            area.removeFromBottom (8);

            auto lists = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (8);
            inUse.setBounds (lists.removeFromTop (lists.getHeight() / 2));
            lists.removeFromTop (8);
            alternatives.setBounds (lists);
            report.setBounds (area);
        }

        ModuleList inUse { "Modules", false }, alternatives { "Alternative Versions", false };
        MarkdownPanel report;
        juce::TextButton switchVersions { "Switch Versions" }, upgradeAllButton { "Upgrade All" };
        juce::ToggleButton alwaysUpgrade { "Always Upgrade" };

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }

        void refreshAlternatives()
        {
            auto selected = inUse.getSelected();
            std::vector<GUID> others;
            if (! selected.empty())
                if (auto* e = context.session.getCatalogue().findByGuid (selected.front()))
                    for (auto* v : context.session.getCatalogue().getVersions (e->originGuid))
                        if (! guidsEqual (v->guid, e->guid))
                            others.push_back (v->guid);

            alternatives.setItems (others, [this] (const GUID& g)
            {
                auto& catalogue = context.session.getCatalogue();
                auto* e = catalogue.findByGuid (g);
                if (e == nullptr)
                    return juce::String ("?");
                const bool latest = catalogue.getBestVersion (*e) == e;
                return e->getSourceLabel() + ", updated " + dateText (e->modified) + (latest ? "  (Latest Official)" : "");
            });
            updateReport();
        }

        void updateReport()
        {
            auto from = inUse.getSelected();
            auto to = alternatives.getSelected();
            switchVersions.setEnabled (! from.empty() && ! to.empty());

            if (from.empty())
            {
                report.setMarkdown (inUse.getListBox().getModel()->getNumRows() == 0
                                        ? juce::String ("*No module in this project has alternative versions.*")
                                        : juce::String ("*Select a module, then the version to switch to.*"));
                return;
            }
            if (to.empty())
            {
                report.setMarkdown (moduleInfoMarkdown (context, from.front()));
                return;
            }

            // ModuleManagementInfoGenerator.getModuleSwitchReport
            juce::String md;
            md << "# Switch Versions\n\n" << context.controller.describeVersionChange (from.front(), to.front()) << "\n\n## Affected modules\n\n";
            for (auto& path : context.controller.findInstances (from.front()))
                md << "* " << path.fromFirstOccurrenceOf (".", false, false) << "\n";
            md << "\nNothing changes until you click **Switch Versions**.\n";
            report.setMarkdown (md);
        }

        void doSwitch()
        {
            auto from = inUse.getSelected();
            auto to = alternatives.getSelected();
            if (from.empty() || to.empty())
                return;
            auto switched = context.controller.switchAllVersions (from.front(), to.front());
            if (onModulesChanged)
                onModulesChanged();
            refresh();
            inUse.select (to.front());
            showReport ("Module Switch Report", "Switched " + juce::String (switched.size()) + (switched.size() == 1 ? " module" : " modules")
                                                    + ":\n\n* " + switched.joinIntoString ("\n* "));
        }

        void doUpgradeAll()
        {
            auto result = context.controller.upgradeAll();
            if (onModulesChanged)
                onModulesChanged();
            refresh();
            showReport ("Module Upgrade Report", ModuleManager::describeUpgrade (context, result));
        }

        AppContext& context;
        std::function<void()> onModulesChanged;
    };

    //==============================================================================
    namespace
    {
        juce::Component::SafePointer<juce::DialogWindow> openManager;
    }

    ModuleManager::ModuleManager (AppContext& c, std::function<void()> onModulesChanged)
        : context (c)
    {
        auto background = Theme::get().panel;
        installTab = new InstallTab (context, onModulesChanged);
        versionsTab = new VersionsTab (context, onModulesChanged);
        tabs.addTab ("Install / Uninstall", background, installTab, true);
        tabs.addTab ("Manage Versions", background, versionsTab, true);
        addAndMakeVisible (tabs);
        setSize (900, 560);
    }

    ModuleManager::~ModuleManager() = default;

    void ModuleManager::paint (juce::Graphics& g)   { g.fillAll (Theme::get().panel); }
    void ModuleManager::resized()                   { tabs.setBounds (getLocalBounds()); }
    void ModuleManager::showTab (Tab tab)           { tabs.setCurrentTabIndex ((int) tab); }

    void ModuleManager::show (AppContext& context, std::function<void()> onModulesChanged, Tab tab)
    {
        if (openManager != nullptr)
        {
            if (auto* manager = dynamic_cast<ModuleManager*> (openManager->getContentComponent()))
                manager->showTab (tab);
            openManager->toFront (true);
            return;
        }

        auto manager = std::make_unique<ModuleManager> (context, std::move (onModulesChanged));
        manager->showTab (tab);
        showWindow (std::move (manager), "Module Manager", &openManager);
    }

    bool ModuleManager::isAlwaysUpgrade (AppContext& context)
    {
        auto* settings = context.properties.getUserSettings();
        return settings != nullptr && settings->getBoolValue ("alwaysUpgrade", false);
    }

    void ModuleManager::setAlwaysUpgrade (AppContext& context, bool shouldUpgrade)
    {
        if (auto* settings = context.properties.getUserSettings())
            settings->setValue ("alwaysUpgrade", shouldUpgrade);
    }

    juce::String ModuleManager::describeUpgrade (AppContext& context, const ProjectController::UpgradeResult& result)
    {
        // ModuleManagementInfoGenerator.getUpgradeReport
        if (result.upgradedModules.isEmpty())
            return "Nothing needs to be upgraded.";

        auto& catalogue = context.session.getCatalogue();
        juce::String md = "The following modules have been upgraded:\n\n";
        for (auto& path : result.upgradedModules)
            md << "* " << path.fromFirstOccurrenceOf (".", false, false) << "\n";

        md << "\n# Upgrade Details\n\n";
        for (auto& from : result.upgradedFrom)
            if (auto* previous = catalogue.findByGuid (from))
                md << "## " << previous->label << "\n\n" << context.controller.describeVersionChange (from, catalogue.getBestVersion (*previous)->guid) << "\n\n";

        if (result.backup != juce::File())
            md << "# Backup\n\nIn case the upgrade has caused any problems, the project as it was before the upgrade was saved to **"
               << result.backup.getFullPathName() << "**\n";
        return md;
    }

    void ModuleManager::showReport (const juce::String& title, const juce::String& markdown)
    {
        auto panel = std::make_unique<MarkdownPanel>();
        panel->setMarkdown (markdown);
        panel->setSize (560, 420);
        showWindow (std::move (panel), title);
    }

    //==============================================================================
    namespace
    {
        /** UpgradeDialog: "Don't Upgrade", "Module Manager...", "Upgrade", "Always Upgrade". */
        class UpgradeDialog  : public juce::Component
        {
        public:
            UpgradeDialog (AppContext& c, std::function<void()> changed) : context (c), onModulesChanged (std::move (changed))
            {
                auto& catalogue = context.session.getCatalogue();
                juce::String md = "This project uses modules for which newer versions are available:\n\n";
                for (auto& t : context.model.get().tracks)
                    for (auto& b : t.blocks)
                        for (auto& m : b.modules)
                            if (m.entry != nullptr)
                                if (auto* best = catalogue.getBestVersion (*m.entry); best != m.entry)
                                    md << "* " << m.path.fromFirstOccurrenceOf (".", false, false) << ": " << m.entry->getSourceLabel()
                                       << " version (updated " << dateText (m.entry->modified) << ") -> " << best->getSourceLabel()
                                       << " version (updated " << dateText (best->modified) << ")\n";
                md << "\nUpgrade them to the latest official versions?";
                text.setMarkdown (md);
                addAndMakeVisible (text);

                for (auto* b : { &dontUpgrade, &manager, &upgrade })
                    addAndMakeVisible (b);
                addAndMakeVisible (always);
                always.setToggleState (ModuleManager::isAlwaysUpgrade (context), juce::dontSendNotification);

                always.onClick = [this] { ModuleManager::setAlwaysUpgrade (context, always.getToggleState()); };
                dontUpgrade.onClick = [this] { close(); };
                manager.onClick = [this]
                {
                    ModuleManager::show (context, onModulesChanged, ModuleManager::Tab::versions);
                    close();
                };
                upgrade.onClick = [this]
                {
                    auto result = context.controller.upgradeAll();
                    if (onModulesChanged)
                        onModulesChanged();
                    ModuleManager::showReport ("Module Upgrade Report", ModuleManager::describeUpgrade (context, result));
                    close();
                };
                setSize (620, 360);
            }

            void paint (juce::Graphics& g) override   { g.fillAll (Theme::get().panel); }

            void resized() override
            {
                auto area = getLocalBounds().reduced (10);
                auto buttons = area.removeFromBottom (28);
                always.setBounds (buttons.removeFromLeft (140));
                upgrade.setBounds (buttons.removeFromRight (100));
                buttons.removeFromRight (8);
                manager.setBounds (buttons.removeFromRight (140));
                buttons.removeFromRight (8);
                dontUpgrade.setBounds (buttons.removeFromRight (120));
                area.removeFromBottom (8);
                text.setBounds (area);
            }

        private:
            void close()
            {
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
            }

            AppContext& context;
            std::function<void()> onModulesChanged;
            MarkdownPanel text;
            juce::TextButton dontUpgrade { "Don't Upgrade" }, manager { "Module Manager..." }, upgrade { "Upgrade" };
            juce::ToggleButton always { "Always Upgrade" };
        };
    }

    void ModuleManager::checkForUpgrades (AppContext& context, std::function<void()> onModulesChanged)
    {
        if (! context.controller.hasUpgradableModules())
            return;

        if (isAlwaysUpgrade (context))
        {
            auto result = context.controller.upgradeAll();
            if (onModulesChanged)
                onModulesChanged();
            showReport ("Module Upgrade Report", describeUpgrade (context, result));
            return;
        }

        showWindow (std::make_unique<UpgradeDialog> (context, std::move (onModulesChanged)), "Upgrade Modules");
    }
}
