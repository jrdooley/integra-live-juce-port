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


#ifndef INTEGRA_HOST_AUDIO_ENGINE_H
#define INTEGRA_HOST_AUDIO_ENGINE_H

#include "audio_engine.h"

#include <atomic>


namespace integra_internal
{
	/*
	 CHostAudioEngine is used when the application embedding libIntegra owns the audio
	 device (eg a JUCE AudioDeviceManager).  It opens no streams itself; instead the host
	 calls CIntegraSession::process_audio() from its audio callback, which forwards to
	 CDspEngine::process_buffer().

	 The device-selection methods required by IAudioEngine (and exposed through the
	 AudioSettings module) only record what the host reports, since device choice is
	 the host's responsibility in this mode.
	*/
	class CHostAudioEngine : public IAudioEngine
	{
		public:

			CHostAudioEngine();
			~CHostAudioEngine();

			CError set_driver( const string &driver );
			CError set_input_device( const string &input_device );
			CError set_output_device( const string &output_device );

			CError set_sample_rate( int sample_rate );
			CError set_number_of_input_channels( int input_channels );
			CError set_number_of_output_channels( int output_channels );

			CError restore_defaults();

			string_vector get_available_drivers() const;
			string_vector get_available_input_devices() const;
			string_vector get_available_output_devices() const;
			int_vector get_available_sample_rates() const;

			string get_selected_driver() const;
			string get_selected_input_device() const;
			string get_selected_output_device() const;

			int get_sample_rate() const;
			int get_number_of_input_channels() const;
			int get_number_of_output_channels() const;

			/* called by the host's audio thread for each block of CDspEngine::samples_per_buffer frames */
			void process( const float *input, float *output, int input_channels, int output_channels, int sample_rate );

			static const string host_driver_name;

		private:

			std::atomic<int> m_sample_rate;
			std::atomic<int> m_input_channels;
			std::atomic<int> m_output_channels;
	};
}



#endif /* INTEGRA_HOST_AUDIO_ENGINE_H */
