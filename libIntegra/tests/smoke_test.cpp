/*
 * Headless smoke test for the CMake build of libIntegra + libpd.
 *
 *   integra_smoke <module directory> <blocks directory>
 *
 * 1. starts a session with the shipped module library
 * 2. instantiates every module that has a DSP implementation inside a container
 *    and pings it through libpd (proves every Pd abstraction and external loads)
 * 3. loads every .integra file in the blocks directory and pings again
 * 4. runs the "no device" audio thread for a moment so libpd processes audio
 *
 * Exits non-zero on the first failure.
 */

#include "api/integra_session.h"
#include "api/server_startup_info.h"
#include "api/server_lock.h"
#include "api/server.h"
#include "api/command.h"
#include "api/interface_definition.h"
#include "api/guid_helper.h"
#include "api/node.h"
#include "api/error.h"
#include "api/trace.h"
#include "api/path.h"
#include "api/polling_notification_sink.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace integra_api;

static int failures = 0;

static void check( const CError &error, const std::string &what )
{
    if( error != CError::SUCCESS )
    {
        std::cerr << "FAIL: " << what << ": " << error.get_text() << std::endl;
        failures++;
    }
}

static std::vector<std::string> files_with_suffix( const std::string &directory, const std::string &suffix )
{
    std::vector<std::string> result;
    if( DIR *dir = opendir( directory.c_str() ) )
    {
        while( dirent *entry = readdir( dir ) )
        {
            std::string name( entry->d_name );
            if( name.size() > suffix.size() && name.compare( name.size() - suffix.size(), suffix.size(), suffix ) == 0 )
            {
                result.push_back( directory + "/" + name );
            }
        }
        closedir( dir );
    }
    return result;
}

int main( int argc, char **argv )
{
    if( argc < 3 )
    {
        std::cerr << "usage: integra_smoke <module directory> <blocks directory>" << std::endl;
        return 2;
    }

    CTrace::set_categories_to_trace( true, getenv( "INTEGRA_SMOKE_VERBOSE" ) != nullptr, false );

    CPollingNotificationSink sink;
    CServerStartupInfo startup_info;
    startup_info.system_module_directory = argv[ 1 ];
    startup_info.third_party_module_directory = "third_party";
    startup_info.notification_sink = &sink;

    CIntegraSession session;
    CError error = session.start_session( startup_info );
    if( error != CError::SUCCESS )
    {
        std::cerr << "FAIL: could not start session: " << error.get_text() << std::endl;
        return 1;
    }

    {
        CServerLock server = session.get_server();
        std::cout << "libIntegra " << server->get_libintegra_version() << std::endl;

        const guid_set &module_ids = server->get_all_module_ids();
        std::cout << module_ids.size() << " modules available" << std::endl;

        GUID container_guid {}, audio_settings_guid {};
        for( const GUID &id : module_ids )
        {
            const string &name = server->find_interface( id )->get_interface_info().get_name();
            if( name == "Container" ) container_guid = id;
            if( name == "AudioSettings" ) audio_settings_guid = id;
        }

        check( server->process_command( INewCommand::create( audio_settings_guid, "AudioSettings1", CPath() ) ), "create AudioSettings" );
        check( server->process_command( INewCommand::create( container_guid, "AllModules", CPath() ) ), "create container" );

        int instantiated = 0;
        for( const GUID &id : module_ids )
        {
            const IInterfaceDefinition *definition = server->find_interface( id );
            if( !definition->get_implementation_info() ) continue;

            const string &name = definition->get_interface_info().get_name();
            CError new_error = server->process_command( INewCommand::create( id, name + "1", CPath( "AllModules" ) ) );
            check( new_error, "instantiate " + name );
            if( new_error == CError::SUCCESS ) instantiated++;
        }
        std::cout << "instantiated " << instantiated << " DSP modules" << std::endl;
    }

    // give the no-device audio thread time to run the patch
    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );

    {
        CServerLock server = session.get_server();
        server->ping_all_dsp_modules();
        check( server->process_command( IDeleteCommand::create( CPath( "AllModules" ) ) ), "delete container" );
    }

    for( const std::string &block_file : files_with_suffix( argv[ 2 ], ".integra" ) )
    {
        CServerLock server = session.get_server();
        size_t nodes_before = server->get_nodes().size();
        check( server->process_command( ILoadCommand::create( block_file, CPath() ) ), "load " + block_file );
        size_t nodes_after = server->get_nodes().size();
        std::cout << "loaded " << block_file << " (" << ( nodes_after - nodes_before ) << " top-level node)" << std::endl;
        server->ping_all_dsp_modules();
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );

    CPollingNotificationSink::changed_endpoint_map changes;
    sink.get_changed_endpoints( changes );
    std::cout << changes.size() << " endpoint change notifications received" << std::endl;

    check( session.end_session(), "end session" );

    if( failures )
    {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "smoke test passed" << std::endl;
    return 0;
}
