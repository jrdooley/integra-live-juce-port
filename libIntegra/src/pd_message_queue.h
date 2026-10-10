/* libIntegra multimedia module interface
 *
 * Copyright (C) 2007 Birmingham City University
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
	Integra Live port: libpd 0.4x's C++ wrapper queued everything Pd sent out (prints,
	lists to subscribed receivers, MIDI) and handed it over with numMessages() /
	nextMessage() as pd::Message structs. libpd 0.16 dropped that in favour of receiver
	callbacks, so this restores the same message type and queue on top of them.

	Pd calls the receiver synchronously (PdBase is initialised unqueued), i.e. on the
	thread that is processing audio or sending to Pd. CDspEngine only does either while
	holding its mutex, and drains the queue under the same mutex, so no extra locking is
	needed here.
*/

#ifndef INTEGRA_PD_MESSAGE_QUEUE_H
#define INTEGRA_PD_MESSAGE_QUEUE_H

#include <deque>
#include <string>

#include "PdBase.hpp"


namespace pd
{
	/* the message types of libpd 0.4x's polling interface */
	enum MessageType
	{
		NONE,
		PRINT,
		BANG, FLOAT, SYMBOL, LIST, MESSAGE,
		NOTE_ON, CONTROL_CHANGE, PROGRAM_CHANGE, PITCH_BEND, AFTERTOUCH, POLY_AFTERTOUCH, BYTE
	};

	/* one message from Pd; only the fields of its type are set */
	struct Message
	{
		MessageType type = NONE;

		std::string dest;		// receiver name (events)
		float num = 0;			// FLOAT
		std::string symbol;		// SYMBOL value, PRINT text, MESSAGE selector
		pd::List list;			// LIST, MESSAGE

		int channel = 0, pitch = 0, velocity = 0, controller = 0, value = 0;	// MIDI
		int port = 0;
		unsigned char byte = 0;
	};
}


namespace integra_internal
{
	class CPdMessageQueue : public pd::PdReceiver, public pd::PdMidiReceiver
	{
		public:

			bool empty() const { return m_messages.empty(); }

			pd::Message pop()
			{
				pd::Message message = m_messages.front();
				m_messages.pop_front();
				return message;
			}

			void print( const std::string &text ) override
			{
				pd::Message &m = push( pd::PRINT );
				m.symbol = text;
			}

			void receiveBang( const std::string &dest ) override
			{
				push( pd::BANG ).dest = dest;
			}

			void receiveFloat( const std::string &dest, float num ) override
			{
				pd::Message &m = push( pd::FLOAT );
				m.dest = dest;
				m.num = num;
			}

			void receiveSymbol( const std::string &dest, const std::string &symbol ) override
			{
				pd::Message &m = push( pd::SYMBOL );
				m.dest = dest;
				m.symbol = symbol;
			}

			void receiveList( const std::string &dest, const pd::List &list ) override
			{
				pd::Message &m = push( pd::LIST );
				m.dest = dest;
				m.list = list;
			}

			void receiveMessage( const std::string &dest, const std::string &msg, const pd::List &list ) override
			{
				pd::Message &m = push( pd::MESSAGE );
				m.dest = dest;
				m.symbol = msg;
				m.list = list;
			}

			void receiveNoteOn( const int channel, const int pitch, const int velocity ) override
			{
				pd::Message &m = push( pd::NOTE_ON );
				m.channel = channel;
				m.pitch = pitch;
				m.velocity = velocity;
			}

			void receiveControlChange( const int channel, const int controller, const int value ) override
			{
				pd::Message &m = push( pd::CONTROL_CHANGE );
				m.channel = channel;
				m.controller = controller;
				m.value = value;
			}

			void receiveProgramChange( const int channel, const int value ) override
			{
				pd::Message &m = push( pd::PROGRAM_CHANGE );
				m.channel = channel;
				m.value = value;
			}

			void receivePitchBend( const int channel, const int value ) override
			{
				pd::Message &m = push( pd::PITCH_BEND );
				m.channel = channel;
				m.value = value;
			}

			void receiveAftertouch( const int channel, const int value ) override
			{
				pd::Message &m = push( pd::AFTERTOUCH );
				m.channel = channel;
				m.value = value;
			}

			void receivePolyAftertouch( const int channel, const int pitch, const int value ) override
			{
				pd::Message &m = push( pd::POLY_AFTERTOUCH );
				m.channel = channel;
				m.pitch = pitch;
				m.value = value;
			}

			void receiveMidiByte( const int port, const int byte ) override
			{
				pd::Message &m = push( pd::BYTE );
				m.port = port;
				m.byte = ( unsigned char ) byte;
			}

		private:

			/* libpd 0.4x's queue held at most this many, dropping the oldest */
			static const size_t max_messages = 1000;

			pd::Message &push( pd::MessageType type )
			{
				if( m_messages.size() >= max_messages )
				{
					m_messages.pop_front();
				}

				m_messages.emplace_back();
				m_messages.back().type = type;
				return m_messages.back();
			}

			std::deque<pd::Message> m_messages;
	};
}

#endif
