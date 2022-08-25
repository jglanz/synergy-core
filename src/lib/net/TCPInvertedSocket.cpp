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

#include "net/TCPInvertedSocket.h"
#include "net/TCPListenSocket.h"

#include "net/NetworkAddress.h"
#include "net/SocketMultiplexer.h"
#include "net/TSocketMultiplexerMethodJob.h"
#include "net/XSocket.h"
#include "mt/Lock.h"
#include "arch/Arch.h"
#include "arch/XArch.h"
#include "base/Log.h"
#include "base/IEventQueue.h"
#include "base/IEventJob.h"
#include "base/TMethodEventJob.h"

#include <cstring>
#include <cstdlib>
#include <memory>
#include <utility>
#include <iostream>

//
// TCPInvertedSocket
//

TCPInvertedSocket::TCPInvertedSocket(IEventQueue* events, SocketMultiplexer* socketMultiplexer, IArchNetwork::EAddressFamily family) :
    IDataSocket(events),
    m_events(events),
    m_mutex(),
    m_flushed(&m_mutex, true),
    m_socketMultiplexer(socketMultiplexer)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    try {
        m_socket = ARCH->newSocket(family, IArchNetwork::kSTREAM);
        LOG((CLOG_DEBUG "Opening new socket: %08X", m_socket));
        ARCH->setNoDelayOnSocket(m_socket, true);

        m_listener = std::make_unique<TCPListenSocket>(m_events, m_socketMultiplexer, family);
    }
    catch (const XArchNetwork& e) {
        throw XSocketCreate(e.what());
    }
}

TCPInvertedSocket::~TCPInvertedSocket()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    try {
        // warning virtual function in destructor is very danger practice
        close();
    }
    catch (...) {
        LOG((CLOG_DEBUG "error while TCP socket destruction"));
    }
}

void
TCPInvertedSocket::bind(const NetworkAddress& addr)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // setup event handler
    auto handler = new TMethodEventJob<TCPInvertedSocket>(this, &TCPInvertedSocket::handleConnecting);
    m_events->adoptHandler(m_events->forIListenSocket().connecting(), m_listener.get(), handler);
    m_listener->bind(addr);
}

void
TCPInvertedSocket::close()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    //LOG((CLOG_DEBUG "Closing socket: %08X", m_socket));

    // remove ourself from the multiplexer
    setJob(nullptr);

    Lock lock(&m_mutex);

    // clear buffers and enter disconnected state
    if (m_connected) {
        sendEvent(m_events->forISocket().disconnected());
    }
    onDisconnected();

    // close the socket
    /*if (m_socket != nullptr) {
        ArchSocket socket = m_socket;
        m_socket = nullptr;
        try {
            ARCH->closeSocket(socket);
        }
        catch (const XArchNetwork& e) {
            // ignore, there's not much we can do
            LOG((CLOG_WARN "error closing socket: %s", e.what()));
        }
    }*/

    if (m_acceptedSocket) {
        m_acceptedSocket->close();
    }

    if (m_listener) {
        m_listener->close();
    }
}

void*
TCPInvertedSocket::getEventTarget() const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    return const_cast<void*>(static_cast<const void*>(this));
}

UInt32
TCPInvertedSocket::read(void* buffer, UInt32 n)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // copy data directly from our input buffer
    Lock lock(&m_mutex);
    UInt32 size = m_inputBuffer.getSize();
    if (n > size) {
        n = size;
    }
    if (buffer != nullptr && n != 0) {
        memcpy(buffer, m_inputBuffer.peek(n), n);
    }
    m_inputBuffer.pop(n);

    // if no more data and we cannot read or write then send disconnected
    if (n > 0 && m_inputBuffer.getSize() == 0 && !m_readable && !m_writable) {
        sendEvent(m_events->forISocket().disconnected());
        m_connected = false;
    }

    return n;
}

void
TCPInvertedSocket::write(const void* buffer, UInt32 n)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    bool wasEmpty;
    {
        Lock lock(&m_mutex);

        // must not have shutdown output
        if (!m_writable) {
            sendEvent(m_events->forIStream().outputError());
            return;
        }

        // ignore empty writes
        if (n == 0) {
            return;
        }

        // copy data to the output buffer
        wasEmpty = (m_outputBuffer.getSize() == 0);
        m_outputBuffer.write(buffer, n);

        // there's data to write
        m_flushed = false;
    }

    // make sure we're waiting to write
    if (wasEmpty) {
        setJob(newJob());
    }
}

void
TCPInvertedSocket::flush()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(&m_mutex);
    while (m_flushed == false) {
        m_flushed.wait();
    }
}

void
TCPInvertedSocket::shutdownInput()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_acceptedSocket->shutdownInput();
    /*bool useNewJob = false;
    {
        Lock lock(&m_mutex);


        // shutdown socket for reading
        try {
            ARCH->closeSocketForRead(m_socket);
        }
        catch (const XArchNetwork& e) {
            // ignore, there's not much we can do
            LOG((CLOG_WARN "error closing socket: %s", e.what()));
        }

        // shutdown buffer for reading
        if (m_readable) {
            sendEvent(m_events->forIStream().inputShutdown());
            onInputShutdown();
            useNewJob = true;
        }
    }
    if (useNewJob) {
        setJob(newJob());
    }*/
}

