#include <juce_gui_extra/juce_gui_extra.h>

#include "Core/IntegraSession.h"
#include "Core/AudioHost.h"
#include "Core/ProjectModel.h"
#include "Core/ProjectController.h"
#include "Core/SelfTest.h"
#include "Core/StressTest.h"
#include "Core/AppContext.h"
#include "UI/MainComponent.h"
#include "UI/IntegraLookAndFeel.h"
#include "UI/GuiTest.h"

namespace integra
{
    //==============================================================================
    class MainWindow  : public juce::DocumentWindow
    {
    public:
        MainWindow (AppContext& context)
            : DocumentWindow ("Integra Live", juce::Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setContentOwned (new MainComponent (context), true);
            setResizeLimits (800, 500, 10000, 10000);

            if (! restoreWindowStateFromString (context.properties.getUserSettings()->getValue ("windowState")))
                centreWithSize (1280, 800);

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        MainComponent* getMainComponent()   { return dynamic_cast<MainComponent*> (getContentComponent()); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    //==============================================================================
    class IntegraLiveApplication  : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override             { return false; }

        void initialise (const juce::String& commandLine) override
        {
            auto args = juce::StringArray::fromTokens (commandLine, true);
            args.trim();
            args.removeEmptyStrings();

            const bool selfTest = args.contains ("--self-test");
            const int stressIndex = args.indexOf ("--stress-test");
            const bool quiet = args.contains ("--quiet");
            CTrace::set_categories_to_trace (! quiet, false, false);

            juce::PropertiesFile::Options options;
            options.applicationName = "IntegraLive";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            options.folderName = "IntegraLive";
            context.properties.setStorageParameters (options);

            auto result = context.session.start (IntegraSession::Locations::findDefault());
            if (result.failed())
            {
                juce::Logger::writeToLog (result.getErrorMessage());
                if (selfTest || stressIndex >= 0)
                {
                    setApplicationReturnValue (1);
                    quit();
                    return;
                }

                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Integra Live", "Couldn't start libIntegra:\n" + result.getErrorMessage());
                quit();
                return;
            }

            std::unique_ptr<juce::XmlElement> audioState (context.properties.getUserSettings()->getXmlValue ("audioDeviceState"));
            auto audioError = context.audioHost.start (audioState.get());
            if (audioError.isNotEmpty())
                juce::Logger::writeToLog ("Audio: " + audioError);

            if (selfTest)
            {
                juce::String log;
                const int failures = runSelfTest (context.session, context.model, context.controller, context.audioHost, log);
                std::cout << log << (failures == 0 ? "self test passed" : juce::String (failures) + " self test failure(s)") << std::endl;
                setApplicationReturnValue (failures == 0 ? 0 : 1);
                quit();
                return;
            }

            // --stress-test [seconds] [project files...]
            if (stressIndex >= 0)
            {
                double seconds = 60.0;
                juce::Array<juce::File> projects;
                for (int i = stressIndex + 1; i < args.size(); ++i)
                {
                    if (args[i].containsOnly ("0123456789."))
                        seconds = args[i].getDoubleValue();
                    else if (juce::File::isAbsolutePath (args[i].unquoted()))
                        projects.add (juce::File (args[i].unquoted()));
                }

                juce::String log;
                const int failures = runStressTest (context.session, context.model, context.controller, context.audioHost, seconds, projects, log);
                std::cout << (failures == 0 ? "stress test passed" : juce::String (failures) + " stress test failure(s)") << std::endl;
                setApplicationReturnValue (failures == 0 ? 0 : 1);
                quit();
                return;
            }

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            // open a file given on the command line, else start a new project
            juce::File toOpen;
            for (int i = 0; i < args.size(); ++i)
            {
                if (args[i] == "--screenshot" || args[i] == "--view" || args[i] == "--select" || args[i] == "--scheme")
                    ++i;    // skip the option's value
                else if (! args[i].startsWith ("--") && juce::File::isAbsolutePath (args[i].unquoted()))
                    toOpen = juce::File (args[i].unquoted());
            }

            if (args.contains ("--demo"))
            {
                auto r = context.controller.newDemoProject();
                if (r.failed())
                    juce::Logger::writeToLog (r.getErrorMessage());
            }
            else if (toOpen.existsAsFile() && ! toOpen.hasFileExtension ("module;bundle"))
            {
                auto r = context.controller.openProject (toOpen);
                if (r.failed())
                    juce::Logger::writeToLog (r.getErrorMessage());
            }
            else
            {
                context.controller.newProject();
            }

            // --select <block or module path>: preselect (used for screenshots)
            const int selectIndex = args.indexOf ("--select");
            if (selectIndex >= 0 && selectIndex + 1 < args.size())
            {
                auto path = args[selectIndex + 1];
                auto& p = context.model.get();
                if (p.findBlock (path) != nullptr)
                {
                    context.model.selectedBlock = path;
                }
                else if (auto* block = p.findBlock (parentPath (path)))
                {
                    context.model.selectedBlock = block->path;
                    auto data = block->userData;
                    data.primarySelectedChild = lastElement (path);
                    context.session.setValue (block->path + ".userData", data.toString());
                    context.model.rebuild();
                }
                context.model.selectedTrack = parentPath (context.model.selectedBlock);
            }

            const int schemeIndex = args.indexOf ("--scheme");
            if (schemeIndex >= 0 && schemeIndex + 1 < args.size())
            {
                const auto scheme = args[schemeIndex + 1];
                context.controller.updateProjectUserData ([scheme] (ProjectUserData& d) { d.colorScheme = scheme; });
            }

            mainWindow = std::make_unique<MainWindow> (context);

            // a module or bundle given on the command line is installed (doAutoloadIfNecessary)
            if (toOpen.existsAsFile() && toOpen.hasFileExtension ("module;bundle"))
                juce::MessageManager::callAsync ([this, toOpen]
                {
                    if (mainWindow != nullptr)
                        if (auto* main = mainWindow->getMainComponent())
                            main->installFiles ({ toOpen });
                });

            if (args.contains ("--gui-test"))
            {
                // callAsync rather than a timer: the test pumps the message loop itself,
                // which would re-fire a still-pending one-shot timer
                juce::MessageManager::callAsync ([this]
                {
                    juce::String log;
                    const int failures = runGuiTest (*mainWindow->getMainComponent(), context, log);
                    std::cout << (failures == 0 ? "gui test passed" : juce::String (failures) + " gui test failure(s)") << std::endl;
                    setApplicationReturnValue (failures == 0 ? 0 : 1);

                    // Quit from a fresh dispatch: JUCE's Linux run loop shares one callback
                    // list between nested loops, so a quit posted from inside this (pumped)
                    // callback would be consumed without the outer loop noticing.
                    juce::Timer::callAfterDelay (100, [] { quit(); });
                });
                return;
            }

            const int screenshotIndex = args.indexOf ("--screenshot");
            if (screenshotIndex >= 0 && screenshotIndex + 1 < args.size())
                scheduleScreenshot (juce::File (args[screenshotIndex + 1].unquoted()), args);
        }

        void shutdown() override
        {
            if (mainWindow != nullptr)
            {
                context.properties.getUserSettings()->setValue ("windowState", mainWindow->getWindowStateAsString());
                mainWindow = nullptr;
            }

            if (auto state = context.audioHost.getDeviceManager().createStateXml())
                context.properties.getUserSettings()->setValue ("audioDeviceState", state.get());

            context.audioHost.stop();
            context.session.stop();
            context.properties.closeFiles();
            getUndoScratchDirectory().deleteRecursively();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        void systemRequestedQuit() override
        {
            if (mainWindow != nullptr)
                if (auto* main = mainWindow->getMainComponent())
                    return main->requestQuit ([] { quit(); });

            quit();
        }

        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            auto file = juce::File (commandLine.unquoted().trim());
            if (mainWindow != nullptr && file.existsAsFile())
                if (auto* main = mainWindow->getMainComponent())
                    main->openFile (file);
        }

    private:
        /** `--screenshot <file.png> [--view arrange|live|module] [--open <file>]`:
            renders the main window to a PNG after start-up and quits (used for docs/CI). */
        void scheduleScreenshot (juce::File target, juce::StringArray args)
        {
            juce::Timer::callAfterDelay (2500, [this, target, args]
            {
                if (mainWindow == nullptr)
                    return;

                if (auto* main = mainWindow->getMainComponent())
                {
                    int viewIndex = args.indexOf ("--view");
                    if (viewIndex >= 0 && viewIndex + 1 < args.size())
                        main->showView (args[viewIndex + 1]);
                }

                juce::Timer::callAfterDelay (1000, [this, target]
                {
                    auto image = mainWindow->getContentComponent()->createComponentSnapshot (mainWindow->getContentComponent()->getLocalBounds());
                    target.deleteFile();
                    juce::FileOutputStream out (target);
                    juce::PNGImageFormat().writeImageToStream (image, out);
                    out.flush();
                    std::cout << "screenshot written to " << target.getFullPathName() << std::endl;
                    quit();
                });
            });
        }

        AppContext context;
        IntegraLookAndFeel lookAndFeel;
        std::unique_ptr<MainWindow> mainWindow;
    };
}

START_JUCE_APPLICATION (integra::IntegraLiveApplication)
