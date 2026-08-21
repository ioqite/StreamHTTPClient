/*
  StreamHTTPClient - Constants and configuration macros

  This library is API-compatible with the Arduino HTTPClient but adds:
    - Bidirectional streaming (request body and response body via Stream)
    - Chunked transfer encoding (send and receive)
    - gzip / deflate decompression (powered by miniz)
    - Keep-Alive connection reuse
    - Configurable redirect following
    - Basic Authentication
    - Custom headers
    - Server-Sent Events (SSE) integrated into the main class
    - Asynchronous request execution using a FreeRTOS task
    - Cooperative polling for platforms without FreeRTOS

  Reference: github.com/espressif/arduino-esp32/libraries/HTTPClient
*/

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Library version
// ---------------------------------------------------------------------------
#define STREAM_HTTP_CLIENT_VERSION "1.0.0"
#define STREAM_HTTP_CLIENT_VERSION_MAJOR 1
#define STREAM_HTTP_CLIENT_VERSION_MINOR 0
#define STREAM_HTTP_CLIENT_VERSION_PATCH 0

// ---------------------------------------------------------------------------
// Configuration knobs - override before including StreamHTTPClient.h
// ---------------------------------------------------------------------------

// Maximum number of custom request headers. Each header consumes ~64 bytes
// of static RAM inside the client object.
#ifndef SHC_MAX_HEADERS
#define SHC_MAX_HEADERS 16
#endif

// Maximum number of headers to *collect* (so they can later be queried by name
// with header()).
#ifndef SHC_MAX_COLLECT_HEADERS
#define SHC_MAX_COLLECT_HEADERS 16
#endif

// Maximum length of a single header line (key + ": " + value) stored in RAM.
#ifndef SHC_MAX_HEADER_LINE
#define SHC_MAX_HEADER_LINE 256
#endif

// Maximum number of HTTP redirects to follow. 0 = no redirect following.
#ifndef SHC_MAX_REDIRECTS
#define SHC_MAX_REDIRECTS 10
#endif

// Default receive buffer used when the response is collected as a String.
#ifndef SHC_DEFAULT_STRING_BUFFER
#define SHC_DEFAULT_STRING_BUFFER 4096
#endif

// Default connect timeout in milliseconds.
#ifndef SHC_DEFAULT_CONNECT_TIMEOUT
#define SHC_DEFAULT_CONNECT_TIMEOUT 5000
#endif

// Default TCP read timeout in milliseconds.
#ifndef SHC_DEFAULT_TCP_TIMEOUT
#define SHC_DEFAULT_TCP_TIMEOUT 5000
#endif

// Default User-Agent string sent when setUserAgent() has not been called.
#ifndef SHC_DEFAULT_USER_AGENT
#define SHC_DEFAULT_USER_AGENT "StreamHTTPClient/1.0"
#endif

// Size of the chunked-encoding line buffer (must hold a hex length + CRLF).
#ifndef SHC_CHUNK_LINE_BUFFER
#define SHC_CHUNK_LINE_BUFFER 32
#endif

// Size of the read buffer used by the SSE line parser.
#ifndef SHC_SSE_LINE_BUFFER
#define SHC_SSE_LINE_BUFFER 1024
#endif

// Stack size (bytes) of the FreeRTOS task used for async requests.
//
// This task runs the full request state machine including URL parsing, TLS
// handshake (mbedTLS uses ~4 KB of stack), HTTP header parsing, chunked
// decoding and any user callbacks. We default to 12 KB so there is a safety
// margin on top of the ~8 KB the actual call chain needs.
//
// Override with -DSHC_ASYNC_TASK_STACK=16384 (or in library.properties) if
// your callbacks use deep call chains or large stack buffers.
#ifndef SHC_ASYNC_TASK_STACK
#define SHC_ASYNC_TASK_STACK 12288
#endif

// Priority of the FreeRTOS task used for async requests.
#ifndef SHC_ASYNC_TASK_PRIORITY
#define SHC_ASYNC_TASK_PRIORITY 1
#endif

// CPU core to pin the async task to. -1 = no affinity (any core).
#ifndef SHC_ASYNC_TASK_CORE
#define SHC_ASYNC_TASK_CORE -1
#endif

// Decompression output buffer used by the gzip wrapper.
#ifndef SHC_GZIP_OUT_BUF_SIZE
#define SHC_GZIP_OUT_BUF_SIZE 16384
#endif

// ---------------------------------------------------------------------------
// Internal marks (do not edit)
// ---------------------------------------------------------------------------
#define SHC_HAS_LOCATION_HEADER 0x01
#define SHC_HAS_TRANSFER_ENCODING 0x02
#define SHC_HAS_CONTENT_LENGTH 0x04
#define SHC_HAS_CONNECTION_CLOSE 0x08
#define SHC_HAS_CONTENT_ENCODING_GZIP 0x10
#define SHC_HAS_CONTENT_ENCODING_DEFLATE 0x20
