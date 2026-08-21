/**************************************************************************
 * Copyright (c) 2026 Ioqit
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 **************************************************************************/

/*
  StreamHTTPClient_SSE.h - Server-Sent Events support

  SSE is RFC 8895 (formerly HTML5). The server keeps an HTTP connection open
  and emits UTF-8 text events separated by blank lines. Each event is a set
  of "field: value" lines, where field is one of:
    event, data, id, retry
  Lines starting with ":" are comments and are ignored.

  This implementation:
    * Opens a long-lived HTTP/1.1 GET request with Accept: text/event-stream
    * Disables decompression (we want raw bytes for the line parser)
    * Disables redirect following on the parent (we handle it ourselves)
    * Reconnects automatically with Last-Event-ID header if the connection drops
    * Honours the "retry:" field for backoff
*/

#pragma once

#include "StreamHTTPClient.h"

class StreamHTTPClient_SSEImpl {
public:
    StreamHTTPClient_SSEImpl(StreamHTTPClient* parent);
    ~StreamHTTPClient_SSEImpl();

    bool connect();
    void stop();
    bool poll();
    bool connected() const { return _connected; }

private:
    StreamHTTPClient* _parent;
    bool _connected;
    String _line_buf;           // accumulates bytes until \n
    String _event_name;
    String _event_data;
    String _event_id;
    uint32_t _retry_ms;

    String _last_event_id;      // last id we received, sent back on reconnect
    uint32_t _next_reconnect_at; // millis() when we may reconnect

    void resetEvent();
    void dispatchEvent();
    void handleLine(const String& line);
    bool doConnect();
};
