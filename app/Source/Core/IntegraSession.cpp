#include "IntegraSession.h"

namespace integra
{
    //==============================================================================
    IntegraSession::Locations IntegraSession::Locations::findDefault()
    {
        Locations l;

        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

       #if JUCE_MAC
        auto bundled = exeDir.getParentDirectory().getChildFile ("Resources");
       #else
        auto bundled = exeDir;
       #endif

        auto pick = [] (const juce::File& installed, const char* sourceTreeFallback)
        {
            if (installed.isDirectory())
                return installed;
            return juce::File (juce::String (sourceTreeFallback));
        };

       #ifdef INTEGRA_SOURCE_MODULES_DIR
        l.systemModules = pick (bundled.getChildFile ("modules"), INTEGRA_SOURCE_MODULES_DIR);
        l.blockLibrary  = pick (bundled.getChildFile ("blocks"), INTEGRA_SOURCE_BLOCKS_DIR);
        l.resources     = pick (bundled.getChildFile ("resources"), INTEGRA_SOURCE_RESOURCES_DIR);
        l.viewInfo      = pick (bundled.getChildFile ("viewInfo"), INTEGRA_SOURCE_VIEWINFO_DIR);
       #else
        l.systemModules = bundled.getChildFile ("modules");
        l.blockLibrary  = bundled.getChildFile ("blocks");
        l.resources     = bundled.getChildFile ("resources");
        l.viewInfo      = bundled.getChildFile ("viewInfo");
       #endif

        auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("IntegraLive");
        l.thirdPartyModules = userData.getChildFile ("ThirdPartyModules");
        l.userBlockLibrary = userData.getChildFile ("BlockLibrary");
        return l;
    }

    //==============================================================================
    IntegraSession::~IntegraSession()
    {
        stop();
    }

    juce::Result IntegraSession::start (const Locations& newLocations)
    {
        if (running)
            return juce::Result::fail ("Session already running");

        locations = newLocations;

        if (! locations.systemModules.isDirectory())
            return juce::Result::fail ("Module directory not found: " + locations.systemModules.getFullPathName());

        locations.thirdPartyModules.createDirectory();
        locations.userBlockLibrary.createDirectory();

        // libIntegra finds its XML schema and legacy id table through this variable
        // (see CFileHelper::get_resources_path).
        if (locations.resources.isDirectory())
        {
           #if JUCE_WINDOWS
            _putenv_s ("INTEGRA_RESOURCES_PATH", locations.resources.getFullPathName().toRawUTF8());
           #else
            setenv ("INTEGRA_RESOURCES_PATH", locations.resources.getFullPathName().toRawUTF8(), 1);
           #endif
        }

        CServerStartupInfo info;
        info.system_module_directory = toStd (locations.systemModules.getFullPathName() + juce::File::getSeparatorString());
        info.third_party_module_directory = toStd (locations.thirdPartyModules.getFullPathName() + juce::File::getSeparatorString());
        info.notification_sink = &notifications;
        info.host_driven_audio = true;

        CError error = session.start_session (info);
        if (error != CError::SUCCESS)
            return juce::Result::fail ("libIntegra failed to start: " + toString (error.get_text()));

        running = true;
        refreshCatalogue();
        return juce::Result::ok();
    }

    void IntegraSession::stop()
    {
        if (! running)
            return;

        running = false;
        session.end_session();
    }

    void IntegraSession::refreshCatalogue()
    {
        if (! running)
            return;

        auto server = lock();
        catalogue.rebuild (*server);
    }

    //==============================================================================
    CError IntegraSession::run (ICommand* command, CCommandResult* result)
    {
        jassert (running);
        CError error = lock()->process_command (command, result);

        if (error != CError::SUCCESS)
        {
            lastError = toString (error.get_text());
            DBG ("libIntegra command failed: " << lastError);
        }

        return error;
    }

    CError IntegraSession::createNode (const GUID& moduleGuid, const juce::String& name, const juce::String& parentPath)
    {
        CError error = run (INewCommand::create (moduleGuid, toStd (name), toPath (parentPath)));
        if (error == CError::SUCCESS)
            notifications.notifyStructureChanged();
        return error;
    }

    CError IntegraSession::createNode (const juce::String& moduleName, const juce::String& name, const juce::String& parentPath)
    {
        if (auto* entry = catalogue.findByName (moduleName))
            return createNode (entry->guid, name, parentPath);

        lastError = "Unknown module " + moduleName;
        return CError::INPUT_ERROR;
    }

