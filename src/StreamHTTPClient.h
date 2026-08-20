/**************************************************************************
  StreamHTTPClient.h - main public header

  Streaming HTTP/HTTPS client for Arduino, API-compatible with HTTPClient.

  Features:
    - HTTP and HTTPS (TLS via WiFiClientSecure)
    - Bidirectional streaming (request body via Stream, response body via
      getStreamPtr() or onData callbacks)
    - Chunked transfer encoding (send and receive)
    - gzip / deflate decompression (powered by miniz)
    - Content-Length and Keep-Alive
    - Configurable redirect following
    - Basic Authentication
    - Custom request headers
    - Server-Sent Events (SSE) integrated into the main class
    - Synchronous and asynchronous request execution
        - Async uses a FreeRTOS task when available
        - Cooperative polling fallback for bare-metal Arduino cores

  Reference: github.com/espressif/arduino-esp32/libraries/HTTPClient

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


#pragma once

#include <Arduino.h>
#include <Client.h>
#include <Stream.h>
#include <functional>

#include "StreamHTTPClientConst.h"
#include "StreamHTTPClientTypes.h"
#include "StreamHTTPClient_GzipDecompressor.h"

// ---------------------------------------------------------------------------
// Platform detection - bring in the right WiFi headers and types.
// ---------------------------------------------------------------------------
#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecure.h>
  #define SHC_HAS_FREERTOS 1
  #define SHC_PLATFORM_ESP32 1
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecureBearSSL.h>
  #define SHC_HAS_FREERTOS 0
  #define SHC_PLATFORM_ESP8266 1
  typedef BearSSL::WiFiClientSecure WiFiClientSecure;
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM)
  #include <WiFi101.h>
  #define SHC_HAS_FREERTOS 0
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
  #include <WiFiClient.h>
  #include <WiFiClientSecure.h>
  #define SHC_HAS_FREERTOS 0
#else
  // Generic Arduino WiFi library (UNO WiFi Rev2, MKR WiFi 1010, etc.)
  #include <WiFi.h>
  #include <WiFiClient.h>
  #if __has_include(<WiFiClientSecure.h>)
    #include <WiFiClientSecure.h>
  #endif
  #define SHC_HAS_FREERTOS 0
#endif

// Fallback: if WiFiClientSecure is not available, define a stub type so the
// class still compiles. begin("https://...") will return false in that case.
#if !defined(__has_include) || !__has_include(<WiFiClientSecure.h>)
class WiFiClientSecure : public WiFiClient {};
#endif

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class StreamHTTPClient_AsyncImpl;
class StreamHTTPClient_SSEImpl;

class StreamHTTPClient {
public:
    StreamHTTPClient();
    ~StreamHTTPClient();

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Parse a URL ("http://host[:port]/path" or "https://host[:port]/path")
    // and prepare the client. Returns false if the URL is invalid or the
    // scheme is not supported.
    bool begin(const String& url);

    // Connect to a specific host/port/uri. Uses http (not https).
    bool begin(const String& host, uint16_t port, const String& uri = "/");

    // Use an externally-managed client (e.g. already-connected WiFiClientSecure
    // with a configured certificate). StreamHTTPClient does NOT take ownership.
    bool begin(WiFiClient* client, const String& url);

    // Close the connection and free resources. Safe to call multiple times.
    void end();

    // -----------------------------------------------------------------------
    // Authentication & headers
    // -----------------------------------------------------------------------
    void setBasicAuth(const String& user, const String& password);
    void setBasicAuth(const char* user, const char* password);

    // Set raw Authorization header value, e.g. "Bearer xyz".
    void setAuthorization(const String& auth);
    void setAuthorization(const char* auth);

    // Add a custom request header.
    //   first   - if true, prepend at the top of the header list
    //   replace - if true (default), overwrite an existing header with the
    //             same name; if false, append a duplicate
    void addHeader(const String& name, const String& value,
                   bool first = false, bool replace = true);

    // Collect these response headers so they can later be queried by name
    // with header(). By default only Content-Length, Content-Type,
    // Transfer-Encoding, Content-Encoding, Location, Connection are stored.
    void collectHeaders(const String* headerKeys, size_t count);
    void collectHeaders(const char** headerKeys, size_t count);

    // Query a collected response header. Returns "" if not collected or not
    // present in the response.
    String header(const String& name) const;
    bool   hasHeader(const String& name) const;
    int    headers() const;       // number of collected headers received
    String headerName(size_t i) const;
    String headerValue(size_t i) const;

    // -----------------------------------------------------------------------
    // Per-request configuration
    // -----------------------------------------------------------------------
    void setUserAgent(const String& ua);
    void setConnectTimeout(uint32_t ms);
    void setTcpTimeout(uint32_t ms);
    void setFollowRedirects(StreamHTTPClient_RedirectPolicy policy);
    void setRedirectLimit(uint8_t limit);
    void setTCPKeepAlive(uint32_t idleSec, uint32_t intvSec, uint8_t count);
    void useHTTP10(bool yes);
    void useStreamMode(bool yes);          // stream body instead of buffering
    void setDecompress(bool yes);          // enable gzip/deflate decompression
    void acceptEncoding(const String& enc);// override Accept-Encoding header

    // -----------------------------------------------------------------------
    // Synchronous requests
    // -----------------------------------------------------------------------
    int GET();
    int POST(const String& body);
    int POST(uint8_t* body, size_t size);
    int POST(Stream* stream, size_t size = (size_t)-1);
    int POST(Stream& stream, size_t size = (size_t)-1) { return POST(&stream, size); }
    int PUT(const String& body);
    int PUT(uint8_t* body, size_t size);
    int PUT(Stream* stream, size_t size = (size_t)-1);
    int PATCH(const String& body);
    int PATCH(uint8_t* body, size_t size);
    int PATCH(Stream* stream, size_t size = (size_t)-1);
    int DELETE(const String& body = "");
    int DELETE(uint8_t* body, size_t size);
    int HEAD();
    int OPTIONS();

    // Generic request entry points. method is "GET", "POST", "PUT", ...
    int sendRequest(const char* method, const String& body = String());
    int sendRequest(const char* method, uint8_t* body, size_t size);
    int sendRequest(const char* method, Stream* body, size_t size = (size_t)-1);

    // Bidirectional streaming: send request body from a Stream, receive
    // response body via getStreamPtr()/onData.
    //   upload_size == (size_t)-1  -> chunked transfer encoding
    //   otherwise                  -> Content-Length: upload_size
    int sendRequestStream(const char* method, Stream* upload,
                          size_t upload_size = (size_t)-1,
                          bool chunked = false);

    // -----------------------------------------------------------------------
    // Response access
    // -----------------------------------------------------------------------
    int    getStatusCode() const;
    String getString();                    // body as String (buffered mode)
    size_t getSize() const;                // Content-Length, or bytes received
    WiFiClient* getStreamPtr();
    Stream*     getStream();
    bool   connected() const;
    int    available() const;              // bytes available in body stream
    size_t readBytes(uint8_t* buf, size_t len);

    // -----------------------------------------------------------------------
    // Asynchronous requests (FreeRTOS task or cooperative polling)
    // -----------------------------------------------------------------------
    bool asyncGET();
    bool asyncPOST(const String& body);
    bool asyncPOST(uint8_t* body, size_t size);
    bool asyncPOST(Stream* stream, size_t size = (size_t)-1);
    bool asyncPUT(const String& body);
    bool asyncPUT(Stream* stream, size_t size = (size_t)-1);
    bool asyncDELETE(const String& body = "");
    bool asyncSendRequest(const char* method, const String& body = String());
    bool asyncSendRequest(const char* method, uint8_t* body, size_t size);
    bool asyncSendRequest(const char* method, Stream* body, size_t size = (size_t)-1);
    bool asyncSendRequestStream(const char* method, Stream* upload,
                                size_t upload_size = (size_t)-1,
                                bool chunked = false);

    void asyncStop();                      // cancel current async request
    bool asyncPoll();                      // drive the state machine (non-task mode)
    bool isAsyncRunning() const;
    StreamHTTPClient_State asyncState() const;
    int  asyncStatusCode() const;
    int  asyncLastError() const;
    int  asyncWaitComplete(uint32_t timeoutMs = 0);  // blocking wait

    // -----------------------------------------------------------------------
    // Callbacks (used by async requests and by SSE)
    // -----------------------------------------------------------------------
    typedef std::function<void(uint8_t* data, size_t len)> DataCallback;
    typedef std::function<void(int statusCode)> CompleteCallback;
    typedef std::function<void(int err)> ErrorCallback;
    typedef std::function<void(const String& name, const String& value)> HeaderCallback;
    typedef std::function<void(const StreamHTTPClient_Progress&)> ProgressCallback;

    void onData(DataCallback cb)            { _on_data = cb; }
    void onComplete(CompleteCallback cb)    { _on_complete = cb; }
    void onError(ErrorCallback cb)          { _on_error = cb; }
    void onHeader(HeaderCallback cb)        { _on_header = cb; }
    void onProgress(ProgressCallback cb)    { _on_progress = cb; }

    // -----------------------------------------------------------------------
    // SSE - Server-Sent Events (integrated into the main class)
    // -----------------------------------------------------------------------
    bool connectSSE();
    void stopSSE();
    bool ssePoll();                        // drive SSE parser (call from loop())
    bool sseConnected() const;

    typedef std::function<void(const SSEEvent&)> SSECallback;
    void onEvent(SSECallback cb)                  { _on_event = cb; }
    void onSSEConnect(std::function<void()> cb)   { _on_sse_connect = cb; }
    void onSSEError(ErrorCallback cb)             { _on_sse_error = cb; }

    // -----------------------------------------------------------------------
    // Utility
    // -----------------------------------------------------------------------
    static String errorToString(int err);

    // Allow access to the underlying secure client for setting certificates.
    // Returns nullptr if begin() was not called or the connection is plain HTTP.
    WiFiClientSecure* getSecureClient();

private:
    // Internal helpers ------------------------------------------------------
    struct Header {
        String name;
        String value;
    };

    struct CollectedHeader {
        String key;
        String value;
    };

    // URL parsing -----------------------------------------------------------
    bool parseURL(const String& url);
    void resetForNewRequest();

    // Transport -------------------------------------------------------------
    bool ensureTransport();
    void releaseTransport();

    // Request writing -------------------------------------------------------
    bool sendRequestLine(const char* method, const String& uri);
    bool sendDefaultHeaders(size_t content_length, bool chunked);
    bool sendCustomHeaders();
    bool flushHeaders();
    bool sendBody(const uint8_t* body, size_t size);
    bool sendBodyChunked(Stream* body);
    bool sendBodyFromStream(Stream* body, size_t size);

    // Response reading ------------------------------------------------------
    int  readResponseStatus();
    bool readResponseHeaders();
    int  readResponseLine(String& line);
    int  readBody();
    int  readBodyStreamed();    // stream body to caller via callbacks or buffer
    int  readBodyChunked();
    int  readBodyContentLength(size_t len);
    int  readBodyUntilClose();

    // Redirects -------------------------------------------------------------
    int  handleRedirect(int statusCode);

    // Decompression ---------------------------------------------------------
    bool initDecompressor(const String& content_encoding);
    void releaseDecompressor();

    // Member variables ------------------------------------------------------
    String      _host;
    uint16_t    _port;
    String      _uri;
    bool        _https;
    String      _user;          // Basic Auth
    String      _password;
    String      _auth;          // Raw Authorization header

    Header      _headers[SHC_MAX_HEADERS];
    size_t      _header_count;

    String      _collect_keys[SHC_MAX_COLLECT_HEADERS];
    size_t      _collect_count;
    CollectedHeader _collected[SHC_MAX_COLLECT_HEADERS];
    size_t      _collected_count;

    WiFiClient*     _client;        // active transport (owned or external)
    WiFiClient      _tcp;           // owned TCP client
    WiFiClientSecure _tls;          // owned TLS client
    bool            _owns_client;   // true if we created _client

    int         _status_code;
    size_t      _content_length;    // declared by server (0 if unknown)
    size_t      _body_received;     // bytes received (after dechunk/decompress)
    size_t      _body_sent;         // bytes uploaded
    uint8_t     _response_flags;    // SHC_HAS_* bitmask
    String      _location;          // for redirects

    // Configuration
    String      _user_agent;
    uint32_t    _connect_timeout;
    uint32_t    _tcp_timeout;
    StreamHTTPClient_RedirectPolicy _redirect_policy;
    uint8_t     _redirect_limit;
    uint8_t     _redirect_count;
    bool        _use_http10;
    bool        _stream_mode;
    bool        _decompress;
    String      _accept_encoding;
    bool        _keep_alive_pending;

    uint32_t    _tcp_keepalive_idle;
    uint32_t    _tcp_keepalive_intv;
    uint8_t     _tcp_keepalive_cnt;
    bool        _tcp_keepalive_set;

    // Body buffer (buffered mode)
    uint8_t*    _body_buf;
    size_t      _body_buf_size;
    size_t      _body_buf_len;
    String*     _external_string_buf;

    // Decompressor
    StreamHTTPClient_GzipDecompressor* _decomp;
    String      _content_encoding;

    // Async + SSE (hidden behind impl to keep the public header clean)
    friend class StreamHTTPClient_AsyncImpl;
    friend class StreamHTTPClient_SSEImpl;
    StreamHTTPClient_AsyncImpl* _async;
    StreamHTTPClient_SSEImpl*   _sse;

    // Callbacks
    DataCallback       _on_data;
    CompleteCallback   _on_complete;
    ErrorCallback      _on_error;
    HeaderCallback     _on_header;
    ProgressCallback   _on_progress;

    SSECallback        _on_event;
    std::function<void()> _on_sse_connect;
    ErrorCallback      _on_sse_error;

    // Internal flags
    bool        _request_in_progress;
    bool        _cancelled;
    uint32_t    _request_start_ms;
    int         _last_err;        // last error code encountered

    // Internal helpers used by both sync and async paths
    int  performRequest(const char* method, const uint8_t* body,
                        size_t size, Stream* body_stream,
                        bool chunked_upload, bool streaming_upload);
    void emitData(uint8_t* data, size_t len);
    void emitHeader(const String& name, const String& value);
    void emitProgress();
    void emitComplete(int code);
    void emitError(int err);
};
