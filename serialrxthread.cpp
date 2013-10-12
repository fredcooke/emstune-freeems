/************************************************************************************
 * EMStudio - Open Source ECU tuning software                                       *
 * Copyright (C) 2013  Michael Carpenter (malcom2073@gmail.com)                     *
 *                                                                                  *
 * This file is a part of EMStudio                                                  *
 *                                                                                  *
 * EMStudio is free software; you can redistribute it and/or                        *
 * modify it under the terms of the GNU Lesser General Public                       *
 * License as published by the Free Software Foundation, version                    *
 * 2.1 of the License.                                                              *
 *                                                                                  *
 * EMStudio is distributed in the hope that it will be useful,                      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU                *
 * Lesser General Public License for more details.                                  *
 *                                                                                  *
 * You should have received a copy of the GNU Lesser General Public                 *
 * License along with this program; if not, write to the Free Software              *
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA   *
 ************************************************************************************/

#include "serialrxthread.h"
#include <QDateTime>
#include "QsLog.h"

SerialRXThread::SerialRXThread(QObject *parent) : QThread(parent)
{
	m_terminate = false;
}
void SerialRXThread::start(SerialPort *serialport)
{
	m_serialPort = serialport;
	QThread::start();
}
SerialRXThread::~SerialRXThread()
{
	m_terminate = true;
	this->terminate();
	this->wait(1000);
}
QByteArray SerialRXThread::readSinglePacket(SerialPort *port)
{

	unsigned char buf;
	int len = 0;
	bool m_inpacket = false;
	bool m_inescape = false;
	QByteArray qbuffer;
	int attempts = 0; //This allows for a 2 second timeout.
	while (attempts <= 50)
	{
		len = port->readBytes(&buf,1);
		if (len < 0)
		{
			QLOG_DEBUG() << "Timeout";
		}
		else if (len == 0)
		{
			//This should be an error
			QLOG_DEBUG() << "Nothing to read:" << attempts;
			msleep(10); //Need a sleep now, due to select timeout.
			attempts++;
			continue;
		}
		else
		{
			if (buf == 0xAA)
			{
				if (m_inpacket)
				{
					//Start byte in the middle of a packet
					//Clear out the buffer and start fresh
					m_inescape = false;
					qbuffer.clear();
					QLOG_DEBUG() << "Buffer error";
				}
				//Start of packet
				m_inpacket = true;
			}
			else if (buf == 0xCC && m_inpacket)
			{
				//End of packet
				m_inpacket = false;

				//New Location of checksum
				unsigned char sum = 0;
				for (int i=0;i<qbuffer.size()-1;i++)
				{
					sum += qbuffer[i];
				}

				if (sum != (unsigned char)qbuffer[qbuffer.size()-1])
				{
					QLOG_DEBUG() << "BAD CHECKSUM!";
					qbuffer.clear();
				}
				else
				{
					return qbuffer.mid(0,qbuffer.length()-1);
				}
			}
			else
			{
				if (m_inpacket && !m_inescape)
				{
					if (buf == 0xBB)
					{
						//Need to escape the next byte
						m_inescape = true;
					}
					else
					{
						qbuffer.append(buf);
					}

				}
				else if (m_inpacket && m_inescape)
				{
					if (buf == 0x55)
					{
						qbuffer.append((char)0xAA);
					}
					else if (buf == 0x44)
					{
						qbuffer.append((char)0xBB);
					}
					else if (buf == 0x33)
					{
						qbuffer.append((char)0xCC);
					}
					else
					{
						QLOG_DEBUG() << "Error, escaped character is not valid!:" << QString::number(buf,16);
					}
					m_inescape = false;
				}
			}
		}
	}
	return QByteArray();
}

void SerialRXThread::run()
{
	QString byteoutofpacket;
	QByteArray qbuffer;
	unsigned char buffer[10240];
	bool m_inpacket = false;
	bool m_inescape = false;
	int m_packetErrorCount=0;
	int readlen=0;
	while (!m_terminate)
	{
		readlen = m_serialPort->readBytes(buffer,1024);
		if (readlen < 0)
		{
			//Nothing on the port
			QLOG_DEBUG() << "Timeout";
		}
		if (readlen == 0)
		{
			//This should be an error
			msleep(10); //Need a sleep now, due to select timeout.
			continue;
		}
		for (int i=0;i<readlen;i++)
		{
			if (buffer[i] == 0xAA)
			{
				if (m_inpacket)
				{
					//Start byte in the middle of a packet
					//Clear out the buffer and start fresh
					m_inescape = false;
					QByteArray bufToEmit;
					bufToEmit.append(0xAA);
					QByteArray nbuffer(qbuffer);
					nbuffer.replace(0xBB,QByteArray().append(0xBB).append(0x44));
					nbuffer.replace(0xAA,QByteArray().append(0xBB).append(0x55));
					nbuffer.replace(0xCC,QByteArray().append(0xBB).append(0x33));
					bufToEmit.append(nbuffer);
					bufToEmit.append(0xCC);
					emit dataRead(bufToEmit);
					qbuffer.clear();
					QLOG_DEBUG() << "Buffer error";
					m_packetErrorCount++;
				}
				//Start of packet
				m_inpacket = true;
			}
			else if (buffer[i] == 0xCC && m_inpacket)
			{
				//End of packet
				m_inpacket = false;

				//New Location of checksum
				unsigned char sum = 0;
				for (int i=0;i<qbuffer.size()-1;i++)
				{
					sum += qbuffer[i];
				}
				QByteArray bufToEmit;
				bufToEmit.append(0xAA);
				QByteArray nbuffer(qbuffer);
				nbuffer.replace(0xBB,QByteArray().append(0xBB).append(0x44));
				nbuffer.replace(0xAA,QByteArray().append(0xBB).append(0x55));
				nbuffer.replace(0xCC,QByteArray().append(0xBB).append(0x33));
				bufToEmit.append(nbuffer);
				bufToEmit.append(0xCC);
				emit dataRead(bufToEmit);
				if (sum != (unsigned char)qbuffer[qbuffer.size()-1])
				{
					QLOG_DEBUG() << "BAD CHECKSUM!";
					m_packetErrorCount++;
				}
				else
				{
					m_packetErrorCount=0;
					emit incomingPacket(qbuffer.mid(0,qbuffer.length()-1));
				}
				//return qbuffer;
				QString output;
				for (int i=0;i<qbuffer.size();i++)
				{
					int num = (unsigned char)qbuffer[i];
					output.append(" ").append((num < 0xF) ? "0" : "").append(QString::number(num,16));
				}
				qbuffer.clear();
			}
			else
			{
				if (m_inpacket && !m_inescape)
				{
					if (buffer[i] == 0xBB)
					{
						//Need to escape the next byte
						m_inescape = true;
					}
					else
					{
						qbuffer.append(buffer[i]);
					}

				}
				else if (m_inpacket && m_inescape)
				{
					if (buffer[i] == 0x55)
					{
						qbuffer.append((char)0xAA);
					}
					else if (buffer[i] == 0x44)
					{
						qbuffer.append((char)0xBB);
					}
					else if (buffer[i] == 0x33)
					{
						qbuffer.append((char)0xCC);
					}
					else
					{
						QLOG_DEBUG() << "Error, escaped character is not valid!:" << QString::number(buffer[i],16);
						m_packetErrorCount++;
					}
					m_inescape = false;
				}
				else
				{
					byteoutofpacket += QString::number(buffer[i],16) + " ";
				}
			}
		}
	}
	return;
}
