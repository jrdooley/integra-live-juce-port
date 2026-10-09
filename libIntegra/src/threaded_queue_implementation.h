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



#ifndef INTEGRA_THREADED_QUEUE_IMPLEMENTATION_PRIVATE
#define INTEGRA_THREADED_QUEUE_IMPLEMENTATION_PRIVATE


#include "assert.h"
#include "api/trace.h"
#include "api/error.h"

using namespace integra_api;

namespace integra_internal
{
	template<class T> CThreadedQueue<T>::CThreadedQueue( IThreadedQueueOutputSink<T> &output_sink )
		:	m_output_sink( output_sink )
	{
		m_content = new std::list<T>;
		m_finished = false;

		m_output_thread = std::thread( [ this ] { output_thread(); } );
	}


	template<class T> CThreadedQueue<T>::~CThreadedQueue()
	{
		{
			std::lock_guard<std::mutex> lock( m_queue_mutex );
			m_finished = true;
		}

		send_signal_to_output_thread();

		m_output_thread.join();

		assert( m_content );
		assert( m_content->empty() );
		delete m_content;
	}


	template<class T> void CThreadedQueue<T>::push( const T &item )
	{
		{
			std::lock_guard<std::mutex> lock( m_queue_mutex );
			assert( m_content );
			m_content->push_back( item );
		}

		send_signal_to_output_thread();
	}


	template<class T> void CThreadedQueue<T>::push( const std::list<T> &items )
	{
		{
			std::lock_guard<std::mutex> lock( m_queue_mutex );
			assert( m_content );
			m_content->insert( m_content->end(), items.begin(), items.end() );
		}

		send_signal_to_output_thread();
	}


	template<class T> void CThreadedQueue<T>::send_signal_to_output_thread()
	{
		m_semaphore.post();
	}


	template<class T> void CThreadedQueue<T>::output_thread()
	{
		bool finished( false );
		while( !finished )
		{
			m_semaphore.wait();

			std::list<T> *content_to_deliver = NULL;
			{
				std::lock_guard<std::mutex> lock( m_queue_mutex );

				assert( m_content );
				if( !m_content->empty() )
				{
					content_to_deliver = m_content;
					m_content = new std::list<T>;
				}

				if( m_finished ) 
				{
					finished = true;
				}
			}

			if( content_to_deliver )
			{
				m_output_sink.handle_queue_items( *content_to_deliver );
				delete content_to_deliver;
			}
		}
	}
}


#endif /* INTEGRA_THREADED_QUEUE_IMPLEMENTATION_PRIVATE */