void
TCPInvertedSocket::shutdownOutput()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_acceptedSocket->shutdownOutput();
    /*bool useNewJob = false;
    {
        Lock lock(&m_mutex);

        // shutdown socket for writing
        try {
            ARCH->closeSocketForWrite(m_socket);
        }
        catch (const XArchNetwork& e) {
            // ignore, there's not much we can do
            LOG((CLOG_WARN "error closing socket: %s", e.what()));
        }

        // shutdown buffer for writing
        if (m_writable) {
            sendEvent(m_events->forIStream().outputShutdown());
            onOutputShutdown();
            useNewJob = true;
        }
    }
    if (useNewJob) {
        setJob(newJob());
    }*/
}

bool
TCPInvertedSocket::isReady() const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(&m_mutex);
    return (m_inputBuffer.getSize() > 0);
}

bool
TCPInvertedSocket::isFatal() const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // TCP sockets aren't ever left in a fatal state.
    LOG((CLOG_ERR "isFatal() not valid for non-secure connections"));
    return false;
}

UInt32
TCPInvertedSocket::getSize() const
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(&m_mutex);
    return m_inputBuffer.getSize();
}

void
TCPInvertedSocket::connect(const NetworkAddress& addr)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    bind(24000);
    /*{
        Lock lock(&m_mutex);

        // fail on attempts to reconnect
        if (m_socket == nullptr || m_connected) {
            sendConnectionFailedEvent("busy");
            return;
        }

        try {

            if (ARCH->connectSocket(m_socket, addr.getAddress())) {
                sendEvent(m_events->forIDataSocket().connected());
                onConnected();
            }
            else {
                // connection is in progress
                m_writable = true;
            }
        }
        catch (const XArchNetwork& e) {
            throw XSocketConnect(e.what());
        }
    }
    setJob(newJob());*/
}

void
TCPInvertedSocket::handleConnecting(const Event&, void*)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;

    // accept client connection
    IDataSocket* socket = m_listener->accept();

    if (socket == NULL) {
        std::cout<<"SGADTRACE: Fail to accept connection"<<std::endl;
        return;
    } else {
        m_acceptedSocket.reset(socket);
        m_socket = m_acceptedSocket->getRawSocket();
        std::cout<<"SGADTRACE: Accepted connection"<<std::endl;
    }
}

TCPInvertedSocket::EJobResult
TCPInvertedSocket::doRead()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    UInt8 buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    size_t bytesRead = 0;

    bytesRead = ARCH->readSocket(m_socket, buffer, sizeof(buffer));

    if (bytesRead > 0) {
        bool wasEmpty = (m_inputBuffer.getSize() == 0);

        // slurp up as much as possible
        do {
            m_inputBuffer.write(buffer, static_cast<UInt32>(bytesRead));

            bytesRead = ARCH->readSocket(m_socket, buffer, sizeof(buffer));
        } while (bytesRead > 0);

        // send input ready if input buffer was empty
        if (wasEmpty) {
            sendEvent(m_events->forIStream().inputReady());
        }
    }
    else {
        // remote write end of stream hungup.  our input side
        // has therefore shutdown but don't flush our buffer
        // since there's still data to be read.
        sendEvent(m_events->forIStream().inputShutdown());
        if (!m_writable && m_inputBuffer.getSize() == 0) {
            sendEvent(m_events->forISocket().disconnected());
            m_connected = false;
        }
        m_readable = false;
        return kNew;
    }

    return kRetry;
}

TCPInvertedSocket::EJobResult
TCPInvertedSocket::doWrite()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // write data
    UInt32 bufferSize = 0;
    int bytesWrote = 0;

    bufferSize = m_outputBuffer.getSize();
    const void* buffer = m_outputBuffer.peek(bufferSize);
    bytesWrote = (UInt32)ARCH->writeSocket(m_socket, buffer, bufferSize);

    if (bytesWrote > 0) {
        discardWrittenData(bytesWrote);
        return kNew;
    }

    return kRetry;
}

void
TCPInvertedSocket::setJob(ISocketMultiplexerJob* job)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // multiplexer will delete the old job
    if (job == nullptr) {
        m_socketMultiplexer->removeSocket(this);
    }
    else {
        m_socketMultiplexer->addSocket(this, job);
    }
}

ISocketMultiplexerJob*
TCPInvertedSocket::newJob()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // note -- must have m_mutex locked on entry

    if (m_socket == nullptr) {
        return nullptr;
    }
    else if (!m_connected) {
        assert(!m_readable);
        if (!(m_readable || m_writable)) {
            return nullptr;
        }
        return new TSocketMultiplexerMethodJob<TCPInvertedSocket>(
                                this, &TCPInvertedSocket::serviceConnecting,
                                m_socket, m_readable, m_writable);
    }
    else {
        if (!(m_readable || (m_writable && (m_outputBuffer.getSize() > 0)))) {
            return nullptr;
        }
        return new TSocketMultiplexerMethodJob<TCPInvertedSocket>(
                                this, &TCPInvertedSocket::serviceConnected,
                                m_socket, m_readable,
                                m_writable && (m_outputBuffer.getSize() > 0));
    }
}

