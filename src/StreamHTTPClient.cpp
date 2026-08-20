/*
  StreamHTTPClient.cpp - main implementation

  The sync path drives a small state machine inline. The async path delegates
  to StreamHTTPClient_AsyncImpl which either spawns a FreeRTOS task (ESP32) or
  is driven cooperatively by the caller via asyncPoll().
*/

#include "StreamHTTPClient.h"
#include "StreamHTTPClient_Async.h"
#include "StreamHTTPClient_SSE.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers - keep these file-local (static)
// ---------------------------------------------------------------------------
namespace {

// Case-insensitive string compare for Arduino String
bool str_iequals(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); i++) {
        if (tolower(a[i]) != tolower(b[i])) return false;
    }
    return true;
}

bool str_iequals(const String& a, const char* b) {
    size_t n = a.length();
    for (size_t i = 0; i < n; i++) {
        if (b[i] == 0) return false;
        if (tolower(a[i]) != tolower((uint8_t)b[i])) return false;
    }
    return b[n] == 0;
}

// Read one CRLF-terminated line from a Client. Returns:
//   >=0  line length (without CRLF)
//   -1   EOF / disconnect
//   -2   line too long for buf
int read_line(Client* c, char* buf, size_t buf_size, uint32_t timeout_ms) {
    if (buf_size == 0) return -2;
    size_t pos = 0;
    uint32_t start = millis();
    while (pos < buf_size - 1) {
        int b = c->read();
        if (b < 0) {
            if (c->connected() && (millis() - start) < timeout_ms) {
                // Yield and retry - bytes may still arrive
                delay(1);
                continue;
            }
            // Connection lost or timeout
            if (pos == 0) return -1;
            break;
        }
        if (b == '\r') {
            // Expect \n
            int b2 = -1;
            uint32_t s2 = millis();
            while ((b2 = c->read()) < 0) {
                if (!c->connected() || (millis() - s2) >= timeout_ms) break;
                delay(1);
            }
            (void)b2; // tolerate missing LF
            buf[pos] = 0;
            return (int)pos;
        }
        if (b == '\n') {
            buf[pos] = 0;
            return (int)pos;
        }
        buf[pos++] = (char)b;
    }
    buf[buf_size - 1] = 0;
    return -2;
}

