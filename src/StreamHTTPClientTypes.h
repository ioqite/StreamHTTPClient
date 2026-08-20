/*
  StreamHTTPClient - Public types and enums

  Mirrors the naming style of the upstream Arduino HTTPClient so existing
  codebases can be ported with minimal changes.
*/

#pragma once

#include <Arduino.h>
#include <Client.h>
#include <Stream.h>

// ---------------------------------------------------------------------------
// HTTP method enum (stringifiable)
// ---------------------------------------------------------------------------
enum StreamHTTPClient_Method {
    SHC_HTTP_METHOD_GET     = 0,
    SHC_HTTP_METHOD_POST    = 1,
    SHC_HTTP_METHOD_PUT     = 2,
    SHC_HTTP_METHOD_PATCH   = 3,
    SHC_HTTP_METHOD_DELETE  = 4,
    SHC_HTTP_METHOD_HEAD    = 5,
    SHC_HTTP_METHOD_OPTIONS = 6,
};

// Convert a method enum to its uppercase string ("GET", "POST", ...).
const char* shc_method_to_string(StreamHTTPClient_Method m);

// ---------------------------------------------------------------------------
// Request / connection errors
//
// Negative codes mirror HTTPClient for source-level compatibility.
// ---------------------------------------------------------------------------
enum StreamHTTPClient_RequestError : int {
    SHC_ERROR_CONNECTION_REFUSED      = -1,  // TCP/TLS connect failed
    SHC_ERROR_SEND_HEADER_FAILED      = -2,  // Could not write request line/headers
    SHC_ERROR_SEND_PAYLOAD_FAILED     = -3,  // Could not write request body
    SHC_ERROR_NO_HTTP_SERVER          = -4,  // Server did not respond with HTTP/1.x
    SHC_ERROR_CONNECTION_LOST         = -5,  // TCP connection dropped mid-request
    SHC_ERROR_NO_DATA                 = -6,  // No response data within timeout
    SHC_ERROR_READ_RESPONSE           = -7,  // Garbage in response headers
    SHC_ERROR_TOO_LESS_RAM            = -8,  // Allocation failed
    SHC_ERROR_STREAM_WRITE            = -9,  // Caller-provided upload stream write error
    SHC_ERROR_CHUNK_SIZE              = -10, // Malformed chunked size line
    SHC_ERROR_DECOMPRESSION           = -11, // gzip/deflate decoder error
    SHC_ERROR_SHA_LENGTH              = -12, // SHA mismatch (kept for compat)
    SHC_ERROR_CACERT                  = -13, // TLS certificate rejected
    SHC_ERROR_HTTP_CODE_LATER         = -14, // Response not yet received (async)
    SHC_ERROR_REDIRECT_EXCEEDED       = -15, // Too many redirects
    SHC_ERROR_INVALID_URL             = -16, // URL could not be parsed
    SHC_ERROR_TASK_CREATE             = -17, // Could not create FreeRTOS task
    SHC_ERROR_ALREADY_RUNNING         = -18, // Async request already in flight
    SHC_ERROR_USER_CANCEL             = -19, // Cancelled via asyncStop()
    SHC_ERROR_UNSUPPORTED_SCHEME      = -20, // Only http and https are supported
    SHC_ERROR_UNKNOWN                 = -99,
};

// Convert an error code to a human-readable string.
String shc_error_to_string(int err);

// ---------------------------------------------------------------------------
// Redirect policy
// ---------------------------------------------------------------------------
enum StreamHTTPClient_RedirectPolicy : uint8_t {
    SHC_REDIRECT_OFF              = 0,  // Never follow redirects
    SHC_REDIRECT_FOLLOW_GET_ONLY  = 1,  // Only follow on GET/HEAD, switch method to GET
    SHC_REDIRECT_FOLLOW_ALL       = 2,  // Follow on any method, may resend body
};

