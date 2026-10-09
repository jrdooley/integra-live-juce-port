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



#ifndef INTEGRA_THREADED_QUEUE_PRIVATE
#define INTEGRA_THREADED_QUEUE_PRIVATE


#include "semaphore.h"

#include <mutex>
#include <thread>
#include <list>

namespace integra_internal
{
	template<class T> class IThreadedQueueOutputSink
	{
		public:

			virtual void handle_queue_items( const std::list<T> &items ) = 0;
	};



	template<class T> class CThreadedQueue
	{
		public:

			CThreadedQueue( IThreadedQueueOutputSink<T> &output_sink );
			~CThreadedQueue();

			void push( const T&item );
			void push( const std::list<T> &items );

		private:

			void send_signal_to_output_thread();

			void output_thread();

			IThreadedQueueOutputSink<T> &m_output_sink;

			std::list<T> *m_content;

			std::mutex m_queue_mutex;
			CSemaphore m_semaphore;
			std::thread m_output_thread;

			bool m_finished;
	};

}


#include "threaded_queue_implementation.h"


#endif /*INTEGRA_THREADED_QUEUE_PRIVATE*/
