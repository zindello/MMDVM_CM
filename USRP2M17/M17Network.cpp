/*
 *   Copyright (C) 2009-2014,2016,2023 by Jonathan Naylor G4KLX
 *   Copyright (C) 2021 by Doug McLain AD8DP
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "M17Network.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>
#include <cassert>
#include <cstring>


CM17Network::CM17Network(const std::string& address, uint16_t dstPort, uint16_t localPort, uint8_t* callsign, bool debug) :
m_addr(),
m_addrLen(0U),
m_socket(localPort),
m_debug(debug),
m_callsign()
{
	memcpy(m_callsign, callsign, 6U);

	if (CUDPSocket::lookup(address, dstPort, m_addr, m_addrLen) != 0U)
		m_addrLen = 0U;
}

CM17Network::~CM17Network()
{
}

bool CM17Network::open()
{
	LogInfo("Opening M17 network connection");

	if (m_addrLen == 0U) {
		LogError("M17 Network, supplied address/port is invalid");
		return false;
	}	

	return m_socket.open(m_addr);
}

bool CM17Network::writeData(const unsigned char* data, unsigned int length)
{
	assert(data != NULL);
	assert(length > 0U);

	if (m_debug)
		CUtils::dump(1U, "M17 Network Data Sent", data, length);

	return m_socket.write(data, length, m_addr, m_addrLen);
}

bool CM17Network::writePoll()
{
	unsigned char data[10U];
	
	memcpy(data, "PONG", 4U);
	memcpy(data + 4U, m_callsign, 6U);

	if (m_debug)
		CUtils::dump(1U, "M17 Network Pong Sent", data, 10U);

	return m_socket.write(data, 10U, m_addr, m_addrLen);
}

bool CM17Network::writeLink(char m)
{
	unsigned char data[11U];
	
	memcpy(data, "CONN", 4U);
	memcpy(data+4, m_callsign, 6U);
	data[10U] = m;

	if (m_debug)
		CUtils::dump(1U, "M17 Network Link Sent", data, 11U);

	return m_socket.write(data, 11U, m_addr, m_addrLen);
}

bool CM17Network::writeUnlink()
{
	unsigned char data[10U];
	
	memcpy(data, "DISC", 4U);
	memcpy(data + 4U, m_callsign, 6U);

	if (m_debug)
		CUtils::dump(1U, "M17 Network Unlink Sent", data, 10U);

	return m_socket.write(data, 10U, m_addr, m_addrLen);
}

unsigned int CM17Network::readData(unsigned char* data, unsigned int length)
{
	assert(data != NULL);
	assert(length > 0U);

	sockaddr_storage addr;
	unsigned int addrLen;
	int len = m_socket.read(data, length, addr, addrLen);
	if (len <= 0)
		return 0U;

	// Check if the data is for us
	if (!CUDPSocket::match(m_addr, addr)) {
		LogMessage("M17 packet received from an invalid source");
		return 0U;
	}

	if (m_debug)
		CUtils::dump(1U, "M17 Network Data Received", data, len);

	return len;
}

void CM17Network::close()
{
	m_socket.close();

	LogInfo("Closing M17 network connection");
}