// ---------------------------------------------------------------------------
// State of an async request. Async callers can poll state() in their loop().
// ---------------------------------------------------------------------------
enum StreamHTTPClient_State : uint8_t {
    SHC_STATE_IDLE          = 0,  // No request in flight
    SHC_STATE_CONNECTING    = 1,  // TCP/TLS connect in progress
    SHC_STATE_SENDING_REQ   = 2,  // Writing request line + headers
    SHC_STATE_SENDING_BODY  = 3,  // Writing request body (upload stream)
    SHC_STATE_READING_HEAD  = 4,  // Reading response status line + headers
    SHC_STATE_READING_BODY  = 5,  // Reading response body (download)
    SHC_STATE_REDIRECTING   = 6,  // Closing and re-opening for a redirect
    SHC_STATE_DECOMPRESSING = 7,  // Draining bytes through the gzip decoder
    SHC_STATE_DONE          = 8,  // Request finished cleanly (state code visible)
    SHC_STATE_ERROR         = 9,  // Request failed (see error code)
};

// ---------------------------------------------------------------------------
// Reason for a callback invocation (used by onData / onChunk / onProgress).
// ---------------------------------------------------------------------------
enum StreamHTTPClient_CallbackReason : uint8_t {
    SHC_CB_REASON_DATA         = 0,  // Raw response bytes (after de-chunking/decompression)
    SHC_CB_REASON_CHUNK        = 1,  // One chunk boundary in chunked transfer
    SHC_CB_REASON_RESPONSE_HDR = 2,  // Response header line
    SHC_CB_REASON_PROGRESS     = 3,  // Periodic progress update
    SHC_CB_REASON_COMPLETE     = 4,  // Response finished cleanly
    SHC_CB_REASON_ERROR        = 5,  // An error occurred
    SHC_CB_REASON_DISCONN      = 6,  // Connection was closed (Keep-Alive EOF)
};

// ---------------------------------------------------------------------------
// Progress information passed to onProgress().
// ---------------------------------------------------------------------------
struct StreamHTTPClient_Progress {
    size_t bytes_sent;        // Bytes uploaded so far (0 if no upload)
    size_t total_sent;        // Expected upload size (0 if unknown / chunked)
    size_t bytes_received;    // Bytes downloaded so far (after decompression)
    size_t total_received;    // Expected download size (0 if unknown / chunked)
    uint32_t elapsed_ms;      // Time since request start
};

// ---------------------------------------------------------------------------
// SSE event - passed to StreamHTTPClient::onEvent().
// ---------------------------------------------------------------------------
struct SSEEvent {
    String event;   // value of the "event:" field, "" if not present (defaults to "message")
    String data;    // value of one or more "data:" lines, joined by '\n'
    String id;      // value of the "id:" field, "" if not present
    uint32_t retry; // value of the "retry:" field, 0 if not present (do not change)
};

// ---------------------------------------------------------------------------
// Abstract upload-stream interface.
//
// Implement this to feed a request body chunk-by-chunk. It is intentionally
// compatible with Arduino Stream so you can pass &Serial, &File, &WiFiClient
// etc. directly via the Stream* overloads of sendRequestStream().
// ---------------------------------------------------------------------------
class StreamHTTPClient_UploadSource {
public:
    virtual ~StreamHTTPClient_UploadSource() {}
    virtual int available() = 0;          // bytes remaining, -1 = unknown (chunked upload)
    virtual size_t read(uint8_t* buf, size_t len) = 0;
    virtual void reset() {}               // optional - called on redirect
};

// ---------------------------------------------------------------------------
// Abstract download sink interface (optional - used by async download sink).
// ---------------------------------------------------------------------------
class StreamHTTPClient_DownloadSink {
public:
    virtual ~StreamHTTPClient_DownloadSink() {}
    // Called repeatedly with raw (de-chunked, decompressed) bytes.
    // Return false to abort the request.
    virtual bool write(const uint8_t* buf, size_t len) = 0;
};
