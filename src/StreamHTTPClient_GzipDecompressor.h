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
  StreamHTTPClient_GzipDecompressor.h
  Streaming gzip / deflate decompressor built on top of miniz.

  Design goals:
    * Constant RAM footprint (no full-document buffering).
    * One-pass streaming: feed bytes in, get bytes out.
    * Transparent handling of both "gzip" (RFC 1952) and "deflate"
      (RFC 1950 / RFC 1951) Content-Encoding.
    * Self-contained: the caller does not need to know whether the upstream
      bytes are gzip or raw deflate.

  miniz is shipped inside this library (src/miniz.c, src/miniz.h) and is
  compiled with MINIZ_NO_ARCHIVE_APIS, MINIZ_NO_STDIO and MINIZ_NO_TIME so
  that only the inflate path is linked.
*/

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <string.h>

// Configure miniz before including its header.
// These defines must be visible to both the wrapper and miniz.c when it is
// compiled, so they live here in the public header.
#ifndef MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#ifndef MINIZ_NO_TIME
#define MINIZ_NO_TIME
#endif

#include "miniz.h"

class StreamHTTPClient_GzipDecompressor {
public:
    // enc can be "gzip", "x-gzip", "deflate", "x-deflate".
    // Anything else (including "") is treated as raw deflate.
    StreamHTTPClient_GzipDecompressor(const char* enc = "gzip");
    ~StreamHTTPClient_GzipDecompressor();

    // Reset the decoder so the same object can be reused for a new response.
    // Safe to call from any state.
    void reset(const char* enc = nullptr);

    // Feed input bytes; emit decoded bytes via the out callback.
    // Returns:
    //   >0   number of input bytes consumed
    //    0   nothing consumed yet (need more output room)
    //   -1   finished cleanly (stream end reached)
    //   -2   decoder error (data corruption)
    //
    // decoded_callback is invoked zero or more times with chunks of decoded
    // data. The callback may store or process the bytes; it must not call
    // back into this object.
    typedef void (*DecodedCallback)(const uint8_t* data, size_t len, void* user);
    int decode(const uint8_t* in, size_t in_len, bool final_chunk,
               uint8_t* out_buf, size_t out_len,
               DecodedCallback cb, void* user);

    // Convenience: decode into a Print (Stream, WiFiClient, Serial, ...).
    int decodeTo(const uint8_t* in, size_t in_len, bool final_chunk,
                 Print& out);

    // True once the underlying inflater has signalled MZ_STREAM_END.
    bool finished() const { return _finished; }

    // Total bytes produced so far.
    size_t totalOut() const { return _total_out; }

    // Total bytes consumed so far.
    size_t totalIn() const { return _total_in; }

    // Human-readable last error (empty if none).
    String lastError() const { return _last_error; }

private:
    enum Encoding {
        ENC_GZIP,    // RFC 1952 (gzip magic 1f 8b)
        ENC_DEFLATE, // RFC 1950 (zlib wrapper, no gzip magic)
        ENC_RAW,     // RFC 1951 (raw deflate, no wrapper)
    };

    Encoding _enc;
    bool     _started;
    bool     _finished;
    bool     _saw_gzip_magic;
    size_t   _total_in;
    size_t   _total_out;
    String   _last_error;

    mz_stream _stream; // miniz stream

    // gzip (RFC 1952) header parser state
    struct GzipHeader {
        enum Stage { MAGIC, METHOD_FLAG, MTIME_XFL_OS, XLEN, FEXTRA, FNAME, FCOMMENT, FHCRC, DONE };
        Stage stage;
        size_t need;
        size_t have;
        uint8_t buf[16];
        uint8_t flags;
    } _gz;

    void   init_stream();
    bool   detect_encoding(const uint8_t* in, size_t in_len);
    size_t consume_gzip_header(const uint8_t* in, size_t in_len);
    void   set_error(const char* msg);
};
