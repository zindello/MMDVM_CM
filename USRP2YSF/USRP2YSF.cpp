/* 
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
*   Copyright (C) 2018 by Andy Uribe CA6JAU
* 	Copyright (C) 2020 by Doug McLain AD8DP
*   Copyright (C) 2022 by Dave Behnke AC8ZD
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

#include "USRP2YSF.h"
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>

#define YSF_FRAME_PER       90U
#define USRP_FRAME_PER     15U

const char* DEFAULT_INI_FILE = "/etc/USRP2YSF.ini";

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2022 by AD8DP, CA6JAU, G4KLX, AC8ZD and others";

#define M17CHARACTERS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/."

#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <cctype>

int end = 0;

void sig_handler(int signo)
{
	if (signo == SIGTERM) {
		end = 1;
		::fprintf(stdout, "Received SIGTERM\n");
	}
}

//trim is necessary over usrp, especially USRP2M17, since people put wonky
//calls in their radio like AC8ZD/DAVE. By default, callsigns coming in from YSF
//are 10 characters and padded with spaces if callsign isn't that long.
//to make it extra M17 friendly, we ensure the callsign is no longer than 8 characters.
std::string trim_callsign(const std::string s) {
    const std::string ACCEPTABLECHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t start = s.find_first_not_of(ACCEPTABLECHARS);
    if (start > 8) {
        start = 8;
    }
    return s.substr(0, start);
}

//pad is necessary for the reverse back to ysf, the callsign needs to be 10 characters
//spaces need to be placed if all 10 characters not used.
void pad_callsign(std::string &str, const size_t num, const char paddingChar = ' ')
{
    if(num > str.size())
        str.insert(str.size(), num - str.size(), paddingChar);
}

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;
	if (argc > 1) {
		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "USRP2YSF version %s\n", VERSION);
				return 0;
			} else if (arg.substr(0, 1) == "-") {
				::fprintf(stderr, "Usage: USRP2YSF [-v|--version] [filename]\n");
				return 1;
			} else {
				iniFile = argv[currentArg];
			}
		}
	}

	// Capture SIGTERM to finish gracelessly
	if (signal(SIGTERM, sig_handler) == SIG_ERR) 
		::fprintf(stdout, "Can't catch SIGTERM\n");

	CUSRP2YSF* gateway = new CUSRP2YSF(std::string(iniFile));

	int ret = gateway->run();

	delete gateway;

	return ret;
}

CUSRP2YSF::CUSRP2YSF(const std::string& configFile) :
m_callsign(),
m_usrpcs(),
m_conf(configFile),
m_conv(),
m_usrpFrame(NULL),
m_usrpFrames(0U)
{
	m_usrpFrame = new uint8_t[400U];
	m_ysfFrame  = new unsigned char[200U];

	::memset(m_usrpFrame, 0U, 400U);
	::memset(m_ysfFrame, 0U, 200U);
}

CUSRP2YSF::~CUSRP2YSF()
{
	delete[] m_usrpFrame;
	delete[] m_ysfFrame;
}

int CUSRP2YSF::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "USRP2YSF: cannot read the .ini file\n");
		return 1;
	}

	setlocale(LC_ALL, "C");

	unsigned int logDisplayLevel = m_conf.getLogDisplayLevel();

	if(m_conf.getDaemon())
		logDisplayLevel = 0U;

	bool m_daemon = m_conf.getDaemon();
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return -1;
		} else if (pid != 0)
			exit(EXIT_SUCCESS);

		// Create new session and process group
		if (::setsid() == -1) {
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return -1;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1) {
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return -1;
		}

		// If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return -1;
			}

			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			// Set user and group ID's to mmdvm:mmdvm
			if (setgid(mmdvm_gid) != 0) {
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return -1;
			}

			if (setuid(mmdvm_uid) != 0) {
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return -1;
			}

			// Double check it worked (AKA Paranoia) 
			if (setuid(0) != -1) {
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return -1;
			}
		}
	}

	ret = ::LogInitialise(m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), logDisplayLevel);
	if (!ret) {
		::fprintf(stderr, "M172YSF: unable to open the log file\n");
		return 1;
	}

	if (m_daemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
		::close(STDERR_FILENO);
	}

	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);

	m_callsign = m_conf.getCallsign();
	bool debug = m_conf.getDebug();
	m_conv.setUSRPGainAdjDb(m_conf.getUSRPGainAdjDb());
	m_conv.setYSFGainAdjDb(m_conf.getYSFGainAdjDb());
	
	std::string usrp_address      = m_conf.getUSRPAddress();
	uint16_t usrp_dstPort     = m_conf.getUSRPDstPort();
	uint16_t usrp_localPort   = m_conf.getUSRPLocalPort();
	
	m_usrpNetwork = new CUSRPNetwork(usrp_address, usrp_dstPort, usrp_localPort, debug);
	
	ret = m_usrpNetwork->open();
	if (!ret) {
		::LogError("Cannot open the USRP network port");
		::LogFinalise();
		return 1;
	}
	
	in_addr ysf_dstAddress       = CUDPSocket::lookup(m_conf.getYSFDstAddress());
	unsigned int ysf_dstPort     = m_conf.getYSFDstPort();
	std::string ysf_localAddress = m_conf.getYSFLocalAddress();
	unsigned int ysf_localPort   = m_conf.getYSFLocalPort();

	m_ysfNetwork = new CYSFNetwork(ysf_localAddress, ysf_localPort, m_callsign, debug);
	m_ysfNetwork->setDestination(ysf_dstAddress, ysf_dstPort);

	ret = m_ysfNetwork->open();
	if (!ret) {
		::LogError("Cannot open the YSF network port");
		::LogFinalise();
		return 1;
	}
	
	CTimer networkWatchdog(100U, 0U, 1500U);
	CTimer pollTimer(1000U, 5U);

	CStopWatch stopWatch;
	CStopWatch usrpWatch;
	CStopWatch ysfWatch;
	stopWatch.start();
	usrpWatch.start();
	ysfWatch.start();
	pollTimer.start();

	uint32_t usrp_cnt = 0;
	unsigned char ysf_cnt = 0;

	m_ysfNetwork->writePoll();
	 
	LogMessage("Starting USRP2YSF-%s", VERSION);

	for (; end == 0;) {
		unsigned char buffer[2000U];
		unsigned int ms = stopWatch.elapsed();
		
		uint32_t len = 0;
		while ( (len = m_usrpNetwork->readData(m_usrpFrame, 400)) ) {
			if( !memcmp(m_usrpFrame, "USRP", 4) && (len == 32) ) {
				LogMessage("USRP received end of voice transmission, %.1f seconds", float(m_usrpFrames) / 50.0F);
				m_conv.putUSRPEOT();
				m_usrpcs.clear();
				m_usrpFrames = 0U;
			}

			if( (!memcmp(m_usrpFrame, "USRP", 4)) && len == 352) {
				if( (m_usrpFrame[20] == USRP_TYPE_TEXT) && (m_usrpFrame[32] == TLV_TAG_SET_INFO) ){
					m_usrpcs = (char *)(m_usrpFrame + 46);
					// pad to 10 for ysf
					pad_callsign(m_usrpcs, 10);

					if (!m_usrpFrames)
					{
						m_conv.putUSRPHeader();
						LogMessage("USRP text info received as first frame callsign=\"%s\" (%lu bytes)", m_usrpcs.c_str(), m_usrpcs.length());
					}
					m_usrpFrames++;
				}
				else if( (m_usrpFrame[20] == USRP_TYPE_VOICE) && (m_usrpFrame[15] == USRP_KEYUP_TRUE) ){
					if(!m_usrpFrames){
						//m_usrpcs.clear();
						m_conv.putUSRPHeader();
						LogMessage("USRP voice received as first frame");
					}
					int16_t pcm[160];
					for(int i = 0; i < 160; ++i){
						pcm[i] = (m_usrpFrame[32+(i*2)+1] << 8) | m_usrpFrame[32+(i*2)];
					}
					m_conv.putUSRP(pcm);
					m_usrpFrames++;
				}
			}
		}
		
		while (m_ysfNetwork->read(buffer) > 0U) {
			CYSFFICH fich;
			bool valid = fich.decode(buffer + 35U);

			if (valid) {
				unsigned char fi = fich.getFI();
				unsigned char dt = fich.getDT();
				//unsigned char fn = fich.getFN();
				//unsigned char ft = fich.getFT();

				if (::memcmp(buffer, "YSFD", 4U) == 0U) {
					//processWiresX(buffer + 35U, fi, dt, fn, ft);

					if (dt == YSF_DT_VD_MODE2) {
						CYSFPayload ysfPayload;

						if (fi == YSF_FI_HEADER) {
							if (ysfPayload.processHeaderData(buffer + 35U)) {
								std::string ysfSrcRaw = ysfPayload.getSource();
								std::string ysfSrc = trim_callsign(ysfSrcRaw);
								std::string ysfDst = ysfPayload.getDest();
								LogMessage("Received YSF Header: RawSrc: \"%s\" Src: \"%s\" Dst: \"%s\"", ysfSrcRaw.c_str(), ysfSrc.c_str(), ysfDst.c_str());
								m_conv.putYSFHeader();
								m_usrpcs = ysfSrc;
							}
						} else if (fi == YSF_FI_TERMINATOR) {
							LogMessage("YSF received end of voice transmission");
							m_conv.putYSFEOT();
						} else if (fi == YSF_FI_COMMUNICATIONS) {
							m_conv.putYSF(buffer + 35U);
						}
					}
				}
			}
		}

		if (usrpWatch.elapsed() > USRP_FRAME_PER) {
			int16_t pcm[160];
			uint32_t usrpFrameType = m_conv.getUSRP(pcm);
			
			if(usrpFrameType == TAG_USRP_HEADER){
				//CUtils::dump(1U, "USRP data:", m_usrpFrame, 33U);

				const uint32_t cnt = htonl(usrp_cnt);
				memset(m_usrpFrame, 0, 352);
				memcpy(m_usrpFrame, "USRP", 4);
				memcpy(m_usrpFrame+4, &cnt, 4);
				m_usrpFrame[15] = USRP_KEYUP_FALSE;
				m_usrpFrame[20] = USRP_TYPE_TEXT;
				m_usrpFrame[32] = TLV_TAG_SET_INFO;
				m_usrpFrame[33] = 13 + m_usrpcs.size();
				
				memcpy(m_usrpFrame+46, m_usrpcs.c_str(), m_usrpcs.size());
				
				m_usrpNetwork->writeData(m_usrpFrame, 352);
				usrp_cnt++;
				usrpWatch.start();
			}
			
			if(usrpFrameType == TAG_USRP_EOT){
				//CUtils::dump(1U, "USRP data:", m_usrpFrame, 33U);
				const uint32_t cnt = htonl(usrp_cnt);
				memcpy(m_usrpFrame, "USRP", 4);
				memset(m_usrpFrame+4, 0, 28);
				memcpy(m_usrpFrame+4, &cnt, 4);
				m_usrpFrame[15] = USRP_KEYUP_FALSE;
				
				m_usrpNetwork->writeData(m_usrpFrame, 32);
				usrp_cnt++;
				usrpWatch.start();
			}
			
			if(usrpFrameType == TAG_USRP_DATA){
				//CUtils::dump(1U, "USRP data:", m_usrpFrame, 33U);
				const uint32_t cnt = htonl(usrp_cnt);
				memcpy(m_usrpFrame, "USRP", 4);
				memset(m_usrpFrame+4, 0, 28);
				memcpy(m_usrpFrame+4, &cnt, 4);
				m_usrpFrame[15] = USRP_KEYUP_TRUE;
				
				for(int i = 0; i < 320; i+=2){
					m_usrpFrame[32+i] = pcm[(i/2)] & 0xff;
					m_usrpFrame[32+i+1] = pcm[(i/2)] >> 8;
				}
				
				m_usrpNetwork->writeData(m_usrpFrame, 352);
				usrp_cnt++;
				usrpWatch.start();
			}
		}
		
		if (ysfWatch.elapsed() > YSF_FRAME_PER) {
			unsigned int ysfFrameType = m_conv.getYSF(m_ysfFrame + 35U);

			//fprintf(stderr, "type:ms %d:%d\n", ysfFrameType, ysfWatch.elapsed());
			
			if(ysfFrameType == TAG_HEADER) {
				ysf_cnt = 0U;

				::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
				::memcpy(m_ysfFrame + 4U, m_ysfNetwork->getCallsign().c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 14U, m_usrpcs.c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
				m_ysfFrame[34U] = 0U; // Net frame counter

				::memcpy(m_ysfFrame + 35U, YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);

				// Set the FICH
				CYSFFICH fich;
				fich.setFI(YSF_FI_HEADER);
				fich.setCS(m_conf.getFICHCallSign());
 				fich.setCM(m_conf.getFICHCallMode());
 				fich.setBN(0U);
 				fich.setBT(0U);
				fich.setFN(0U);
				fich.setFT(m_conf.getFICHFrameTotal());
				fich.setDev(0U);
				fich.setMR(m_conf.getFICHMessageRoute());
 				fich.setVoIP(m_conf.getFICHVOIP());
 				fich.setDT(m_conf.getFICHDataType());
 				fich.setSQL(m_conf.getFICHSQLType());
 				fich.setSQ(m_conf.getFICHSQLCode());
				fich.encode(m_ysfFrame + 35U);

				unsigned char csd1[20U], csd2[20U];
				memset(csd1, '*', YSF_CALLSIGN_LENGTH);
 				memset(csd1, '*', YSF_CALLSIGN_LENGTH/2);
 				memcpy(csd1 + YSF_CALLSIGN_LENGTH/2, m_conf.getYsfRadioID().c_str(), YSF_CALLSIGN_LENGTH/2);
				memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_usrpcs.c_str(), YSF_CALLSIGN_LENGTH);
				memset(csd2, ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);

				CYSFPayload payload;
				payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);

				m_ysfNetwork->write(m_ysfFrame);

				ysf_cnt++;
				//ysfWatch.start();
			}
			else if (ysfFrameType == TAG_EOT) {

				::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
				::memcpy(m_ysfFrame + 4U, m_ysfNetwork->getCallsign().c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 14U, m_usrpcs.c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);
				m_ysfFrame[34U] = ysf_cnt; // Net frame counter

				::memcpy(m_ysfFrame + 35U, YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);

				// Set the FICH
				CYSFFICH fich;
				fich.setFI(YSF_FI_TERMINATOR);
				fich.setCS(m_conf.getFICHCallSign());
 				fich.setCM(m_conf.getFICHCallMode());
 				fich.setBN(0U);
 				fich.setBT(0U);
 				fich.setFN(0U);
				fich.setFT(m_conf.getFICHFrameTotal());
 				fich.setDev(0U);
				fich.setMR(m_conf.getFICHMessageRoute());
 				fich.setVoIP(m_conf.getFICHVOIP());
 				fich.setDT(m_conf.getFICHDataType());
 				fich.setSQL(m_conf.getFICHSQLType());
 				fich.setSQ(m_conf.getFICHSQLCode());
				fich.encode(m_ysfFrame + 35U);

				unsigned char csd1[20U], csd2[20U];
				memset(csd1, '*', YSF_CALLSIGN_LENGTH/2);
 				memcpy(csd1 + YSF_CALLSIGN_LENGTH/2, m_conf.getYsfRadioID().c_str(), YSF_CALLSIGN_LENGTH/2);
				memcpy(csd1 + YSF_CALLSIGN_LENGTH, m_usrpcs.c_str(), YSF_CALLSIGN_LENGTH);
				memset(csd2, ' ', YSF_CALLSIGN_LENGTH + YSF_CALLSIGN_LENGTH);

				CYSFPayload payload;
				payload.writeHeader(m_ysfFrame + 35U, csd1, csd2);

				m_ysfNetwork->write(m_ysfFrame);
			}
			else if (ysfFrameType == TAG_DATA) {

				CYSFFICH fich;
				CYSFPayload ysfPayload;
				unsigned char dch[10U];

				unsigned int fn = (ysf_cnt - 1U) % (m_conf.getFICHFrameTotal() + 1);

				::memcpy(m_ysfFrame + 0U, "YSFD", 4U);
				::memcpy(m_ysfFrame + 4U, m_ysfNetwork->getCallsign().c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 14U, m_usrpcs.c_str(), YSF_CALLSIGN_LENGTH);
				::memcpy(m_ysfFrame + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);

				::memcpy(m_ysfFrame + 35U, YSF_SYNC_BYTES, YSF_SYNC_LENGTH_BYTES);

				switch (fn) {
					case 0:
						memset(dch, '*', YSF_CALLSIGN_LENGTH/2);
 						memcpy(dch + YSF_CALLSIGN_LENGTH/2, m_conf.getYsfRadioID().c_str(), YSF_CALLSIGN_LENGTH/2);
 						ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, dch);
						break;
					case 1:
						ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (unsigned char*)m_usrpcs.c_str());
						break;
					case 2:
						ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (unsigned char*)m_usrpcs.c_str());
						break;
					case 5:
						memset(dch, ' ', YSF_CALLSIGN_LENGTH/2);
 						memcpy(dch + YSF_CALLSIGN_LENGTH/2, m_conf.getYsfRadioID().c_str(), YSF_CALLSIGN_LENGTH/2);
 						ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, dch);	// Rem3/4
 						break;
					case 6: {
							unsigned char dt1[10U] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
							for (unsigned int i = 0U; i < m_conf.getYsfDT1().size() && i < 10U; i++)
								dt1[i] = m_conf.getYsfDT1()[i];
							ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, dt1);
						}
						break;
					case 7: {
							unsigned char dt2[10U] = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
							for (unsigned int i = 0U; i < m_conf.getYsfDT2().size() && i < 10U; i++)
								dt2[i] = m_conf.getYsfDT2()[i];
							ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, dt2);
						}
						break;
					default:
						ysfPayload.writeVDMode2Data(m_ysfFrame + 35U, (const unsigned char*)"          ");
				}

				// Set the FICH
				fich.setFI(YSF_FI_COMMUNICATIONS);
				fich.setCS(m_conf.getFICHCallSign());
 				fich.setCM(m_conf.getFICHCallMode());
 				fich.setBN(0U);
 				fich.setBT(0U);
 				fich.setFN(fn);
				fich.setFT(m_conf.getFICHFrameTotal());
				fich.setDev(0U);
				fich.setMR(m_conf.getFICHMessageRoute());
 				fich.setVoIP(m_conf.getFICHVOIP());
 				fich.setDT(m_conf.getFICHDataType());
 				fich.setSQL(m_conf.getFICHSQLType());
 				fich.setSQ(m_conf.getFICHSQLCode());
				fich.encode(m_ysfFrame + 35U);

				// Net frame counter
				m_ysfFrame[34U] = (ysf_cnt & 0x7FU) << 1;

				// Send data
				m_ysfNetwork->write(m_ysfFrame);

				ysf_cnt++;
				//ysfWatch.start();
			}
			ysfWatch.start();
		}

		stopWatch.start();
		m_ysfNetwork->clock(ms);
		pollTimer.clock(ms);
		
		if (pollTimer.isRunning() && pollTimer.hasExpired()) {
			m_ysfNetwork->writePoll();
			pollTimer.start();
		}

		if (ms < 5U) ::usleep(5 * 1000);
	}

	m_usrpNetwork->close();
	m_ysfNetwork->close();
	delete m_ysfNetwork;
	delete m_usrpNetwork;

	::LogFinalise();

	return 0;
}

