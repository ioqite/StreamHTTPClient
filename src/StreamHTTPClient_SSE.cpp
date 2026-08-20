/*
  StreamHTTPClient_SSE.cpp - Server-Sent Events implementation
*/

#include "StreamHTTPClient_SSE.h"

StreamHTTPClient_SSEImpl::StreamHTTPClient_SSEImpl(StreamHTTPClient* parent)
    : _parent(parent),
      _connected(false),
      _retry_ms(3000),
      _next_reconnect_at(0) {
}

StreamHTTPClient_SSEImpl::~StreamHTTPClient_SSEImpl() {
    stop();
}

void StreamHTTPClient_SSEImpl::resetEvent() {
    _event_name = "";
    _event_data = "";
    _event_id = "";
}

bool StreamHTTPClient_SSEImpl::connect() {
    _last_event_id = "";
    _next_reconnect_at = 0;
    return doConnect();
}

bool StreamHTTPClient_SSEImpl::doConnect() {
    // Configure parent for SSE
    _parent->useStreamMode(true);
    _parent->setDecompress(false);          // we parse raw text
    _parent->setFollowRedirects(SHC_REDIRECT_FOLLOW_GET_ONLY);
    _parent->addHeader("Accept", "text/event-stream", false, true);
    _parent->addHeader("Cache-Control", "no-cache", false, true);
    if (_last_event_id.length()) {
        _parent->addHeader("Last-Event-ID", _last_event_id, false, true);
    }

    int code = _parent->GET();
    if (code <= 0) {
        if (_parent->_on_sse_error) _parent->_on_sse_error(code ? code : SHC_ERROR_CONNECTION_REFUSED);
        _connected = false;
        _next_reconnect_at = millis() + _retry_ms;
        return false;
    }
    if (code != 200) {
        if (_parent->_on_sse_error) _parent->_on_sse_error(code);
        _connected = false;
        _parent->end();
        return false;
    }
    _connected = true;
    _line_buf = "";
    resetEvent();
    if (_parent->_on_sse_connect) _parent->_on_sse_connect();
    return true;
}

void StreamHTTPClient_SSEImpl::stop() {
    _connected = false;
    _parent->end();
}

void StreamHTTPClient_SSEImpl::handleLine(const String& line) {
    if (line.length() == 0) {
        // Blank line - dispatch event
        if (_event_data.length() > 0 || _event_name.length() > 0 || _event_id.length() > 0) {
            // Remove trailing \n from data
            if (_event_data.length() > 0 &&
                _event_data.charAt(_event_data.length() - 1) == '\n') {
                _event_data.remove(_event_data.length() - 1);
            }
            SSEEvent ev;
            ev.event = _event_name.length() ? _event_name : String("message");
            ev.data  = _event_data;
            ev.id    = _event_id;
            ev.retry = _retry_ms;
            if (_event_id.length() > 0) {
                _last_event_id = _event_id;
            }
            if (_parent->_on_event) _parent->_on_event(ev);
        }
        resetEvent();
        return;
    }
    if (line.charAt(0) == ':') {
        // Comment - ignore
        return;
    }
    int colon = line.indexOf(':');
    String field, value;
    if (colon < 0) {
        field = line;
        value = "";
    } else {
        field = line.substring(0, colon);
        value = line.substring(colon + 1);
        // Per spec, a single leading space is stripped
        if (value.length() > 0 && value.charAt(0) == ' ') {
            value.remove(0, 1);
        }
    }
    if (field == "event") {
        _event_name = value;
    } else if (field == "data") {
        _event_data += value;
        _event_data += '\n';
    } else if (field == "id") {
        _event_id = value;
    } else if (field == "retry") {
        long r = value.toInt();
        if (r > 0) _retry_ms = (uint32_t)r;
    }
}

void StreamHTTPClient_SSEImpl::dispatchEvent() {
    // no-op: handleLine already dispatches on blank line
}

bool StreamHTTPClient_SSEImpl::poll() {
    if (!_connected) {
        // Try reconnect if it's time
        if (_next_reconnect_at != 0 && (int32_t)(millis() - _next_reconnect_at) >= 0) {
            _next_reconnect_at = 0;
            doConnect();
        }
        return _connected;
    }

    WiFiClient* c = _parent->getStreamPtr();
    if (!c || !c->connected()) {
        _connected = false;
        if (c) c->stop();
        _next_reconnect_at = millis() + _retry_ms;
        return false;
    }

    // Read whatever is available, line-buffered
    while (c->available() > 0) {
        int b = c->read();
        if (b < 0) break;
        if (b == '\r') continue;
        if (b == '\n') {
            handleLine(_line_buf);
            _line_buf = "";
        } else {
            if (_line_buf.length() < SHC_SSE_LINE_BUFFER) {
                _line_buf += (char)b;
            }
        }
    }
    return _connected;
}
