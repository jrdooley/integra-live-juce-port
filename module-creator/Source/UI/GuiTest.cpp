#include "GuiTest.h"
#include "EndpointsPage.h"
#include "ImplementationPage.h"
#include "InterfaceInfoPage.h"
#include "WidgetLayoutPage.h"
#include "Core/ModuleFile.h"
#include <iostream>

namespace modulecreator
{
    namespace
    {
        struct Checker
        {
            juce::String& log;
            int failures = 0;

            void check (bool ok, const juce::String& what)
            {
                auto line = juce::String (ok ? "ok   " : "FAIL ") + what + "\n";
                log << line;
                std::cout << line << std::flush;
                if (! ok)
                    ++failures;
            }
        };

        void pump (int ms = 100)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
        }

        juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
        {
            auto source = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (source, pos, juce::ModifierKeys::leftButtonModifier, juce::MouseInputSource::defaultPressure,
                                     0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, 1, dragged);
        }

        void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
        {
            c.mouseDown (makeEvent (c, from, from, false));
            for (int i = 1; i <= 8; ++i)
                c.mouseDrag (makeEvent (c, from + (to - from) * ((float) i / 8.0f), from, true));
            c.mouseUp (makeEvent (c, to, from, true));
        }

        /** Types into a text editor as a user would, a key at a time (replacing its text);
            false if the editor lost the keyboard focus on the way. */
        bool type (juce::TextEditor& editor, const juce::String& text)
        {
            editor.grabKeyboardFocus();
            editor.selectAll();
            bool keptFocus = true;
            if (text.isEmpty())
                editor.insertTextAtCaret ({});
            for (auto ch : text)
            {
                editor.insertTextAtCaret (juce::String::charToString (ch));
                pump (5);   // TextEditor posts its change notification
                keptFocus = keptFocus && editor.hasKeyboardFocus (false);
            }
            pump (20);
            return keptFocus;
        }

        template <typename Type>
        void findAll (juce::Component& root, std::vector<Type*>& out)
        {
            for (auto* child : root.getChildren())
            {
                if (auto* t = dynamic_cast<Type*> (child))
                    out.push_back (t);
                findAll (*child, out);
            }
        }

        template <typename Type>
        Type& field (juce::Component& root, const juce::String& id)
        {
            std::vector<Type*> all;
            findAll (root, all);
            for (auto* f : all)
                if (f->getComponentID() == id)
                    return *f;
            static Type missing;
            std::cout << "no field " << id << std::endl;
            return missing;
        }