void
TCPInvertedSocket::sendConnectionFailedEvent(const char* msg)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    ConnectionFailedInfo* info = new ConnectionFailedInfo(msg);
    m_events->addEvent(Event(m_events->forIDataSocket().connectionFailed(),
                            getEventTarget(), info, Event::kDontFreeData));
}

void
TCPInvertedSocket::sendEvent(Event::Type type)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_events->addEvent(Event(type, getEventTarget()));
}

void
TCPInvertedSocket::discardWrittenData(int bytesWrote)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_outputBuffer.pop(bytesWrote);
    if (m_outputBuffer.getSize() == 0) {
        sendEvent(m_events->forIStream().outputFlushed());
        m_flushed = true;
        m_flushed.broadcast();
    }
}

void
TCPInvertedSocket::onConnected()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_connected = true;
    m_readable  = true;
    m_writable  = true;
}

void
TCPInvertedSocket::onInputShutdown()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_inputBuffer.pop(m_inputBuffer.getSize());
    m_readable = false;
}

void
TCPInvertedSocket::onOutputShutdown()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    m_outputBuffer.pop(m_outputBuffer.getSize());
    m_writable = false;

    // we're now flushed
    m_flushed = true;
    m_flushed.broadcast();
}

void
TCPInvertedSocket::onDisconnected()
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    // disconnected
    onInputShutdown();
    onOutputShutdown();
    m_connected = false;
}

ISocketMultiplexerJob*
TCPInvertedSocket::serviceConnecting(ISocketMultiplexerJob* job,
                bool, bool write, bool error)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(&m_mutex);

    // should only check for errors if error is true but checking a new
    // socket (and a socket that's connecting should be new) for errors
    // should be safe and Mac OS X appears to have a bug where a
    // non-blocking stream socket that fails to connect immediately is
    // reported by select as being writable (i.e. connected) even when
    // the connection has failed.  this is easily demonstrated on OS X
    // 10.3.4 by starting a synergy client and telling to connect to
    // another system that's not running a synergy server.  it will
    // claim to have connected then quickly disconnect (i guess because
    // read returns 0 bytes).  unfortunately, synergy attempts to
    // reconnect immediately, the process repeats and we end up
    // spinning the CPU.  luckily, OS X does set SO_ERROR on the
    // socket correctly when the connection has failed so checking for
    // errors works.  (curiously, sometimes OS X doesn't report
    // connection refused.  when that happens it at least doesn't
    // report the socket as being writable so synergy is able to time
    // out the attempt.)
    if (error || true) {
        try {
            // connection may have failed or succeeded
            ARCH->throwErrorOnSocket(m_socket);
        }
        catch (const XArchNetwork& e) {
            sendConnectionFailedEvent(e.what());
            onDisconnected();
            return newJob();
        }
    }

    if (write) {
        sendEvent(m_events->forIDataSocket().connected());
        onConnected();
        return newJob();
    }

    return job;
}

ISocketMultiplexerJob*
TCPInvertedSocket::serviceConnected(ISocketMultiplexerJob* job,
                bool read, bool write, bool error)
{
    std::cout<<"SGADTRACE: "<<__FUNCTION__<<std::endl;
    Lock lock(&m_mutex);

    if (error) {
        sendEvent(m_events->forISocket().disconnected());
        onDisconnected();
        return newJob();
    }

    EJobResult result = kRetry;
    if (write) {
        try {
            result = doWrite();
        }
        catch (XArchNetworkShutdown&) {
            // remote read end of stream hungup.  our output side
            // has therefore shutdown.
            onOutputShutdown();
            sendEvent(m_events->forIStream().outputShutdown());
            if (!m_readable && m_inputBuffer.getSize() == 0) {
                sendEvent(m_events->forISocket().disconnected());
                m_connected = false;
            }
            result = kNew;
        }
        catch (XArchNetworkDisconnected&) {
            // stream hungup
            onDisconnected();
            sendEvent(m_events->forISocket().disconnected());
            result = kNew;
        }
        catch (XArchNetwork& e) {
            // other write error
            LOG((CLOG_WARN "error writing socket: %s", e.what()));
            onDisconnected();
            sendEvent(m_events->forIStream().outputError());
            sendEvent(m_events->forISocket().disconnected());
            result = kNew;
        }
    }

    if (read && m_readable) {
        try {
            result = doRead();
        }
        catch (XArchNetworkDisconnected&) {
            // stream hungup
            sendEvent(m_events->forISocket().disconnected());
            onDisconnected();
            result = kNew;
        }
        catch (XArchNetwork& e) {
            // ignore other read error
            LOG((CLOG_WARN "error reading socket: %s", e.what()));
        }
    }

    if (result == kBreak) {
        return nullptr;
    }

    return result == kNew ? newJob() : job;
}

ArchSocket TCPInvertedSocket::getRawSocket() const
{
    return m_socket;
}
