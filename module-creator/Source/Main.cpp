#include "Core/SelfTest.h"
#include "UI/GuiTest.h"
#include "UI/MainComponent.h"
#include <iostream>

namespace modulecreator
{
    class MainWindow  : public juce::DocumentWindow
    {
    public:
        MainWindow (const CreatorConfig& config, juce::ApplicationProperties& appProperties, bool developerMode)
            : DocumentWindow ("Integra Module Creator", Theme::get().background, DocumentWindow::allButtons)
        {
            auto* main = new MainComponent (config, appProperties, developerMode);
            main->onTitleChanged = [this] (const juce::String& title) { setName (title); };
            setUsingNativeTitleBar (true);
            setContentOwned (main, true);
            setResizable (true, true);
            setResizeLimits (800, 520, 10000, 10000);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (main);
           #else
            setMenuBar (main);
           #endif
            addKeyListener (main->getCommandManager().getKeyMappings());

            const auto state = appProperties.getUserSettings()->getValue ("windowState");
            if (state.isEmpty() || ! restoreWindowStateFromString (state))
                centreWithSize (getWidth(), getHeight());
            setName (main->getDocument().getTitle());
            setVisible (true);
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        MainComponent* getMain()    { return dynamic_cast<MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    class ModuleCreatorApplication  : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override             { return true; }

        void initialise (const juce::String& commandLine) override
        {
            auto args = juce::StringArray::fromTokens (commandLine, true);
            for (auto& a : args)
                a = a.unquoted();

            auto r = CreatorConfig::load (CreatorConfig::findResourcesDirectory(), config);
            if (r.failed())
            {
                std::cerr << r.getErrorMessage() << std::endl;
                setApplicationReturnValue (1);
                quit();
                return;
            }

            if (args.contains ("--self-test"))
            {
                // --self-test [modules folder]
                juce::File modules (MODULE_CREATOR_SOURCE_MODULES_DIR);
                const int i = args.indexOf ("--self-test");
                if (i + 1 < args.size() && juce::File::isAbsolutePath (args[i + 1]))
                    modules = juce::File (args[i + 1]);
                juce::String log;
                const int failures = runSelfTest (config, modules, log);
                std::cout << log << (failures == 0 ? "self test passed" : juce::String (failures) + " self test failure(s)") << std::endl;
                setApplicationReturnValue (failures == 0 ? 0 : 1);
                quit();
                return;
            }

            juce::PropertiesFile::Options options;
            options.applicationName = "Integra Module Creator";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            options.folderName = "Integra Module Creator";
            properties.setStorageParameters (options);

            const bool guiTest = args.contains ("--gui-test");
            if (guiTest)
            {
                // a clean, throwaway settings file
                options.folderName = "Integra Module Creator GUI Test " + createGuid();
                properties.setStorageParameters (options);
                properties.getUserSettings()->setValue ("workingDirectoryParent", properties.getUserSettings()->getFile().getParentDirectory().getFullPathName());
            }

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            lookAndFeel.applyTheme();
            mainWindow = std::make_unique<MainWindow> (config, properties, args.contains ("--developer"));

            if (guiTest)
            {
                juce::MessageManager::callAsync ([this]
                {
                    juce::String log;
                    const int failures = runGuiTest (*mainWindow->getMain(), log);
                    std::cout << (failures == 0 ? "gui test passed" : juce::String (failures) + " gui test failure(s)") << std::endl;
                    setApplicationReturnValue (failures == 0 ? 0 : 1);
                    properties.getUserSettings()->getFile().getParentDirectory().deleteRecursively();
                    juce::Timer::callAfterDelay (100, [] { quit(); });
                });
                return;
            }

            juce::File moduleToOpen;
            for (auto& a : args)
                if (! a.startsWith ("--") && juce::File::isAbsolutePath (a) && juce::File (a).hasFileExtension ("module"))
                    moduleToOpen = juce::File (a);
            if (moduleToOpen != juce::File())
                mainWindow->getMain()->openFile (moduleToOpen);

            // --screenshots <folder> [module]: a PNG of each tab (for the documentation)
            const int shotsIndex = args.indexOf ("--screenshots");
            if (shotsIndex >= 0 && shotsIndex + 1 < args.size())
            {
                const juce::File folder (args[shotsIndex + 1]);
                // the work pumps the message loop, which would fire a still-running one-shot
                // timer again (and delete it under its own feet): run it from callAsync
                juce::Timer::callAfterDelay (500, [this, folder] { juce::MessageManager::callAsync ([this, folder]
                {
                    auto& main = *mainWindow->getMain();
                    folder.createDirectory();
                    const juce::StringArray names { "interface-info", "endpoints", "widget-layout", "implementation" };
                    for (int i = 0; i < names.size(); ++i)
                    {
                        main.getTabs().setCurrentTabIndex (i);
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                        juce::PNGImageFormat png;
                        auto file = folder.getChildFile ("module-creator-" + names[i] + ".png");
                        file.deleteFile();
                        juce::FileOutputStream out (file);
                        png.writeImageToStream (main.createComponentSnapshot (main.getLocalBounds()), out);
                    }
                    // quit from a fresh dispatch (a quit posted inside the pumped loop is lost)
                    juce::Timer::callAfterDelay (100, [] { quit(); });
                }); });
                return;
            }

            mainWindow->getMain()->showLicenseOnStartupIfNeeded ([] { quit(); });
        }

        void shutdown() override
        {
            if (mainWindow != nullptr)
            {
                if (properties.getUserSettings() != nullptr)
                    properties.getUserSettings()->setValue ("windowState", mainWindow->getWindowStateAsString());
                mainWindow = nullptr;
            }
            properties.closeFiles();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        void systemRequestedQuit() override
        {
            if (mainWindow != nullptr)
                if (auto* main = mainWindow->getMain())
                    return main->requestQuit ([] { quit(); });
            quit();
        }

        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            auto file = juce::File (commandLine.unquoted().trim());
            if (mainWindow != nullptr && file.existsAsFile())
                mainWindow->getMain()->openFile (file);
        }

    private:
        CreatorConfig config;
        juce::ApplicationProperties properties;
        integra::IntegraLookAndFeel lookAndFeel;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (modulecreator::ModuleCreatorApplication)