        void saveShot (juce::Component& c, const juce::String& name)
        {
            auto folder = juce::SystemStats::getEnvironmentVariable ("MODULE_CREATOR_GUI_TEST_SHOTS", {});
            if (folder.isEmpty())
                return;
            juce::File (folder).createDirectory();
            juce::PNGImageFormat png;
            juce::FileOutputStream out (juce::File (folder).getChildFile (name + ".png"));
            out.setPosition (0);
            out.truncate();
            png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds()), out);
        }
    }

    int runGuiTest (MainComponent& main, juce::String& log)
    {
        Checker t { log };
        auto& document = main.getDocument();
        auto& tabs = main.getTabs();
        auto& definition = document.definition;
        auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("module-creator-gui-test-" + createGuid());
        scratch.createDirectory();

        main.getCommandManager().invokeDirectly (MainComponent::fileNew, false);
        pump();
        t.check (! document.modified && definition.endpoints.empty() && tabs.getCurrentTabIndex() == 0, "File > New: an empty module");

        // --- Interface Info ----------------------------------------------------------------
        auto& info = main.getInterfaceInfoPage();
        const bool nameKeptFocus = type (info.getNameEditor(), "Tremolo");
        pump();
        t.check (definition.name == "Tremolo" && document.modified && main.getDocument().getTitle().endsWith ("*") && nameKeptFocus,
                 "typing the interface name edits the module and marks it modified, keeping the focus");
        type (info.getNameEditor(), "Bad Name!");
        t.check (definition.name == "BadName", "the name field only takes letters (" + definition.name + ")");
        type (info.getNameEditor(), "Tremolo");
        saveShot (main, "interface-info");

        // --- Endpoints ---------------------------------------------------------------------
        tabs.setCurrentTabIndex (1);
        pump();
        auto& endpoints = main.getEndpointsPage();
        auto& editor = endpoints.getEditor();
        for (int i = 0; i < 4; ++i)
        {
            endpoints.getAddButton().onClick();
            pump (30);
        }
        t.check (definition.endpoints.size() == 4 && endpoints.getSelectedIndex() == 3, "Add creates endpoints and selects the new one");

        endpoints.selectEndpoint (0);
        pump (30);
        type (editor.getNameEditor(), "in1");
        field<juce::ComboBox> (editor, "type").setSelectedItemIndex (1, juce::sendNotificationSync);
        pump (30);
        t.check (definition.endpoints[0].name == "in1" && ! definition.endpoints[0].isControl
                     && field<juce::ComboBox> (editor, "direction").isShowing() && ! field<juce::ComboBox> (editor, "stateType").isShowing(),
                 "the first endpoint becomes an audio input stream");

        endpoints.selectEndpoint (1);
        pump (30);
        type (editor.getNameEditor(), "out1");
        field<juce::ComboBox> (editor, "type").setSelectedItemIndex (1, juce::sendNotificationSync);
        field<juce::ComboBox> (editor, "direction").setSelectedItemIndex (1, juce::sendNotificationSync);
        pump (30);
        t.check (definition.endpoints[1].isAudioStream (false), "the second becomes an audio output");

        endpoints.selectEndpoint (2);
        pump (30);
        type (editor.getNameEditor(), "rate");
        const bool keptFocus = type (field<juce::TextEditor> (editor, "maximum"), "20");
        type (field<juce::TextEditor> (editor, "default"), "4");
        pump (30);
        t.check (keptFocus, "a field keeps the keyboard focus while typing (each key edits the module)");
        auto& rate = definition.endpoints[2];
        t.check (rate.name == "rate" && rate.maximum == "20" && rate.defaultValue == "4" && rate.isState,
                 "a float control with a 0..20 range and default 4");
        type (field<juce::TextEditor> (editor, "default"), "40x");
        t.check (! document.issuesFor (ValidationIssue::Page::endpoints, 2).empty(), "a default outside the range is an error on the endpoint");
        t.check (definition.endpoints[2].defaultValue == "40", "number fields only take number characters");
        type (field<juce::TextEditor> (editor, "default"), "4");

        // a stream has no widget
        endpoints.selectEndpoint (2);
        definition.assignWidget (definition.endpoints[2].uid, "Knob", "value", 0, { 85, 85 });
        field<juce::ComboBox> (editor, "type").setSelectedItemIndex (1, juce::sendNotificationSync);
        t.check (definition.widgets.empty(), "an endpoint that becomes a stream loses its widget");
        field<juce::ComboBox> (editor, "type").setSelectedItemIndex (0, juce::sendNotificationSync);
        pump (30);

        endpoints.selectEndpoint (3);
        pump (30);
        type (editor.getNameEditor(), "depth");
        pump (30);

        // widgets, as the widget menu assigns them
        const int rateUid = definition.endpoints[2].uid, depthUid = definition.endpoints[3].uid;
        definition.assignWidget (rateUid, "Knob", "value", 0, main.getDocument().config.findWidget ("Knob")->defaultSize);
        definition.assignWidget (depthUid, "Slider", "value", 0, main.getDocument().config.findWidget ("Slider")->defaultSize);
        document.changed();
        pump (30);
        t.check (editor.getWidgetButton().getButtonText() == "Slider" && definition.controlsWithoutWidgets().isEmpty(),
                 "the endpoint editor shows the assigned widget");
        t.check (document.validate().empty(), "the module is valid (" + (document.validate().empty() ? juce::String() : document.validate()[0].message) + ")");
        saveShot (main, "endpoints");

        // --- Widget Layout: drag a widget, then resize it --------------------------------------
        tabs.setCurrentTabIndex (2);
        pump();
        auto& canvas = main.getWidgetLayoutPage().getCanvas();
        auto knob = definition.widgetFor (rateUid)->bounds;
        drag (canvas, knob.getCentre().toFloat(), knob.getCentre().toFloat() + juce::Point<float> (300.0f, 40.0f));
        auto moved = definition.widgetFor (rateUid)->bounds;
        t.check (moved.getX() > knob.getX() + 250 && moved.getY() > knob.getY() + 30 && moved.getWidth() == knob.getWidth() && moved.getHeight() == knob.getHeight(),
                 "dragging a widget moves it");
        const auto slider = definition.widgetFor (depthUid)->bounds;
        drag (canvas, { (float) slider.getRight() - 2.0f, (float) slider.getCentreY() }, { (float) slider.getRight() + 400.0f, (float) slider.getCentreY() });
        auto* sliderDef = main.getDocument().config.findWidget ("Slider");
        t.check (definition.widgetFor (depthUid)->bounds.getWidth() == sliderDef->maximumSize.x, "dragging an edge resizes, up to the widget's maximum size");
        // snapping: drop the knob near the slider's top edge
        drag (canvas, moved.getCentre().toFloat(), moved.getCentre().toFloat() + juce::Point<float> (0.0f, (float) (slider.getY() - moved.getY() + 4)));
        t.check (definition.widgetFor (rateUid)->bounds.getY() == slider.getY(), "widget edges snap to other widgets");
        saveShot (main, "widget-layout");

        // --- Implementation: a patch in the working directory --------------------------------
        tabs.setCurrentTabIndex (3);
        auto wd = document.getWorkingDirectory();
        wd.getChildFile ("Tremolo.pd").replaceWithText ("#N canvas 0 0 450 300 10;\n#X obj 10 10 inlet~;\n#X obj 10 40 osc~ 4;\n"
                                                       "#X obj 10 70 *~;\n#X obj 10 100 outlet~;\n");
        document.pollWorkingDirectory();
        pump();
        t.check (definition.patchName == "Tremolo.pd" && document.validate().empty(), "the patch becomes the main patch");
        saveShot (main, "implementation");

        // --- validation failures switch to the page with the problem ---------------------------
        type (info.getNameEditor(), "");
        t.check (! main.validateForSave ("Save Module") && tabs.getCurrentTabIndex() == 0, "saving an invalid module shows its first problem's page");
        pump();
        juce::ModalComponentManager::getInstance()->cancelAllModalComponents();
        type (info.getNameEditor(), "Tremolo");

        // --- save and reopen ---------------------------------------------------------------
        auto file = scratch.getChildFile ("Tremolo.module");
        auto r = document.save (file);
        const auto saved = definition.toXmlString();
        main.getCommandManager().invokeDirectly (MainComponent::fileNew, false);
        pump();
        main.openFile (file);
        pump();
        t.check (r.wasOk() && definition.toXmlString() == saved && definition.endpoints.size() == 4 && endpoints.getSelectedIndex() == 0,
                 "save and reopen through the window");

        // --- import a template from the File menu --------------------------------------------
        auto fileMenu = main.getMenuForIndex (0, "File");
        bool hasTemplates = false, hasQuit = false;
        for (juce::PopupMenu::MenuItemIterator it (fileMenu, true); it.next();)
        {
            hasTemplates = hasTemplates || it.getItem().text == "StereoProcessor";
            hasQuit = hasQuit || it.getItem().itemID == juce::StandardApplicationCommandIDs::quit;
        }
        t.check (hasTemplates, "File > Import Template lists the templates");
       #if ! JUCE_MAC
        t.check (hasQuit, "File > Quit");
       #endif
        main.menuItemSelected (0x3000, 0);     // the first template
        pump();
        t.check (definition.name.isEmpty() && ! definition.endpoints.empty() && document.file == juce::File(), "importing a template starts a new module from it");

        main.getCommandManager().invokeDirectly (MainComponent::fileNew, false);
        pump();
        scratch.deleteRecursively();
        return t.failures;
    }
}
