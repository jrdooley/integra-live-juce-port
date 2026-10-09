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


#ifndef INTEGRA_SEMAPHORE_PRIVATE_H
#define INTEGRA_SEMAPHORE_PRIVATE_H

#include <condition_variable>
#include <mutex>


namespace integra_internal
{
	/*
	 Portable counting semaphore.  Replaces the POSIX sem_t (Linux), dispatch_semaphore_t
	 (macOS) and pthreads-win32 semaphores libIntegra used to need per platform.
	 (std::counting_semaphore would do, but is C++20.)
	*/
	class CSemaphore
	{
		public:

			void post()
			{
				{
					std::lock_guard<std::mutex> lock( m_mutex );
					m_count++;
				}
				m_condition.notify_one();
			}

			void wait()
			{
				std::unique_lock<std::mutex> lock( m_mutex );
				m_condition.wait( lock, [ this ] { return m_count > 0; } );
				m_count--;
			}

			bool try_wait()
			{
				std::lock_guard<std::mutex> lock( m_mutex );
				if( m_count == 0 ) return false;
				m_count--;
				return true;
			}

		private:

			std::mutex m_mutex;
			std::condition_variable m_condition;
			unsigned int m_count = 0;
	};
}


#endif /* INTEGRA_SEMAPHORE_PRIVATE_H */
