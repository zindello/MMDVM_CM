/*
 *   Copyright (C) 2015,2016,2017,2023 by Jonathan Naylor G4KLX
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

#include "USRPNetwork.h"
#include "StopWatch.h"
#include "Utils.h"
#include "Log.h"

#include <cstdio>

CUSRPNetwork::CUSRPNetwork(const std::string& address, uint16_t dstPort, uint16_t localPort, bool debug) :
m_addr(),
m_addrLen(0U),
m_socket(localPort),
m_debug(debug)
{
	if (CUDPSocket::lookup(address, dstPort, m_addr, m_addrLen) != 0)
		m_addrLen = 0U;

	CStopWatch stopWatch;
}

CUSRPNetwork::~CUSRPNetwork()
{
}

bool CUSRPNetwork::open()
{
	LogMessage("USRP Network, Opening");

	if (m_addrLen == 0U) {
		LogError("USRP Network, supplied address/port is invalid");
		return false;
	}	

	return m_socket.open(m_addr);
}

void CUSRPNetwork::close()
{
	m_socket.close();
}

uint32_t CUSRPNetwork::readData(uint8_t* data, uint32_t length)
{
	sockaddr_storage addr;
	unsigned int addrLen;
	int len = m_socket.read(data, length, addr, addrLen);
	if (len <= 0)
		return 0U;

	// Check if the data is for us
	if (!CUDPSocket::match(m_addr, addr)) {
		LogMessage("USRP packet received from an invalid source");
		return 0U;
	}

	if (m_debug)
		CUtils::dump(1U, "USRP Network Data Received", data, len);

	return len;
}

bool CUSRPNetwork::writeData(const uint8_t* data, uint32_t length)
{
	if (m_debug)
		CUtils::dump(1U, "USRP Network Data Sent", data, length);

	return m_socket.write(data, length, m_addr, m_addrLen);
}
