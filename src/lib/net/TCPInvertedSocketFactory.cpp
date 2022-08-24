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

#include "net/TCPInvertedSocketFactory.h"
#include "net/TCPInvertedSocket.h"
#include "net/TCPInvertedListenSocket.h"
#include "net/SecureSocket.h"
#include "net/SecureListenSocket.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include <iostream>

//
// TCPInvertedSocketFactory
//

TCPInvertedSocketFactory::TCPInvertedSocketFactory(IEventQueue* events, SocketMultiplexer* socketMultiplexer) :
    m_events(events),
    m_socketMultiplexer(socketMultiplexer)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // do nothing
}

IDataSocket*
TCPInvertedSocketFactory::create(bool secure, IArchNetwork::EAddressFamily family) const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    if (secure) {
        SecureSocket* secureSocket = new SecureSocket(m_events, m_socketMultiplexer, family);
        secureSocket->initSsl (false);
        return secureSocket;
    }
    else {
        return new TCPInvertedSocket(m_events, m_socketMultiplexer, family);
    }
}

IListenSocket*
TCPInvertedSocketFactory::createListen(bool secure, IArchNetwork::EAddressFamily family) const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    IListenSocket* socket = NULL;
    if (secure) {
        socket = new SecureListenSocket(m_events, m_socketMultiplexer, family);
    }
    else {
        socket = new TCPInvertedListenSocket(m_events, m_socketMultiplexer, family);
    }

    return socket;
}
