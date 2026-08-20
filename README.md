# StreamHTTPClient
### English | [简体中文](./README.zh-CN.md)

A streaming HTTP/HTTPS client for Arduino with synchronous **and** asynchronous
APIs, bidirectional streaming, Server-Sent Events (SSE), chunked transfer
encoding, gzip/deflate decompression, Keep-Alive, redirects and Basic
Authentication.

The public API is **drop-in compatible** with the upstream Arduino
[`HTTPClient`](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)
for the common call patterns (`begin`, `GET`, `POST`, `addHeader`,
`getString`, ...) so existing sketches can be ported with minimal changes.
On top of that, StreamHTTPClient adds first-class streaming, async, SSE and
decompression support that the stock `HTTPClient` lacks.

Reference implementation: [github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32).

---

## Features

| Feature | Notes |
| --- | --- |
| HTTP and HTTPS | HTTPS uses `WiFiClientSecure` (platform-native TLS) |
| Bidirectional streaming | Upload body from any `Stream`; download body via `getStreamPtr()` or `onData` callback |
| Chunked transfer encoding | Both send and receive |
| Content-Length | Transparent |
| Keep-Alive | Default HTTP/1.1; can be turned off with `useHTTP10(true)` |
| Redirects | `SHC_REDIRECT_OFF` / `SHC_REDIRECT_FOLLOW_GET_ONLY` (default) / `SHC_REDIRECT_FOLLOW_ALL`; configurable limit |
| Basic Auth | `setBasicAuth(user, pass)`; raw Authorization via `setAuthorization("Bearer ...")` |
| Custom headers | `addHeader(name, value, first, replace)` |
| gzip / deflate decompression | Powered by [miniz](https://github.com/richgel999/miniz) (bundled, MIT license) |
| Server-Sent Events (SSE) | Integrated into the main class via `connectSSE()` / `onEvent()` |
| Sync API | `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, `sendRequest`, `sendRequestStream` |
| Async API | FreeRTOS task on ESP32 / RP2040; cooperative polling fallback on other cores |
| TCP keep-alive | `setTCPKeepAlive(idle, intv, count)` (ESP32 only) |

---

## Supported platforms

| Platform | Status | Notes |
| --- | --- | --- |
| ESP32 (Arduino-ESP32) | Full | FreeRTOS task-based async; TCP keep-alive |
| ESP8266 (Arduino-ESP8266) | Full | Cooperative polling async (no FreeRTOS task) |
| Raspberry Pi Pico W (arduino-pico) | Full | Cooperative polling |
| SAMD / SAM (WiFi101) | Full | Cooperative polling |
| Generic Arduino WiFi (UNO WiFi Rev2, Nano 33 IoT, ...) | Full | Cooperative polling |

The library auto-detects the platform via `#ifdef`. If `WiFiClientSecure` is
not available, HTTPS requests will return `false` from `begin("https://...")`.

---

## Installation

### Option A: Arduino Library Manager (after publication)

Sketch → Include Library → Manage Libraries... → search for "StreamHTTPClient"
→ Install.

### Option B: Manual install

1. Download the latest `StreamHTTPClient-x.y.z.zip` from the
   [releases page](https://github.com/your-org/StreamHTTPClient/releases).
2. Sketch → Include Library → Add .ZIP Library... → pick the zip.
3. Restart the Arduino IDE.

### Option C: PlatformIO

```ini
lib_deps =
    StreamHTTPClient=symlink://file:///path/to/StreamHTTPClient
```

Or copy the `StreamHTTPClient/` folder into your project's `lib/` directory.

---

## Quick start

```cpp
#include <WiFi.h>
#include <StreamHTTPClient.h>

StreamHTTPClient http;

void setup() {
    Serial.begin(115200);
    WiFi.begin("ssid", "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    http.begin("http://httpbin.org/get");
    int code = http.GET();
    if (code == 200) {
        Serial.println(http.getString());
    }
    http.end();
}

void loop() {}
```

---

## Examples

The library ships with 16 examples in `examples/`:

| Example | Demonstrates |
| --- | --- |
| `BasicGET` | Simplest GET request |
| `BasicPOST` | POST with JSON body |
| `HTTPSRequest` | HTTPS / TLS with insecure mode and CA cert notes |
| `StreamingDownload` | Large file download without buffering |
| `StreamingUpload` | Stream upload from `File` (SPIFFS) with Content-Length and chunked |
| `ChunkedResponse` | Reading a chunked HTTP response |
| `GzipDecompression` | Transparent gzip decompression |
| `AsyncRequest` | Async GET with callbacks |
| `AsyncStreaming` | Async streaming download while blinking an LED |
| `SSEClient` | Server-Sent Events subscription |
| `SSEChatGPT` | OpenAI streaming chat completion via SSE |
| `BasicAuth` | HTTP Basic Authentication |
| `CustomHeaders` | Send custom request headers, collect response headers |
| `RedirectFollow` | HTTP redirect handling policies |
| `KeepAlive` | HTTP/1.1 connection reuse vs HTTP/1.0 |
| `BidirectionalStream` | Simultaneous streaming upload + download |
| `PutDelete` | PUT and DELETE methods |
| `FullFeatured` | Kitchen-sink example combining every feature |

---

## API reference

### Lifecycle

| Method | Description |
| --- | --- |
| `bool begin(const String& url)` | Parse `http(s)://host[:port]/path` and prepare the client. |
| `bool begin(const String& host, uint16_t port, const String& uri = "/")` | Direct connection (HTTP). |
| `bool begin(WiFiClient* client, const String& url)` | Use an externally-managed transport. |
| `void end()` | Close the connection and free resources. |

### Authentication & headers

| Method | Description |
| --- | --- |
| `setBasicAuth(user, pass)` | HTTP Basic authentication. |
| `setAuthorization(auth)` | Raw `Authorization` header, e.g. `"Bearer xyz"`. |
| `addHeader(name, value, first=false, replace=true)` | Add/replace/prepend a custom request header. |
| `collectHeaders(keys, count)` | Declare which response headers to capture. |
| `header(name)` / `hasHeader(name)` / `headers()` | Query captured response headers. |
| `headerName(i)` / `headerValue(i)` | Iterate captured headers by index. |

### Configuration

| Method | Description |
| --- | --- |
| `setUserAgent(ua)` | Default `"StreamHTTPClient/1.0"`. |
| `setConnectTimeout(ms)` | Default 5000. |
| `setTcpTimeout(ms)` | Default 5000. |
| `setFollowRedirects(policy)` | `SHC_REDIRECT_OFF`, `SHC_REDIRECT_FOLLOW_GET_ONLY`, `SHC_REDIRECT_FOLLOW_ALL`. |
| `setRedirectLimit(n)` | Default 10. |
| `setTCPKeepAlive(idleSec, intvSec, count)` | ESP32 only. |
| `useHTTP10(true)` | Use HTTP/1.0 (no keep-alive). |
| `useStreamMode(true)` | Do not buffer the body in RAM. |
| `setDecompress(true)` | Enable gzip/deflate decompression (default). |
| `acceptEncoding(enc)` | Override `Accept-Encoding` header. |

### Synchronous requests

| Method | Description |
| --- | --- |
| `GET()` | Issue a `GET`. |
| `POST(body)` / `POST(buf, size)` / `POST(stream, size=-1)` | Issue a `POST` with body. |
| `PUT`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS` | Same body variants. |
| `sendRequest(method, body)` | Generic entry point. |
| `sendRequestStream(method, upload, size=-1, chunked=false)` | Bidirectional streaming. `size=-1` implies chunked. |

### Response access

| Method | Description |
| --- | --- |
| `getStatusCode()` | HTTP status code. |
| `getString()` | Body as String (buffered mode). |
| `getSize()` | `Content-Length`, or bytes received. |
| `getStreamPtr()` | Direct access to the underlying `WiFiClient*`. |
| `getStream()` | Same, as `Stream*`. |
| `connected()` | True if the transport is still connected. |
| `available()` | Bytes available to read from the body. |
| `readBytes(buf, len)` | Read raw bytes from the body. |

### Async API

| Method | Description |
| --- | --- |
| `asyncGET()` / `asyncPOST(...)` / `asyncPUT(...)` / `asyncDELETE(...)` | Start an async request. |
| `asyncSendRequest(method, body)` / `asyncSendRequestStream(method, upload, size, chunked)` | Generic async entry points. |
| `asyncPoll()` | Drive the state machine (cooperative mode only). |
| `asyncStop()` | Cancel the current request. |
| `isAsyncRunning()` | True while the request is in flight. |
| `asyncState()` | One of `SHC_STATE_IDLE`, `SHC_STATE_CONNECTING`, ..., `SHC_STATE_DONE`, `SHC_STATE_ERROR`. |
| `asyncStatusCode()` / `asyncLastError()` | Final status / error code. |
| `asyncWaitComplete(timeoutMs=0)` | Block until done. |

### Callbacks

```cpp
http.onData([](uint8_t* data, size_t len) { ... });
http.onComplete([](int code) { ... });
http.onError([](int err) { ... });
http.onHeader([](const String& name, const String& value) { ... });
http.onProgress([](const StreamHTTPClient_Progress& p) { ... });
```

### SSE - Server-Sent Events

```cpp
http.onEvent([](const SSEEvent& ev) {
    Serial.printf("event=%s data=%s id=%s retry=%ums\n",
                  ev.event.c_str(), ev.data.c_str(),
                  ev.id.c_str(), ev.retry);
});
http.onSSEConnect([]() { Serial.println("SSE connected"); });
http.onSSEError([](int err) { Serial.printf("SSE error: %d\n", err); });

http.connectSSE();
// In loop():
http.ssePoll();
```

The library automatically:
- Sends `Accept: text/event-stream`
- Reconnects on connection drop
- Sends `Last-Event-ID` on reconnect
- Honours the server's `retry:` field

---

## Architecture

```
                 +-----------------------------+
                 |       StreamHTTPClient      |
                 |  (public API, sync path)   |
                 +------+----------+----------+
                        |          |
                        v          v
        +---------------+   +------+----------------+
        |  Async impl   |   |    SSE impl          |
        |  (FreeRTOS    |   |  (line parser +      |
        |   task or     |   |   reconnect)         |
        |   poll)       |   |                      |
        +------+--------+   +----------------------+
               |
               v
        +------+------------+
        | GzipDecompressor  |
        |  (miniz wrapper)  |
        +------+------------+
               |
               v
        +------+------------+
        |   WiFiClient /    |
        |   WiFiClientSecure|
        +-------------------+
```

- **miniz** is statically linked into the library. Only the inflate path is
  compiled (`MINIZ_NO_ARCHIVE_APIS`, `MINIZ_NO_STDIO`, `MINIZ_NO_TIME`),
  keeping the flash footprint small (~30 KB on ESP32).
- **Async impl** spawns a `xTaskCreateUniversal` task on ESP32 with
  `SHC_ASYNC_TASK_STACK` bytes of stack (default 8 KB, configurable). On
  platforms without FreeRTOS, `asyncPoll()` drives the same state machine
  cooperatively from the caller's `loop()`.
- **SSE impl** opens a normal `GET` with `Accept: text/event-stream`, then
  feeds the response bytes through a small line-based parser. The parser is
  RFC 8895-compliant: handles `event:`, `data:`, `id:`, `retry:` fields and
  `:` comments, dispatches on blank lines, joins multi-line `data:` with `\n`.

---

## Configuration macros

All of these can be overridden by defining them before including
`StreamHTTPClient.h`:

| Macro | Default | Description |
| --- | --- | --- |
| `SHC_MAX_HEADERS` | 16 | Max custom request headers |
| `SHC_MAX_COLLECT_HEADERS` | 16 | Max response headers to collect |
| `SHC_MAX_HEADER_LINE` | 256 | Max length of a single response header line |
| `SHC_MAX_REDIRECTS` | 10 | Default redirect limit |
| `SHC_DEFAULT_STRING_BUFFER` | 4096 | Initial body buffer (grows as needed) |
| `SHC_DEFAULT_CONNECT_TIMEOUT` | 5000 | TCP connect timeout (ms) |
| `SHC_DEFAULT_TCP_TIMEOUT` | 5000 | TCP read timeout (ms) |
| `SHC_DEFAULT_USER_AGENT` | `"StreamHTTPClient/1.0"` | Default User-Agent |
| `SHC_CHUNK_LINE_BUFFER` | 32 | Chunked size line buffer |
| `SHC_SSE_LINE_BUFFER` | 1024 | SSE line parser buffer |
| `SHC_ASYNC_TASK_STACK` | 8192 | FreeRTOS task stack for async |
| `SHC_ASYNC_TASK_PRIORITY` | 1 | FreeRTOS task priority |
| `SHC_ASYNC_TASK_CORE` | -1 | FreeRTOS task core (-1 = any) |
| `SHC_GZIP_OUT_BUF_SIZE` | 16384 | Decompressor output buffer |

---

## Error codes

Negative return values indicate errors. Positive values are HTTP status codes.

| Code | Constant | Meaning |
| --- | --- | --- |
| -1  | `SHC_ERROR_CONNECTION_REFUSED` | TCP/TLS connect failed |
| -2  | `SHC_ERROR_SEND_HEADER_FAILED` | Could not write request line/headers |
| -3  | `SHC_ERROR_SEND_PAYLOAD_FAILED` | Could not write request body |
| -4  | `SHC_ERROR_NO_HTTP_SERVER` | Server did not respond with HTTP/1.x |
| -5  | `SHC_ERROR_CONNECTION_LOST` | TCP connection dropped mid-request |
| -6  | `SHC_ERROR_NO_DATA` | No response data within timeout |
| -7  | `SHC_ERROR_READ_RESPONSE` | Garbage in response headers |
| -8  | `SHC_ERROR_TOO_LESS_RAM` | Allocation failed |
| -9  | `SHC_ERROR_STREAM_WRITE` | Caller-provided upload stream error |
| -10 | `SHC_ERROR_CHUNK_SIZE` | Malformed chunked size line |
| -11 | `SHC_ERROR_DECOMPRESSION` | gzip/deflate decoder error |
| -13 | `SHC_ERROR_CACERT` | TLS certificate rejected |
| -15 | `SHC_ERROR_REDIRECT_EXCEEDED` | Too many redirects |
| -16 | `SHC_ERROR_INVALID_URL` | URL could not be parsed |
| -17 | `SHC_ERROR_TASK_CREATE` | Could not create FreeRTOS task |
| -18 | `SHC_ERROR_ALREADY_RUNNING` | Async request already in flight |
| -19 | `SHC_ERROR_USER_CANCEL` | Cancelled via `asyncStop()` |
| -20 | `SHC_ERROR_UNSUPPORTED_SCHEME` | Only `http` and `https` supported |
| -99 | `SHC_ERROR_UNKNOWN` | Unknown error |

Use `StreamHTTPClient::errorToString(code)` to get a human-readable string.

---

## Thread-safety notes (ESP32)

The async path runs the request in its own FreeRTOS task. Callbacks fire
from that task. Inside callbacks:

- ✅ Safe: `Serial.print`, writing to a `WiFiClient`, allocating memory,
  setting flags, writing to a `Stream`/`File`.
- ❌ Not safe: calling back into the same `StreamHTTPClient` instance
  (e.g. calling `http.end()` from inside `onComplete()` may deadlock).
  Use a flag instead and call `end()` from `loop()`.

If your callback needs to do heavy work, prefer to copy the bytes out and
process them in `loop()`.

---

## License

- StreamHTTPClient source code: **MIT** (see `LICENSE`)
- Bundled [miniz](https://github.com/richgel999/miniz): **MIT / Public Domain**
  (see `src/miniz.c` header)

---

## Acknowledgements

- Espressif's [`HTTPClient`](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)
  for the original API design that this library is compatible with.
- Rich Geldreich and contributors for [miniz](https://github.com/richgel999/miniz).
- The Arduino community for the `WiFiClient` / `WiFiClientSecure` interface.
