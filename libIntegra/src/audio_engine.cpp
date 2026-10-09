/* libIntegra modular audio framework
 *
 * Copyright (C) 2007 Birmingham City University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, 
 * USA.
 */



#include "platform_specifics.h"

#include "audio_engine.h"
#include "portaudio_engine.h"
#include "host_audio_engine.h"


namespace integra_internal
{
	IAudioEngine *IAudioEngine::create_audio_engine( CDspEngine &dsp_engine, bool host_driven )
	{
		/*
		 host_driven: the embedding application owns the audio device and calls
		 CIntegraSession::process_audio (see CServerStartupInfo::host_driven_audio).
		 Otherwise libIntegra opens devices itself via PortAudio.
		*/

		IAudioEngine *engine = NULL;
		if( host_driven )
		{
			engine = new CHostAudioEngine;
		}
		else
		{
			engine = new CPortAudioEngine;
		}

		engine->m_dsp_engine = &dsp_engine;
		return engine;
	}
}

