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