    CError IntegraSession::deleteNode (const juce::String& path)
    {
        CError error = run (IDeleteCommand::create (toPath (path)));
        if (error == CError::SUCCESS)
            notifications.notifyStructureChanged();
        return error;
    }

    CError IntegraSession::renameNode (const juce::String& path, const juce::String& newName)
    {
        CError error = run (IRenameCommand::create (toPath (path), toStd (newName)));
        if (error == CError::SUCCESS)
            notifications.notifyStructureChanged();
        return error;
    }

    CError IntegraSession::moveNode (const juce::String& path, const juce::String& newParentPath)
    {
        CError error = run (IMoveCommand::create (toPath (path), toPath (newParentPath)));
        if (error == CError::SUCCESS)
            notifications.notifyStructureChanged();
        return error;
    }

    CError IntegraSession::setValue (const juce::String& endpointPath, const juce::var& value)
    {
        CValue::type type;
        bool isBang = false;
        {
            auto server = lock();
            const INodeEndpoint* endpoint = server->find_node_endpoint (toPath (endpointPath));
            if (endpoint == nullptr)
            {
                lastError = "No such endpoint: " + endpointPath;
                return CError::PATH_ERROR;
            }

            const IControlInfo* control = endpoint->get_endpoint_definition().get_control_info();
            if (control == nullptr)
            {
                lastError = "Not a control endpoint: " + endpointPath;
                return CError::TYPE_ERROR;
            }

            isBang = control->get_type() == IControlInfo::BANG;
            type = isBang ? CValue::INTEGER : control->get_state_info()->get_type();
        }

        if (isBang)
            return run (ISetCommand::create (toPath (endpointPath)));

        auto converted = toValue (value, type);
        return run (ISetCommand::create (toPath (endpointPath), *converted));
    }

    CError IntegraSession::bang (const juce::String& endpointPath)
    {
        return run (ISetCommand::create (toPath (endpointPath)));
    }

    CError IntegraSession::loadFile (const juce::File& file, const juce::String& parentPath, guid_set* newEmbeddedModules)
    {
        CLoadCommandResult result;
        CError error = run (ILoadCommand::create (toStd (file.getFullPathName()), toPath (parentPath)), &result);

        if (error == CError::SUCCESS)
        {
            if (newEmbeddedModules != nullptr)
                *newEmbeddedModules = result.get_new_embedded_module_ids();

            if (! result.get_new_embedded_module_ids().empty())
                refreshCatalogue();

            notifications.notifyStructureChanged();
        }

        return error;
    }

    CError IntegraSession::saveFile (const juce::File& file, const juce::String& nodePath)
    {
        return run (ISaveCommand::create (toStd (file.getFullPathName()), toPath (nodePath)));
    }

    //==============================================================================
    juce::var IntegraSession::getValue (const juce::String& endpointPath)
    {
        auto server = lock();
        return toVar (server->get_value (toPath (endpointPath)));
    }

    bool IntegraSession::nodeExists (const juce::String& path)
    {
        auto server = lock();
        return server->find_node (toPath (path)) != nullptr;
    }

    juce::String IntegraSession::getModuleName (const juce::String& nodePath)
    {
        auto server = lock();
        if (auto* node = server->find_node (toPath (nodePath)))
            return toString (node->get_interface_definition().get_interface_info().get_name());
        return {};
    }

    juce::StringArray IntegraSession::getChildNames (const juce::String& parentPath)
    {
        juce::StringArray names;
        auto server = lock();

        const node_map* children = nullptr;
        if (parentPath.isEmpty())
            children = &server->get_nodes();
        else if (auto* node = server->find_node (toPath (parentPath)))
            children = &node->get_children();

        if (children != nullptr)
            for (auto& child : *children)
                names.add (toString (child.first));

        return names;
    }

    juce::String IntegraSession::getNewChildName (const juce::String& parentPath, const juce::String& prefix, const GUID& moduleGuid)
    {
        juce::StringArray existing;
        int sameType = 0;
        {
            auto server = lock();
            const node_map* children = nullptr;
            if (parentPath.isEmpty())
                children = &server->get_nodes();
            else if (auto* node = server->find_node (toPath (parentPath)))
                children = &node->get_children();

            if (children != nullptr)
            {
                for (auto& child : *children)
                {
                    existing.add (toString (child.first));
                    if (guidsEqual (child.second->get_interface_definition().get_module_guid(), moduleGuid))
                        ++sameType;
                }
            }
        }

        for (int number = sameType + 1;; ++number)
        {
            auto candidate = prefix + juce::String (number);
            if (! existing.contains (candidate))
                return candidate;
        }
    }
}
