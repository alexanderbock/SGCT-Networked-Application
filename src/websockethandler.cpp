﻿/*****************************************************************************************
 *                                                                                       *
 * WebSocket Handler                                                                     *
 *                                                                                       *
 * Copyright (c) Alexander Bock, 2020                                                    *
 *                                                                                       *
 * All rights reserved.                                                                  *
 *                                                                                       *
 * Redistribution and use in source and binary forms, with or without modification, are  *
 * permitted provided that the following conditions are met:                             *
 *                                                                                       *
 * 1. Redistributions of source code must retain the above copyright notice, this list   *
 *    of conditions and the following disclaimer.                                        *
 *                                                                                       *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this     *
 *    list of conditions and the following disclaimer in the documentation and/or other  *
 *    materials provided with the distribution.                                          *
 *                                                                                       *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY   *
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES  *
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT   *
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,        *
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  *
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR    *
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN      *
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN    *
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
 * DAMAGE.                                                                               *
 ****************************************************************************************/

#include "websockethandler.h"

#include "libwebsockets.h"
#include <assert.h>
#include <exception>
#include <mutex>
#include <string_view>
#include <vector>

/// Private implementation (=pimpl) of the WebSocketHandler to hide all details in here
struct WebSocketHandlerImpl {
    /// Address that we want to connect to
    std::string address;
    /// Port at which to connect
    int port = 0;

    /// Mutex that protects simulteanoues access to the messageQueue
    std::mutex messageMutex;
    /// The queued list of messages that will be sent one-by-one, whenever the sockets
    /// reports that it is ready to be written to
    std::vector<std::vector<std::byte>> messageQueue;

    /// The user's function pointer that is called when a connection is established
    std::function<void()> connectionEstablished;
    /// The user's function pointer that is called when the connection is terminated
    std::function<void()> connectionClosed;
    /// The user's function pointer that is called when a message was received, which
    /// includes the data of the message
    std::function<void(const void*, size_t)> messageReceived;

    /// The disconnect method sets this to \c true.  We can't disconnect the socket
    /// directly, but have to wait for a round-trip through the callback method, which
    /// needs to return -1 in order to signal to libwebsocket that it should close it.
    /// ¯\_(ツ)_/¯

    bool wantsToDisconnect = false;

    /// A pointer to the context that contains our protocol and connection
    lws_context* context = nullptr;
};

int callback(lws* wsi, lws_callback_reasons reason, void*, void* in, size_t len) {
    // We need this extra check as in some of the early callbacks the protocol doesn't
    // seem to be fully initialized and will return a nullptr.  But we don't handle any of
    // these early callbacks in our switch, so the pImpl being nullptr won't be a problem.
    //
    // If you find this message because your program crashed:  I'm sorry and please open
    // an issue on https://github.com/alexanderbock/SGCT-Networked-Application so that I
    // can fix this
    void* usr = lws_get_protocol(wsi) ? lws_get_protocol(wsi)->user : nullptr;
    WebSocketHandlerImpl* pImpl = reinterpret_cast<WebSocketHandlerImpl*>(usr);

    if (pImpl && pImpl->wantsToDisconnect) {
        pImpl->wantsToDisconnect = false;
        pImpl->context = nullptr;
        return -1;
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            assert(pImpl);
            pImpl->connectionEstablished();
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            assert(pImpl);
            pImpl->messageReceived(in, len);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            assert(pImpl);
            std::lock_guard lock(pImpl->messageMutex);
            if (pImpl->messageQueue.empty()) {
                break;
            }

            // Get a copy of first message from the queue.  Don't change this into a
            // reference or we will have a dangling reference after the following erase
            std::vector<std::byte> msg = pImpl->messageQueue.front();
            pImpl->messageQueue.erase(pImpl->messageQueue.begin());

            // Send the message
            lws_write(
                wsi,
                reinterpret_cast<unsigned char*>(msg.data()),
                msg.size(),
                LWS_WRITE_TEXT
            );
            break;
        }
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            assert(pImpl);
            pImpl->connectionClosed();
            pImpl->context = nullptr;
            return -1; // close the connection
        default:
            break;
    }

    return 0;
}

WebSocketHandler::WebSocketHandler(std::string address, int port,
                                   std::function<void()> connectionEstablished,
                                   std::function<void()> connectionClosed,
                                   std::function<void(const void*, size_t)> msgReceived)
{
    // Check whether there is a valid address
    assert(!address.empty());

    // Check whether the port is a valid (>0) number
    assert(port > 0);

    // Check whether the callbacks provided are non-empty
    assert(connectionEstablished);
    assert(msgReceived);

    // Create our private implementation
    _pImpl = std::make_unique<WebSocketHandlerImpl>();
    _pImpl->address = std::move(address);
    _pImpl->port = port;
    _pImpl->connectionEstablished = std::move(connectionEstablished);
    _pImpl->connectionClosed = std::move(connectionClosed);
    _pImpl->messageReceived = std::move(msgReceived);
}

WebSocketHandler::~WebSocketHandler() {
    disconnect();
    tick();
}

bool WebSocketHandler::connect(std::string protocolName, int bufferSize) {
    assert(bufferSize >= 0);

    lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;

    // This is a bit ugly;  we pass in the address to the callbacks structure so that we
    // can access it as the user pointer of the protocol inside the callback function
    // whenever something interesting happens in the websocket connection
    const size_t bufSize = static_cast<size_t>(bufferSize);
    const lws_protocols protocols[] = {
        { protocolName.c_str(), callback, 0, bufSize, 0, _pImpl.get() },
        { nullptr, nullptr, 0, 0, 0, nullptr } // terminal value
    };

    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    _pImpl->context = lws_create_context(&info);
    lws_client_connect_info ccinfo;
    std::memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = _pImpl->context;
    ccinfo.address = _pImpl->address.c_str();
    ccinfo.port = _pImpl->port;
    ccinfo.path = "/";
    ccinfo.host = lws_canonical_hostname(_pImpl->context);
    ccinfo.origin = "origin";
    ccinfo.protocol = protocols[0].name;

    lws* connection = lws_client_connect_via_info(&ccinfo);
    return connection != nullptr;
}

void WebSocketHandler::disconnect() {
    _pImpl->wantsToDisconnect = true;
}

void WebSocketHandler::tick() {
    lws_service(_pImpl->context, 0);
}

void WebSocketHandler::queueMessage(std::vector<std::byte> message) {
    std::lock_guard lock(_pImpl->messageMutex);
    _pImpl->messageQueue.push_back(std::move(message));
}

int WebSocketHandler::queueSize() const {
    std::lock_guard lock(_pImpl->messageMutex);
    return static_cast<int>(_pImpl->messageQueue.size());
}
