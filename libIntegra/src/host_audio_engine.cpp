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

#include "host_audio_engine.h"
#include "dsp_engine.h"


namespace integra_internal
{
	const string CHostAudioEngine::host_driver_name = "Host";


	CHostAudioEngine::CHostAudioEngine()
		:	m_sample_rate( 44100 ),
			m_input_channels( 2 ),
			m_output_channels( 2 )
	{
	}


	CHostAudioEngine::~CHostAudioEngine()
	{
	}


	CError CHostAudioEngine::set_driver( const string & )					{ return CError::SUCCESS; }
	CError CHostAudioEngine::set_input_device( const string & )				{ return CError::SUCCESS; }
	CError CHostAudioEngine::set_output_device( const string & )			{ return CError::SUCCESS; }
	CError CHostAudioEngine::set_sample_rate( int )							{ return CError::SUCCESS; }
	CError CHostAudioEngine::set_number_of_input_channels( int )			{ return CError::SUCCESS; }
	CError CHostAudioEngine::set_number_of_output_channels( int )			{ return CError::SUCCESS; }
	CError CHostAudioEngine::restore_defaults()								{ return CError::SUCCESS; }


	string_vector CHostAudioEngine::get_available_drivers() const			{ return string_vector( 1, host_driver_name ); }
	string_vector CHostAudioEngine::get_available_input_devices() const		{ return string_vector( 1, host_driver_name ); }
	string_vector CHostAudioEngine::get_available_output_devices() const	{ return string_vector( 1, host_driver_name ); }
	int_vector CHostAudioEngine::get_available_sample_rates() const			{ return int_vector( 1, m_sample_rate.load() ); }

	string CHostAudioEngine::get_selected_driver() const					{ return host_driver_name; }
	string CHostAudioEngine::get_selected_input_device() const				{ return host_driver_name; }
	string CHostAudioEngine::get_selected_output_device() const				{ return host_driver_name; }

	int CHostAudioEngine::get_sample_rate() const							{ return m_sample_rate; }
	int CHostAudioEngine::get_number_of_input_channels() const				{ return m_input_channels; }
	int CHostAudioEngine::get_number_of_output_channels() const				{ return m_output_channels; }


	void CHostAudioEngine::process( const float *input, float *output, int input_channels, int output_channels, int sample_rate )
	{
		m_sample_rate = sample_rate;
		m_input_channels = input_channels;
		m_output_channels = output_channels;

		get_dsp_engine().process_buffer( input, output, input_channels, output_channels, sample_rate );
	}
}