// Base64 encoding for Basic Auth
const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t* data, size_t len) {
    String out;
    out.reserve(((len + 2) / 3) * 4 + 1);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i+1] << 8 | data[i+2];
        out += b64_table[(v >> 18) & 0x3F];
        out += b64_table[(v >> 12) & 0x3F];
        out += b64_table[(v >> 6)  & 0x3F];
        out += b64_table[ v        & 0x3F];
    }
    if (i < len) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i+1] << 8;
        out += b64_table[(v >> 18) & 0x3F];
        out += b64_table[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// shc_method_to_string / shc_error_to_string
// ---------------------------------------------------------------------------
const char* shc_method_to_string(StreamHTTPClient_Method m) {
    switch (m) {
        case SHC_HTTP_METHOD_GET:     return "GET";
        case SHC_HTTP_METHOD_POST:    return "POST";
        case SHC_HTTP_METHOD_PUT:     return "PUT";
        case SHC_HTTP_METHOD_PATCH:   return "PATCH";
        case SHC_HTTP_METHOD_DELETE:  return "DELETE";
        case SHC_HTTP_METHOD_HEAD:    return "HEAD";
        case SHC_HTTP_METHOD_OPTIONS: return "OPTIONS";
    }
    return "GET";
}

String shc_error_to_string(int err) {
    switch (err) {
        case SHC_ERROR_CONNECTION_REFUSED:    return F("connection refused");
        case SHC_ERROR_SEND_HEADER_FAILED:    return F("send header failed");
        case SHC_ERROR_SEND_PAYLOAD_FAILED:   return F("send payload failed");
        case SHC_ERROR_NO_HTTP_SERVER:        return F("no HTTP server");
        case SHC_ERROR_CONNECTION_LOST:       return F("connection lost");
        case SHC_ERROR_NO_DATA:               return F("no data");
        case SHC_ERROR_READ_RESPONSE:         return F("read response failed");
        case SHC_ERROR_TOO_LESS_RAM:          return F("not enough RAM");
        case SHC_ERROR_STREAM_WRITE:          return F("stream write error");
        case SHC_ERROR_CHUNK_SIZE:            return F("invalid chunk size");
        case SHC_ERROR_DECOMPRESSION:         return F("decompression failed");
        case SHC_ERROR_SHA_LENGTH:            return F("SHA length mismatch");
        case SHC_ERROR_CACERT:                return F("TLS certificate rejected");
        case SHC_ERROR_HTTP_CODE_LATER:       return F("response not yet received");
        case SHC_ERROR_REDIRECT_EXCEEDED:     return F("redirect limit exceeded");
        case SHC_ERROR_INVALID_URL:           return F("invalid URL");
        case SHC_ERROR_TASK_CREATE:           return F("async task create failed");
        case SHC_ERROR_ALREADY_RUNNING:       return F("async already running");
        case SHC_ERROR_USER_CANCEL:           return F("cancelled by user");
        case SHC_ERROR_UNSUPPORTED_SCHEME:    return F("unsupported scheme");
        case SHC_ERROR_UNKNOWN:               return F("unknown error");
        default:                              return F("unknown error");
    }
}

String StreamHTTPClient::errorToString(int err) {
    return shc_error_to_string(err);
}

// ===========================================================================
// Lifecycle
// ===========================================================================
StreamHTTPClient::StreamHTTPClient()
    : _port(80),
      _https(false),
      _header_count(0),
      _collect_count(0),
      _collected_count(0),
      _client(nullptr),
      _owns_client(false),
      _status_code(0),
      _content_length(0),
      _body_received(0),
      _body_sent(0),
      _response_flags(0),
      _user_agent(F(SHC_DEFAULT_USER_AGENT)),
      _connect_timeout(SHC_DEFAULT_CONNECT_TIMEOUT),
      _tcp_timeout(SHC_DEFAULT_TCP_TIMEOUT),
      _redirect_policy(SHC_REDIRECT_FOLLOW_GET_ONLY),
      _redirect_limit(SHC_MAX_REDIRECTS),
      _redirect_count(0),
      _use_http10(false),
      _stream_mode(false),
      _decompress(true),
      _accept_encoding("gzip, deflate"),
      _keep_alive_pending(false),
      _tcp_keepalive_idle(0),
      _tcp_keepalive_intv(0),
      _tcp_keepalive_cnt(0),
      _tcp_keepalive_set(false),
      _body_buf(nullptr),
      _body_buf_size(0),
      _body_buf_len(0),
      _external_string_buf(nullptr),
      _decomp(nullptr),
      _async(nullptr),
      _sse(nullptr),
      _on_data(nullptr),
      _on_complete(nullptr),
      _on_error(nullptr),
      _on_header(nullptr),
      _on_progress(nullptr),
      _on_event(nullptr),
      _on_sse_connect(nullptr),
      _on_sse_error(nullptr),
      _request_in_progress(false),
      _cancelled(false),
      _request_start_ms(0),
      _last_err(0) {
}

StreamHTTPClient::~StreamHTTPClient() {
    end();
    if (_body_buf) {
        free(_body_buf);
        _body_buf = nullptr;
    }
}

void StreamHTTPClient::resetForNewRequest() {
    _status_code = 0;
    _content_length = 0;
    _body_received = 0;
    _body_sent = 0;
    _response_flags = 0;
    _location = "";
    _body_buf_len = 0;
    _collected_count = 0;
    _content_encoding = "";
    _cancelled = false;
    _request_start_ms = millis();
    releaseDecompressor();
}

bool StreamHTTPClient::parseURL(const String& url) {
    // url must look like: scheme://[user:pass@]host[:port]/path?query
    int scheme_end = url.indexOf("://");
    if (scheme_end < 0) {
        _last_err = SHC_ERROR_INVALID_URL;
        return false;
    }
    String scheme = url.substring(0, scheme_end);
    scheme.toLowerCase();
    if (scheme == "https") {
        _https = true;
        _port = 443;
    } else if (scheme == "http") {
        _https = false;
        _port = 80;
    } else {
        _last_err = SHC_ERROR_UNSUPPORTED_SCHEME;
        return false;
    }

    String rest = url.substring(scheme_end + 3);

    // userinfo
    int at = rest.indexOf('@');
    if (at >= 0) {
        String userinfo = rest.substring(0, at);
        rest = rest.substring(at + 1);
        int colon = userinfo.indexOf(':');
        if (colon >= 0) {
            _user = userinfo.substring(0, colon);
            _password = userinfo.substring(colon + 1);
        } else {
            _user = userinfo;
            _password = "";
        }
    }

    // path
    int slash = rest.indexOf('/');
    String hostport;
    if (slash >= 0) {
        hostport = rest.substring(0, slash);
        _uri = rest.substring(slash);
    } else {
        hostport = rest;
        _uri = "/";
    }

    // host:port
    int colon = hostport.indexOf(':');
    if (colon >= 0) {
        _host = hostport.substring(0, colon);
        _port = hostport.substring(colon + 1).toInt();
    } else {
        _host = hostport;
    }

    if (_host.length() == 0) {
        _last_err = SHC_ERROR_INVALID_URL;
        return false;
    }
    return true;
}

bool StreamHTTPClient::begin(const String& url) {
    end();
    if (!parseURL(url)) {
        return false;
    }
    _owns_client = true;
    return true;
}

bool StreamHTTPClient::begin(const String& host, uint16_t port, const String& uri) {
    end();
    _host = host;
    _port = port;
    _uri  = uri.length() ? uri : String("/");
    _https = (port == 443);
    _owns_client = true;
    return true;
}

bool StreamHTTPClient::begin(WiFiClient* client, const String& url) {
    end();
    if (!parseURL(url)) {
        return false;
    }
    _client = client;
    _owns_client = false;
    return true;
}

void StreamHTTPClient::end() {
    if (_async) {
        _async->stop();
    }
    if (_sse) {
        _sse->stop();
    }
    releaseTransport();
    releaseDecompressor();
    if (_body_buf) {
        free(_body_buf);
        _body_buf = nullptr;
        _body_buf_size = 0;
        _body_buf_len = 0;
    }
    _header_count = 0;
    _collect_count = 0;
    _collected_count = 0;
    _request_in_progress = false;
}

void StreamHTTPClient::releaseTransport() {
    if (_client) {
        _client->stop();
        if (_owns_client) {
            // _tcp and _tls are members; they auto-destruct. Just null the pointer.
        }
        _client = nullptr;
    }
    _tcp.stop();
    _tls.stop();
    _keep_alive_pending = false;
}

bool StreamHTTPClient::ensureTransport() {
    if (_client && _client->connected()) {
        return true;
    }
    if (!_owns_client) {
        // External client must already be connected
        if (_client) return _client->connected();
        return false;
    }

    // Set up TCP keep-alive parameters on the owned transport before connect.
    if (_https) {
#ifdef ESP32
        if (_tcp_keepalive_set) {
            _tls.setKeepAlive(_tcp_keepalive_cnt, _tcp_keepalive_idle,
                              _tcp_keepalive_intv);
        }
#endif
        _client = &_tls;
    } else {
#ifdef ESP32
        if (_tcp_keepalive_set) {
            _tcp.setKeepAlive(_tcp_keepalive_cnt, _tcp_keepalive_idle,
                              _tcp_keepalive_intv);
        }
#endif
        _client = &_tcp;
    }

    _client->setTimeout(_tcp_timeout);
    if (!_client->connect(_host.c_str(), _port)) {
        _client = nullptr;
        return false;
    }
    return true;
}

// ===========================================================================
// Authentication & headers
// ===========================================================================
void StreamHTTPClient::setBasicAuth(const String& user, const String& pass) {
    _user = user; _password = pass; _auth = "";
}
void StreamHTTPClient::setBasicAuth(const char* user, const char* pass) {
    _user = user ? String(user) : String();
    _password = pass ? String(pass) : String();
    _auth = "";
}
void StreamHTTPClient::setAuthorization(const String& auth) { _auth = auth; _user = _password = ""; }
void StreamHTTPClient::setAuthorization(const char* auth) {
    _auth = auth ? String(auth) : String();
    _user = _password = "";
}

void StreamHTTPClient::addHeader(const String& name, const String& value,
                                 bool first, bool replace) {
    if (replace) {
        for (size_t i = 0; i < _header_count; i++) {
            if (str_iequals(_headers[i].name, name)) {
                _headers[i].value = value;
                return;
            }
        }
    }
    if (_header_count >= SHC_MAX_HEADERS) {
        // Drop oldest to make room.
        for (size_t i = 1; i < _header_count; i++) {
            _headers[i-1] = _headers[i];
        }
        _header_count--;
    }
    if (first) {
        for (size_t i = _header_count; i > 0; i--) {
            _headers[i] = _headers[i-1];
        }
        _header_count++;
        _headers[0].name = name;
        _headers[0].value = value;
    } else {
        _headers[_header_count].name = name;
        _headers[_header_count].value = value;
        _header_count++;
    }
}

void StreamHTTPClient::collectHeaders(const String* headerKeys, size_t count) {
    _collect_count = 0;
    for (size_t i = 0; i < count && i < SHC_MAX_COLLECT_HEADERS; i++) {
        _collect_keys[_collect_count++] = headerKeys[i];
    }
}

void StreamHTTPClient::collectHeaders(const char** headerKeys, size_t count) {
    _collect_count = 0;
    for (size_t i = 0; i < count && i < SHC_MAX_COLLECT_HEADERS; i++) {
        _collect_keys[_collect_count++] = String(headerKeys[i]);
    }
}

String StreamHTTPClient::header(const String& name) const {
    for (size_t i = 0; i < _collected_count; i++) {
        if (str_iequals(_collected[i].key, name)) {
            return _collected[i].value;
        }
    }
    return String();
}

bool StreamHTTPClient::hasHeader(const String& name) const {
    for (size_t i = 0; i < _collected_count; i++) {
        if (str_iequals(_collected[i].key, name)) return true;
    }
    return false;
}

int StreamHTTPClient::headers() const { return (int)_collected_count; }

String StreamHTTPClient::headerName(size_t i) const {
    return (i < _collected_count) ? _collected[i].key : String();
}

String StreamHTTPClient::headerValue(size_t i) const {
    return (i < _collected_count) ? _collected[i].value : String();
}

// ===========================================================================
// Configuration setters
// ===========================================================================
void StreamHTTPClient::setUserAgent(const String& ua)      { _user_agent = ua; }
void StreamHTTPClient::setConnectTimeout(uint32_t ms)      { _connect_timeout = ms; }
void StreamHTTPClient::setTcpTimeout(uint32_t ms)          { _tcp_timeout = ms; }
void StreamHTTPClient::setFollowRedirects(StreamHTTPClient_RedirectPolicy p) {
    _redirect_policy = p;
}
void StreamHTTPClient::setRedirectLimit(uint8_t limit)     { _redirect_limit = limit; }
void StreamHTTPClient::useHTTP10(bool yes)                 { _use_http10 = yes; }
void StreamHTTPClient::useStreamMode(bool yes)             { _stream_mode = yes; }
void StreamHTTPClient::setDecompress(bool yes)             { _decompress = yes; }
void StreamHTTPClient::acceptEncoding(const String& enc)   { _accept_encoding = enc; }

void StreamHTTPClient::setTCPKeepAlive(uint32_t idleSec, uint32_t intvSec, uint8_t count) {
    _tcp_keepalive_idle = idleSec;
    _tcp_keepalive_intv = intvSec;
    _tcp_keepalive_cnt  = count;
    _tcp_keepalive_set  = true;
}

WiFiClientSecure* StreamHTTPClient::getSecureClient() {
    if (_https && _owns_client) return &_tls;
    return nullptr;
}

// ===========================================================================
// Sync request methods
// ===========================================================================
int StreamHTTPClient::GET() { return sendRequest("GET"); }

int StreamHTTPClient::POST(const String& body)   { return sendRequest("POST", body); }
int StreamHTTPClient::POST(uint8_t* body, size_t size) { return sendRequest("POST", body, size); }
int StreamHTTPClient::POST(Stream* stream, size_t size) {
    return sendRequestStream("POST", stream, size, /*chunked=*/ (size == (size_t)-1));
}

int StreamHTTPClient::PUT(const String& body)    { return sendRequest("PUT", body); }
int StreamHTTPClient::PUT(uint8_t* body, size_t size) { return sendRequest("PUT", body, size); }
int StreamHTTPClient::PUT(Stream* stream, size_t size) {
    return sendRequestStream("PUT", stream, size, /*chunked=*/ (size == (size_t)-1));
}

int StreamHTTPClient::PATCH(const String& body)  { return sendRequest("PATCH", body); }
int StreamHTTPClient::PATCH(uint8_t* body, size_t size) { return sendRequest("PATCH", body, size); }
int StreamHTTPClient::PATCH(Stream* stream, size_t size) {
    return sendRequestStream("PATCH", stream, size, /*chunked=*/ (size == (size_t)-1));
}

int StreamHTTPClient::DELETE(const String& body) { return sendRequest("DELETE", body); }
int StreamHTTPClient::DELETE(uint8_t* body, size_t size) {
    return sendRequest("DELETE", body, size);
}

int StreamHTTPClient::HEAD()     { return sendRequest("HEAD"); }
int StreamHTTPClient::OPTIONS()  { return sendRequest("OPTIONS"); }

int StreamHTTPClient::sendRequest(const char* method, const String& body) {
    const uint8_t* p = body.length() ? (const uint8_t*)body.c_str() : nullptr;
    return performRequest(method, p, body.length(), nullptr, false, false);
}

int StreamHTTPClient::sendRequest(const char* method, uint8_t* body, size_t size) {
    return performRequest(method, body, size, nullptr, false, false);
}

int StreamHTTPClient::sendRequest(const char* method, Stream* body, size_t size) {
    return performRequest(method, nullptr, size, body,
                          /*chunked=*/ (size == (size_t)-1), /*streaming=*/ true);
}

int StreamHTTPClient::sendRequestStream(const char* method, Stream* upload,
                                        size_t upload_size, bool chunked) {
    return performRequest(method, nullptr, upload_size, upload, chunked, true);
}

// ===========================================================================
// performRequest - the core of the synchronous path
// ===========================================================================
int StreamHTTPClient::performRequest(const char* method, const uint8_t* body,
                                     size_t body_size, Stream* body_stream,
                                     bool chunked_upload, bool streaming_upload) {
    (void)streaming_upload;
    if (_request_in_progress) {
        return SHC_ERROR_ALREADY_RUNNING;
    }
    _request_in_progress = true;
    resetForNewRequest();

    int saved_redirect_count = _redirect_count;
    _redirect_count = 0;

    int result = 0;
    while (true) {
        if (!ensureTransport()) {
            result = SHC_ERROR_CONNECTION_REFUSED;
            break;
        }

        // ---- Send request line + headers ----
        if (!sendRequestLine(method, _uri)) {
            result = SHC_ERROR_SEND_HEADER_FAILED; break;
        }

        bool is_head = (strcmp(method, "HEAD") == 0);

        // Determine Content-Length / Transfer-Encoding for upload
        bool will_send_body_chunked = chunked_upload;
        size_t will_send_body_len = body_size;
        if (body_stream && !chunked_upload) {
            will_send_body_len = body_size == (size_t)-1 ? 0 : body_size;
        } else if (body) {
            will_send_body_len = body_size;
        } else {
            will_send_body_len = 0;
        }

        if (!sendDefaultHeaders(will_send_body_len, will_send_body_chunked)) {
            result = SHC_ERROR_SEND_HEADER_FAILED; break;
        }
        if (!sendCustomHeaders()) { result = SHC_ERROR_SEND_HEADER_FAILED; break; }
        if (!flushHeaders())      { result = SHC_ERROR_SEND_HEADER_FAILED; break; }

        // ---- Send request body ----
        if (body) {
            if (!sendBody(body, body_size)) { result = SHC_ERROR_SEND_PAYLOAD_FAILED; break; }
            _body_sent = body_size;
        } else if (body_stream) {
            if (chunked_upload) {
                if (!sendBodyChunked(body_stream)) { result = SHC_ERROR_SEND_PAYLOAD_FAILED; break; }
            } else {
                if (!sendBodyFromStream(body_stream, body_size)) { result = SHC_ERROR_SEND_PAYLOAD_FAILED; break; }
            }
        }

        // ---- Read response ----
        int status = readResponseStatus();
        if (status < 0) { result = status; break; }
        _status_code = status;

        if (!readResponseHeaders()) { result = SHC_ERROR_READ_RESPONSE; break; }

        // Setup decompressor if Content-Encoding is gzip/deflate and enabled
        if (_decompress && (_response_flags & (SHC_HAS_CONTENT_ENCODING_GZIP |
                                               SHC_HAS_CONTENT_ENCODING_DEFLATE))) {
            if (!initDecompressor(_content_encoding)) {
                result = SHC_ERROR_DECOMPRESSION; break;
            }
        }

        // HEAD responses have no body.
        if (!is_head) {
            int br = readBody();
            if (br < 0) { result = br; break; }
        }

        // ---- Redirect? ----
        if ((_status_code >= 301 && _status_code <= 303) || _status_code == 307 || _status_code == 308) {
            if (_redirect_policy != SHC_REDIRECT_OFF &&
                _redirect_count < _redirect_limit &&
                _location.length() > 0) {
                _redirect_count++;
                // Re-issue the request to the new location.
                // For 301/302/303 we may switch to GET (per policy).
                // For 307/308 we keep method and body.
                const char* next_method = method;
                const uint8_t* next_body = body;
                size_t next_body_size = body_size;
                Stream* next_stream = body_stream;
                bool next_chunked = chunked_upload;

                if (_status_code == 303 ||
                    (_status_code != 307 && _status_code != 308 &&
                     _redirect_policy == SHC_REDIRECT_FOLLOW_GET_ONLY &&
                     (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 ||
                      strcmp(method, "PATCH") == 0 || strcmp(method, "DELETE") == 0))) {
                    next_method = "GET";
                    next_body = nullptr;
                    next_body_size = 0;
                    next_stream = nullptr;
                    next_chunked = false;
                }

                // Parse Location (may be relative or absolute)
                String old_host = _host; uint16_t old_port = _port; bool old_https = _https;
                String old_uri = _uri;
                if (_location.startsWith("http://") || _location.startsWith("https://")) {
                    if (!parseURL(_location)) {
                        result = SHC_ERROR_INVALID_URL; break;
                    }
                } else if (_location.startsWith("/")) {
                    _uri = _location;
                } else {
                    // Relative
                    String base = _uri;
                    int q = base.indexOf('?');
                    if (q >= 0) base = base.substring(0, q);
                    int slash = base.lastIndexOf('/');
                    if (slash >= 0) base = base.substring(0, slash + 1);
                    _uri = base + _location;
                }
                (void)old_host; (void)old_port; (void)old_https; (void)old_uri;

                // Reset for next iteration (drop body buffer, decompressor).
                releaseDecompressor();
                _body_buf_len = 0;
                _body_received = 0;
                _content_length = 0;
                _response_flags = 0;
                _collected_count = 0;

                // If host changed, drop the keep-alive connection.
                if (_client && !_client->connected()) {
                    _client = nullptr;
                }

                // Update method/body for the next iteration
                method = next_method;
                body = next_body;
                body_size = next_body_size;
                body_stream = next_stream;
                chunked_upload = next_chunked;
                continue;
            }
        }

        result = _status_code;
        break;
    }

    _redirect_count = saved_redirect_count;
    _request_in_progress = false;

    if (result < 0) {
        if (_on_error) _on_error(result);
    } else {
        if (_on_complete) _on_complete(result);
    }
    return result;
}

// ===========================================================================
// Request writing helpers
// ===========================================================================
bool StreamHTTPClient::sendRequestLine(const char* method, const String& uri) {
    if (!_client) return false;
    String line = String(method) + " " + uri + " HTTP/" +
                  (_use_http10 ? "1.0" : "1.1") + "\r\n";
    size_t n = line.length();
    size_t w = _client->write((const uint8_t*)line.c_str(), n);
    return w == n;
}

bool StreamHTTPClient::sendDefaultHeaders(size_t content_length, bool chunked) {
    if (!_client) return false;
    String h;
    h.reserve(128);
    h = "Host: " + _host;
    if ((_https && _port != 443) || (!_https && _port != 80)) {
        h += ":"; h += String(_port);
    }
    h += "\r\n";
    if (_user_agent.length()) {
        h += "User-Agent: "; h += _user_agent; h += "\r\n";
    }
    if (_use_http10) {
        h += "Connection: close\r\n";
    } else {
        h += "Connection: keep-alive\r\n";
        h += "Accept: */*\r\n";
    }
    if (_decompress && _accept_encoding.length()) {
        h += "Accept-Encoding: "; h += _accept_encoding; h += "\r\n";
    }
    if (_user.length() || _auth.length()) {
        if (_auth.length()) {
            h += "Authorization: "; h += _auth; h += "\r\n";
        } else {
            String cred = _user + ":" + _password;
            String b64 = base64_encode((const uint8_t*)cred.c_str(), cred.length());
            h += "Authorization: Basic "; h += b64; h += "\r\n";
        }
    }
    if (chunked) {
        h += "Transfer-Encoding: chunked\r\n";
    } else if (content_length > 0) {
        h += "Content-Length: "; h += String((unsigned long)content_length); h += "\r\n";
    }
    size_t n = h.length();
    return _client->write((const uint8_t*)h.c_str(), n) == n;
}

bool StreamHTTPClient::sendCustomHeaders() {
    for (size_t i = 0; i < _header_count; i++) {
        String h = _headers[i].name + ": " + _headers[i].value + "\r\n";
        size_t n = h.length();
        if (_client->write((const uint8_t*)h.c_str(), n) != n) return false;
    }
    return true;
}

bool StreamHTTPClient::flushHeaders() {
    if (!_client) return false;
    return _client->write((const uint8_t*)"\r\n", 2) == 2;
}

bool StreamHTTPClient::sendBody(const uint8_t* body, size_t size) {
    if (!_client || !body || size == 0) return true;
    size_t sent = 0;
    while (sent < size) {
        size_t w = _client->write(body + sent, size - sent);
        if (w == 0) return false;
        sent += w;
    }
    return true;
}

bool StreamHTTPClient::sendBodyChunked(Stream* body) {
    if (!_client) return false;
    uint8_t buf[1024];
    while (true) {
        int avail = body->available();
        if (avail <= 0) {
            // Try one more read in case available() is unreliable.
            int n = body->readBytes(buf, sizeof(buf));
            if (n == 0) break;
            avail = n;
            // Process the bytes we just read.
            String hdr = String(n, HEX) + "\r\n";
            _client->write((const uint8_t*)hdr.c_str(), hdr.length());
            _client->write(buf, n);
            _client->write((const uint8_t*)"\r\n", 2);
            _body_sent += n;
            continue;
        }
        size_t want = (size_t)avail;
        if (want > sizeof(buf)) want = sizeof(buf);
        size_t got = body->readBytes(buf, want);
        if (got == 0) break;
        String hdr = String((int)got, HEX) + "\r\n";
        _client->write((const uint8_t*)hdr.c_str(), hdr.length());
        _client->write(buf, got);
        _client->write((const uint8_t*)"\r\n", 2);
        _body_sent += got;
    }
    // Terminating chunk
    _client->write((const uint8_t*)"0\r\n\r\n", 5);
    return true;
}

bool StreamHTTPClient::sendBodyFromStream(Stream* body, size_t size) {
    if (!_client) return false;
    if (size == 0) return true;
    uint8_t buf[1024];
    size_t sent = 0;
    while (sent < size) {
        size_t want = size - sent;
        if (want > sizeof(buf)) want = sizeof(buf);
        int avail = body->available();
        if (avail <= 0) {
            delay(1);
            continue;
        }
        if (want > (size_t)avail) want = (size_t)avail;
        size_t got = body->readBytes(buf, want);
        if (got == 0) break;
        size_t w = _client->write(buf, got);
        if (w != got) return false;
        sent += got;
        _body_sent = sent;
    }
    return sent == size;
}

// ===========================================================================
// Response reading
// ===========================================================================
int StreamHTTPClient::readResponseStatus() {
    if (!_client) return SHC_ERROR_CONNECTION_LOST;
    char line[256];
    int n = read_line(_client, line, sizeof(line), _tcp_timeout);
    if (n <= 0) return SHC_ERROR_NO_HTTP_SERVER;
    // Expect: HTTP/1.x SSS Reason
    if (strncmp(line, "HTTP/", 5) != 0) return SHC_ERROR_NO_HTTP_SERVER;
    char* sp = strchr(line, ' ');
    if (!sp) return SHC_ERROR_NO_HTTP_SERVER;
    int code = atoi(sp + 1);
    if (code == 0) return SHC_ERROR_NO_HTTP_SERVER;
    return code;
}

bool StreamHTTPClient::readResponseHeaders() {
    if (!_client) return false;
    char line[SHC_MAX_HEADER_LINE];
    while (true) {
        int n = read_line(_client, line, sizeof(line), _tcp_timeout);
        if (n < 0) return false;
        if (n == 0) return true; // empty line - end of headers

        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        String name(line);
        String value(colon + 1);
        value.trim();
        name.trim();

        if (_on_header) emitHeader(name, value);

        // Built-in header handling
        if (str_iequals(name, "Content-Length")) {
            _content_length = (size_t)value.toInt();
            _response_flags |= SHC_HAS_CONTENT_LENGTH;
        } else if (str_iequals(name, "Transfer-Encoding")) {
            String v = value; v.toLowerCase();
            if (v.indexOf("chunked") >= 0) {
                _response_flags |= SHC_HAS_TRANSFER_ENCODING;
                _content_length = 0;
            }
        } else if (str_iequals(name, "Location")) {
            _location = value;
            _response_flags |= SHC_HAS_LOCATION_HEADER;
        } else if (str_iequals(name, "Connection")) {
            String v = value; v.toLowerCase();
            if (v.indexOf("close") >= 0) {
                _response_flags |= SHC_HAS_CONNECTION_CLOSE;
            }
        } else if (str_iequals(name, "Content-Encoding")) {
            String v = value; v.toLowerCase();
            if (v.indexOf("gzip") >= 0) {
                _response_flags |= SHC_HAS_CONTENT_ENCODING_GZIP;
                _content_encoding = "gzip";
            } else if (v.indexOf("deflate") >= 0) {
                _response_flags |= SHC_HAS_CONTENT_ENCODING_DEFLATE;
                _content_encoding = "deflate";
            }
        }

        // Collect this header if user requested it
        for (size_t i = 0; i < _collect_count; i++) {
            if (str_iequals(_collect_keys[i], name)) {
                if (_collected_count < SHC_MAX_COLLECT_HEADERS) {
                    _collected[_collected_count].key = name;
                    _collected[_collected_count].value = value;
                    _collected_count++;
                }
                break;
            }
        }
    }
}

int StreamHTTPClient::readBody() {
    // Decide body framing
    if (_response_flags & SHC_HAS_TRANSFER_ENCODING) {
        return readBodyChunked();
    } else if (_response_flags & SHC_HAS_CONTENT_LENGTH) {
        return readBodyContentLength(_content_length);
    } else {
        return readBodyUntilClose();
    }
}

int StreamHTTPClient::readBodyChunked() {
    if (!_client) return SHC_ERROR_CONNECTION_LOST;
    char size_line[SHC_CHUNK_LINE_BUFFER];
    uint8_t in_buf[1024];
    uint8_t out_buf[SHC_GZIP_OUT_BUF_SIZE];

    while (true) {
        if (_cancelled) return SHC_ERROR_USER_CANCEL;
        int n = read_line(_client, size_line, sizeof(size_line), _tcp_timeout);
        if (n < 0) return SHC_ERROR_CONNECTION_LOST;
        // Parse hex size (strip any extension after ';')
        char* semi = strchr(size_line, ';');
        if (semi) *semi = 0;
        long chunk_size = strtol(size_line, nullptr, 16);
        if (chunk_size < 0) return SHC_ERROR_CHUNK_SIZE;
        if (chunk_size == 0) {
            // Trailers
            while (true) {
                int t = read_line(_client, size_line, sizeof(size_line), _tcp_timeout);
                if (t <= 0) break;
            }
            return (int)_body_received;
        }

        size_t remaining = (size_t)chunk_size;
        while (remaining > 0) {
            size_t want = remaining > sizeof(in_buf) ? sizeof(in_buf) : remaining;
            size_t got = _client->readBytes(in_buf, want);
            if (got == 0) return SHC_ERROR_CONNECTION_LOST;
            remaining -= got;

            if (_decomp) {
                bool final = (remaining == 0) && false; // we'll feed MZ_FINISH later
                (void)final;
                int r = _decomp->decode(in_buf, got, /*final_chunk=*/false,
                                        out_buf, sizeof(out_buf),
                                        [](const uint8_t* d, size_t l, void* user) {
                                            StreamHTTPClient* self = (StreamHTTPClient*)user;
                                            if (self->_on_data) {
                                                self->_on_data((uint8_t*)d, l);
                                            }
                                            if (self->_stream_mode) {
                                                // Append to body buffer if user calls getString() later
                                                // (no-op for pure stream mode)
                                            } else {
                                                self->_body_buf_len; // placeholder
                                                // Append to body buffer
                                                if (self->_body_buf == nullptr) {
                                                    self->_body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                                                    if (self->_body_buf) self->_body_buf_size = SHC_DEFAULT_STRING_BUFFER;
                                                }
                                                if (self->_body_buf) {
                                                    if (self->_body_buf_len + l > self->_body_buf_size) {
                                                        size_t newsz = self->_body_buf_size * 2;
                                                        while (newsz < self->_body_buf_len + l) newsz *= 2;
                                                        uint8_t* nb = (uint8_t*)realloc(self->_body_buf, newsz);
                                                        if (nb) {
                                                            self->_body_buf = nb;
                                                            self->_body_buf_size = newsz;
                                                        } else {
                                                            return; // drop bytes
                                                        }
                                                    }
                                                    memcpy(self->_body_buf + self->_body_buf_len, d, l);
                                                    self->_body_buf_len += l;
                                                }
                                            }
                                        }, this);
                if (r == -2) return SHC_ERROR_DECOMPRESSION;
                _body_received += got; // count input bytes
            } else {
                // No decompression - pass through
                if (_on_data) _on_data(in_buf, got);
                if (_body_buf || !_stream_mode) {
                    if (_body_buf == nullptr) {
                        _body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                        if (_body_buf) _body_buf_size = SHC_DEFAULT_STRING_BUFFER;
                    }
                    if (_body_buf) {
                        if (_body_buf_len + got > _body_buf_size) {
                            size_t newsz = _body_buf_size * 2;
                            while (newsz < _body_buf_len + got) newsz *= 2;
                            uint8_t* nb = (uint8_t*)realloc(_body_buf, newsz);
                            if (nb) { _body_buf = nb; _body_buf_size = newsz; }
                            else { return SHC_ERROR_TOO_LESS_RAM; }
                        }
                        memcpy(_body_buf + _body_buf_len, in_buf, got);
                        _body_buf_len += got;
                    }
                }
                _body_received += got;
            }
        }
        // Consume trailing CRLF
        int t = read_line(_client, size_line, sizeof(size_line), _tcp_timeout);
        (void)t;
    }
}

int StreamHTTPClient::readBodyContentLength(size_t len) {
    if (!_client) return SHC_ERROR_CONNECTION_LOST;
    uint8_t in_buf[1024];
    uint8_t out_buf[SHC_GZIP_OUT_BUF_SIZE];
    size_t remaining = len;
    while (remaining > 0) {
        if (_cancelled) return SHC_ERROR_USER_CANCEL;
        size_t want = remaining > sizeof(in_buf) ? sizeof(in_buf) : remaining;
        size_t got = _client->readBytes(in_buf, want);
        if (got == 0) return SHC_ERROR_CONNECTION_LOST;
        remaining -= got;

        if (_decomp) {
            bool final_chunk = (remaining == 0);
            int r = _decomp->decode(in_buf, got, final_chunk,
                                    out_buf, sizeof(out_buf),
                                    [](const uint8_t* d, size_t l, void* user) {
                                        StreamHTTPClient* self = (StreamHTTPClient*)user;
                                        if (self->_on_data) {
                                            self->_on_data((uint8_t*)d, l);
                                        }
                                        if (self->_body_buf == nullptr && !self->_stream_mode) {
                                            self->_body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                                            if (self->_body_buf) self->_body_buf_size = SHC_DEFAULT_STRING_BUFFER;
                                        }
                                        if (self->_body_buf && !self->_stream_mode) {
                                            if (self->_body_buf_len + l > self->_body_buf_size) {
                                                size_t newsz = self->_body_buf_size * 2;
                                                while (newsz < self->_body_buf_len + l) newsz *= 2;
                                                uint8_t* nb = (uint8_t*)realloc(self->_body_buf, newsz);
                                                if (nb) { self->_body_buf = nb; self->_body_buf_size = newsz; }
                                                else return;
                                            }
                                            memcpy(self->_body_buf + self->_body_buf_len, d, l);
                                            self->_body_buf_len += l;
                                        }
                                    }, this);
            if (r == -2) return SHC_ERROR_DECOMPRESSION;
        } else {
            if (_on_data) _on_data(in_buf, got);
            if (_body_buf == nullptr && !_stream_mode) {
                _body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                if (_body_buf) _body_buf_size = SHC_DEFAULT_STRING_BUFFER;
            }
            if (_body_buf && !_stream_mode) {
                if (_body_buf_len + got > _body_buf_size) {
                    size_t newsz = _body_buf_size * 2;
                    while (newsz < _body_buf_len + got) newsz *= 2;
                    uint8_t* nb = (uint8_t*)realloc(_body_buf, newsz);
                    if (nb) { _body_buf = nb; _body_buf_size = newsz; }
                    else return SHC_ERROR_TOO_LESS_RAM;
                }
                memcpy(_body_buf + _body_buf_len, in_buf, got);
                _body_buf_len += got;
            }
        }
        _body_received += got;
    }
    return (int)_body_received;
}

int StreamHTTPClient::readBodyUntilClose() {
    if (!_client) return SHC_ERROR_CONNECTION_LOST;
    uint8_t in_buf[1024];
    uint8_t out_buf[SHC_GZIP_OUT_BUF_SIZE];
    while (_client->connected() || _client->available() > 0) {
        if (_cancelled) return SHC_ERROR_USER_CANCEL;
        int avail = _client->available();
        if (avail <= 0) { delay(1); continue; }
        size_t want = (size_t)avail;
        if (want > sizeof(in_buf)) want = sizeof(in_buf);
        size_t got = _client->readBytes(in_buf, want);
        if (got == 0) break;

        if (_decomp) {
            int r = _decomp->decode(in_buf, got, /*final_chunk=*/false,
                                    out_buf, sizeof(out_buf),
                                    [](const uint8_t* d, size_t l, void* user) {
                                        StreamHTTPClient* self = (StreamHTTPClient*)user;
                                        if (self->_on_data) {
                                            self->_on_data((uint8_t*)d, l);
                                        }
                                        if (self->_body_buf == nullptr && !self->_stream_mode) {
                                            self->_body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                                            if (self->_body_buf) self->_body_buf_size = SHC_DEFAULT_STRING_BUFFER;
                                        }
                                        if (self->_body_buf && !self->_stream_mode) {
                                            if (self->_body_buf_len + l > self->_body_buf_size) {
                                                size_t newsz = self->_body_buf_size * 2;
                                                while (newsz < self->_body_buf_len + l) newsz *= 2;
                                                uint8_t* nb = (uint8_t*)realloc(self->_body_buf, newsz);
                                                if (nb) { self->_body_buf = nb; self->_body_buf_size = newsz; }
                                                else return;
                                            }
                                            memcpy(self->_body_buf + self->_body_buf_len, d, l);
                                            self->_body_buf_len += l;
                                        }
                                    }, this);
            if (r == -2) return SHC_ERROR_DECOMPRESSION;
        } else {
            if (_on_data) _on_data(in_buf, got);
            if (_body_buf == nullptr && !_stream_mode) {
                _body_buf = (uint8_t*)malloc(SHC_DEFAULT_STRING_BUFFER);
                if (_body_buf) _body_buf_size = SHC_DEFAULT_STRING_BUFFER;
            }
            if (_body_buf && !_stream_mode) {
                if (_body_buf_len + got > _body_buf_size) {
                    size_t newsz = _body_buf_size * 2;
                    while (newsz < _body_buf_len + got) newsz *= 2;
                    uint8_t* nb = (uint8_t*)realloc(_body_buf, newsz);
                    if (nb) { _body_buf = nb; _body_buf_size = newsz; }
                    else return SHC_ERROR_TOO_LESS_RAM;
                }
                memcpy(_body_buf + _body_buf_len, in_buf, got);
                _body_buf_len += got;
            }
        }
        _body_received += got;
    }
    return (int)_body_received;
}

// ===========================================================================
// Decompression setup
// ===========================================================================
bool StreamHTTPClient::initDecompressor(const String& content_encoding) {
    releaseDecompressor();
    _decomp = new (std::nothrow) StreamHTTPClient_GzipDecompressor(content_encoding.c_str());
    return _decomp != nullptr;
}

void StreamHTTPClient::releaseDecompressor() {
    if (_decomp) {
        delete _decomp;
        _decomp = nullptr;
    }
}

// ===========================================================================
// Response access
// ===========================================================================
int StreamHTTPClient::getStatusCode() const { return _status_code; }

String StreamHTTPClient::getString() {
    if (_body_buf && _body_buf_len > 0) {
        // Convert to String (with explicit length so embedded NULs are kept)
        String s;
        s.reserve(_body_buf_len);
        s.concat((const char*)_body_buf, _body_buf_len);
        return s;
    }
    if (_external_string_buf) return *_external_string_buf;
    return String();
}

size_t StreamHTTPClient::getSize() const {
    if (_content_length) return _content_length;
    return _body_received;
}

WiFiClient* StreamHTTPClient::getStreamPtr() {
    return _client;
}

Stream* StreamHTTPClient::getStream() {
    return (Stream*)_client;
}

bool StreamHTTPClient::connected() const {
    return _client && _client->connected();
}

int StreamHTTPClient::available() const {
    return _client ? _client->available() : 0;
}

size_t StreamHTTPClient::readBytes(uint8_t* buf, size_t len) {
    return _client ? _client->readBytes(buf, len) : 0;
}

// ===========================================================================
// Callback emitters
// ===========================================================================
void StreamHTTPClient::emitData(uint8_t* data, size_t len) {
    if (_on_data) _on_data(data, len);
}

void StreamHTTPClient::emitHeader(const String& name, const String& value) {
    if (_on_header) _on_header(name, value);
}

void StreamHTTPClient::emitProgress() {
    if (!_on_progress) return;
    StreamHTTPClient_Progress p;
    p.bytes_sent = _body_sent;
    p.total_sent = 0;
    p.bytes_received = _body_received;
    p.total_received = _content_length;
    p.elapsed_ms = millis() - _request_start_ms;
    _on_progress(p);
}

void StreamHTTPClient::emitComplete(int code) {
    if (_on_complete) _on_complete(code);
}

void StreamHTTPClient::emitError(int err) {
    if (_on_error) _on_error(err);
}

// ===========================================================================
// Async API - delegates to StreamHTTPClient_AsyncImpl
// ===========================================================================
bool StreamHTTPClient::asyncGET() {
    return asyncSendRequest("GET");
}
bool StreamHTTPClient::asyncPOST(const String& body) {
    return asyncSendRequest("POST", body);
}
bool StreamHTTPClient::asyncPOST(uint8_t* body, size_t size) {
    return asyncSendRequest("POST", body, size);
}
bool StreamHTTPClient::asyncPOST(Stream* stream, size_t size) {
    return asyncSendRequest("POST", stream, size);
}
bool StreamHTTPClient::asyncPUT(const String& body) {
    return asyncSendRequest("PUT", body);
}
bool StreamHTTPClient::asyncPUT(Stream* stream, size_t size) {
    return asyncSendRequest("PUT", stream, size);
}
bool StreamHTTPClient::asyncDELETE(const String& body) {
    return asyncSendRequest("DELETE", body);
}

bool StreamHTTPClient::asyncSendRequest(const char* method, const String& body) {
    if (!_async) _async = new StreamHTTPClient_AsyncImpl(this);
    return _async->start(method, body);
}

bool StreamHTTPClient::asyncSendRequest(const char* method, uint8_t* body, size_t size) {
    if (!_async) _async = new StreamHTTPClient_AsyncImpl(this);
    return _async->start(method, body, size);
}

bool StreamHTTPClient::asyncSendRequest(const char* method, Stream* body, size_t size) {
    if (!_async) _async = new StreamHTTPClient_AsyncImpl(this);
    return _async->start(method, body, size);
}

bool StreamHTTPClient::asyncSendRequestStream(const char* method, Stream* upload,
                                              size_t upload_size, bool chunked) {
    if (!_async) _async = new StreamHTTPClient_AsyncImpl(this);
    return _async->startStream(method, upload, upload_size, chunked);
}

void StreamHTTPClient::asyncStop() {
    if (_async) _async->stop();
}

bool StreamHTTPClient::asyncPoll() {
    if (_async) return _async->poll();
    return false;
}

bool StreamHTTPClient::isAsyncRunning() const {
    return _async ? _async->isRunning() : false;
}

StreamHTTPClient_State StreamHTTPClient::asyncState() const {
    return _async ? _async->state() : SHC_STATE_IDLE;
}

int StreamHTTPClient::asyncStatusCode() const {
    return _async ? _async->statusCode() : 0;
}

int StreamHTTPClient::asyncLastError() const {
    return _async ? _async->lastError() : 0;
}

int StreamHTTPClient::asyncWaitComplete(uint32_t timeoutMs) {
    if (!_async) return SHC_ERROR_UNKNOWN;
    return _async->waitComplete(timeoutMs);
}

// ===========================================================================
// SSE - Server-Sent Events
// ===========================================================================
bool StreamHTTPClient::connectSSE() {
    if (!_sse) _sse = new StreamHTTPClient_SSEImpl(this);
    return _sse->connect();
}

void StreamHTTPClient::stopSSE() {
    if (_sse) _sse->stop();
}

bool StreamHTTPClient::ssePoll() {
    return _sse ? _sse->poll() : false;
}

bool StreamHTTPClient::sseConnected() const {
    return _sse ? _sse->connected() : false;
}

// Used by StreamHTTPClient.cpp - placeholder; full impl in StreamHTTPClient_SSE.cpp
