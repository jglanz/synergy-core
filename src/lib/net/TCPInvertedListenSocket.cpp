/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2002 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "net/TCPInvertedListenSocket.h"

#include "net/NetworkAddress.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocket.h"
#include "net/TSocketMultiplexerMethodJob.h"
#include "net/XSocket.h"
#include "io/XIO.h"
#include "mt/Lock.h"
#include "mt/Mutex.h"
#include "arch/Arch.h"
#include "arch/XArch.h"
#include "base/Log.h"
#include "base/IEventQueue.h"

#include <iostream>
#include <memory>
//
// TCPInvertedListenSocket
//

TCPInvertedListenSocket::TCPInvertedListenSocket(IEventQueue* events, SocketMultiplexer* socketMultiplexer, IArchNetwork::EAddressFamily family) :
    m_events(events),
    m_socketMultiplexer(socketMultiplexer)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_mutex = new Mutex;
    try {
        m_socket = ARCH->newSocket(family, IArchNetwork::kSTREAM);
    }
    catch (XArchNetwork& e) {
        throw XSocketCreate(e.what());
    }
}

TCPInvertedListenSocket::~TCPInvertedListenSocket()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    try {
        if (m_socket != NULL) {
            m_socketMultiplexer->removeSocket(this);
            ARCH->closeSocket(m_socket);
        }
    }
    catch (...) {
        // ignore
        LOG((CLOG_WARN "error while closing TCP socket"));
    }
    delete m_mutex;
}

void
TCPInvertedListenSocket::bind(const NetworkAddress& addr)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    try {
        Lock lock(m_mutex);
        connect();
        ARCH->setReuseAddrOnSocket(m_socket, true);
        ARCH->bindSocket(m_socket, addr.getAddress());
        ARCH->listenOnSocket(m_socket);
        m_socketMultiplexer->addSocket(this,
                            new TSocketMultiplexerMethodJob<TCPInvertedListenSocket>(
                                this, &TCPInvertedListenSocket::serviceListening,
                                m_socket, true, false));
    }
    catch (XArchNetworkAddressInUse& e) {
        throw XSocketAddressInUse(e.what());
    }
    catch (XArchNetwork& e) {
        throw XSocketBind(e.what());
    }
}

void
TCPInvertedListenSocket::close()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(m_mutex);
    if (m_socket == NULL) {
        throw XIOClosed();
    }
    try {
        m_socketMultiplexer->removeSocket(this);
        ARCH->closeSocket(m_socket);
        m_socket = NULL;
    }
    catch (XArchNetwork& e) {
        throw XSocketIOClose(e.what());
    }
}

void*
TCPInvertedListenSocket::getEventTarget() const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    return const_cast<void*>(static_cast<const void*>(this));
}

IDataSocket*
TCPInvertedListenSocket::accept()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    IDataSocket* socket = NULL;
    try {
        socket = new TCPSocket(m_events, m_socketMultiplexer, ARCH->acceptSocket(m_socket, NULL));
        if (socket != NULL) {
            setListeningJob();
        }
        return socket;
    }
    catch (XArchNetwork&) {
        if (socket != NULL) {
            delete socket;
            setListeningJob();
        }
        return NULL;
    }
    catch (std::exception &ex) {
        if (socket != NULL) {
            delete socket;
            setListeningJob();
        }
        throw ex;
    }
}

void
TCPInvertedListenSocket::connect()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_client = std::make_unique<TCPSocket>(m_events, m_socketMultiplexer, IArchNetwork::EAddressFamily::kINET);
    NetworkAddress address("192.168.1.191", 24000);
    address.resolve();
    m_client->connect(address);
}

void
TCPInvertedListenSocket::setListeningJob()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_socketMultiplexer->addSocket(this,
                            new TSocketMultiplexerMethodJob<TCPInvertedListenSocket>(
                                this, &TCPInvertedListenSocket::serviceListening,
                                m_socket, true, false));
}

ISocketMultiplexerJob*
TCPInvertedListenSocket::serviceListening(ISocketMultiplexerJob* job,
                            bool read, bool, bool error)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    if (error) {
        close();
        return NULL;
    }
    if (read) {
        m_events->addEvent(Event(m_events->forIListenSocket().connecting(), this));
        // stop polling on this socket until the client accepts
        return NULL;
    }
    return job;
}
