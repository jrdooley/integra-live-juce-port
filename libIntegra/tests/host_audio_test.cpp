/*
 * Verifies CServerStartupInfo::host_driven_audio / CIntegraSession::process_audio:
 * the embedding application pulls audio through libpd itself, as the JUCE app does.
 *
 *   integra_host_audio_test <module directory>
 *
 * Builds  AudioIn -> StereoAudioOut  inside a container, feeds a sine wave into the
 * input and checks it arrives at the output; then does the same with TestSource
 * (no input) to check Pd-generated signals come out.
 */

#include "api/integra_session.h"
#include "api/server_startup_info.h"
#include "api/server_lock.h"
#include "api/server.h"
#include "api/command.h"
#include "api/interface_definition.h"
#include "api/error.h"
#include "api/trace.h"
#include "api/path.h"
#include "api/value.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace integra_api;

static int failures = 0;

static void check( bool condition, const std::string &what )
{
    std::cout << ( condition ? "ok   " : "FAIL " ) << what << std::endl;
    if( !condition ) failures++;
}

static void check( const CError &error, const std::string &what )
{
    check( error == CError::SUCCESS, what + ( error == CError::SUCCESS ? "" : " (" + error.get_text() + ")" ) );
}

/* runs `blocks` blocks of host-driven audio, returns output RMS of the first output channel */
static float run_audio( CIntegraSession &session, int blocks, bool feed_sine )
{
    const int block_size = CIntegraSession::get_audio_block_size();
    const int channels = 2;
    const int sample_rate = 44100;
    std::vector<float> input( block_size * channels ), output( block_size * channels );

    double phase = 0, sum_of_squares = 0;
    int counted = 0;
    for( int block = 0; block < blocks; block++ )
    {
        for( int frame = 0; frame < block_size; frame++ )
        {
            float sample = feed_sine ? 0.5f * (float) std::sin( phase ) : 0.f;
            phase += 2 * M_PI * 440.0 / sample_rate;
            for( int channel = 0; channel < channels; channel++ ) input[ frame * channels + channel ] = sample;
        }

        CError error = session.process_audio( input.data(), output.data(), channels, channels, sample_rate );
        if( error != CError::SUCCESS ) return -1;

        if( block >= blocks / 2 )   // skip parameter ramps at the start
        {
            for( int frame = 0; frame < block_size; frame++ )
            {
                sum_of_squares += output[ frame * channels ] * output[ frame * channels ];
                counted++;
            }
        }
    }
    return (float) std::sqrt( sum_of_squares / counted );
}

int main( int argc, char **argv )
{
    if( argc < 2 )
    {
        std::cerr << "usage: integra_host_audio_test <module directory>" << std::endl;
        return 2;
    }

    CTrace::set_categories_to_trace( true, false, false );

    CServerStartupInfo startup_info;
    startup_info.system_module_directory = argv[ 1 ];
    startup_info.third_party_module_directory = "third_party";
    startup_info.host_driven_audio = true;

    CIntegraSession session;
    check( session.start_session( startup_info ), "start host-driven session" );

    std::map<std::string, GUID> modules;
    {
        CServerLock server = session.get_server();
        for( const GUID &id : server->get_all_module_ids() )
        {
            modules[ server->find_interface( id )->get_interface_info().get_name() ] = id;
        }

        check( server->process_command( INewCommand::create( modules[ "AudioSettings" ], "AudioSettings1", CPath() ) ), "create AudioSettings" );
        check( server->process_command( INewCommand::create( modules[ "Container" ], "Block", CPath() ) ), "create container" );
        check( server->process_command( INewCommand::create( modules[ "AudioIn" ], "AudioIn1", CPath( "Block" ) ) ), "create AudioIn" );
        check( server->process_command( INewCommand::create( modules[ "StereoAudioOut" ], "Out1", CPath( "Block" ) ) ), "create StereoAudioOut" );
        check( server->process_command( INewCommand::create( modules[ "Connection" ], "Connection1", CPath( "Block" ) ) ), "create Connection" );
        check( server->process_command( ISetCommand::create( CPath( "Block.Connection1.sourcePath" ), CStringValue( "AudioIn1.out" ) ) ), "set connection source" );
        check( server->process_command( ISetCommand::create( CPath( "Block.Connection1.targetPath" ), CStringValue( "Out1.in1" ) ) ), "set connection target" );

        const CValue *driver = server->get_value( CPath( "AudioSettings1.selectedDriver" ) );
        check( driver && (const string &) *driver == "Host", "AudioSettings reports host driver" );
    }

    float silent_rms = run_audio( session, 200, false );
    check( silent_rms >= 0 && silent_rms < 1e-4f, "silence in -> silence out (rms " + std::to_string( silent_rms ) + ")" );

    float thru_rms = run_audio( session, 400, true );
    check( thru_rms > 0.1f, "sine in -> AudioIn -> StereoAudioOut -> out (rms " + std::to_string( thru_rms ) + ")" );

    {
        CServerLock server = session.get_server();
        check( server->process_command( IDeleteCommand::create( CPath( "Block.Connection1" ) ) ), "delete connection" );
    }
    float disconnected_rms = run_audio( session, 200, true );
    check( disconnected_rms < 1e-4f, "deleting the connection stops the signal (rms " + std::to_string( disconnected_rms ) + ")" );

    {
        CServerLock server = session.get_server();
        check( server->process_command( INewCommand::create( modules[ "TestSource" ], "TestSource1", CPath( "Block" ) ) ), "create TestSource" );
        check( server->process_command( INewCommand::create( modules[ "Connection" ], "Connection2", CPath( "Block" ) ) ), "create Connection" );
        check( server->process_command( ISetCommand::create( CPath( "Block.Connection2.sourcePath" ), CStringValue( "TestSource1.out1" ) ) ), "set connection source" );
        check( server->process_command( ISetCommand::create( CPath( "Block.Connection2.targetPath" ), CStringValue( "Out1.in1" ) ) ), "set connection target" );
        server->process_command( ISetCommand::create( CPath( "Block.TestSource1.mute" ), CIntegerValue( 0 ) ) );
    }
    float generated_rms = run_audio( session, 400, false );
    check( generated_rms > 0.01f, "TestSource -> StereoAudioOut produces signal (rms " + std::to_string( generated_rms ) + ")" );

    check( session.end_session(), "end session" );

    if( failures )
    {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "host audio test passed" << std::endl;
    return 0;
}
