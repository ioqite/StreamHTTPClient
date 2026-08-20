/*
  StreamHTTPClient_GzipDecompressor.cpp
  Streaming gzip / deflate decompressor implementation.
*/

#include "StreamHTTPClientConst.h"
#include "StreamHTTPClient_GzipDecompressor.h"

static const char* gz_err_str(int r) {
    switch (r) {
        case MZ_OK:              return "ok";
        case MZ_STREAM_END:      return "stream end";
        case MZ_NEED_DICT:       return "need dict";
        case MZ_ERRNO:           return "errno";
        case MZ_STREAM_ERROR:    return "stream error";
        case MZ_DATA_ERROR:      return "data error";
        case MZ_MEM_ERROR:       return "mem error";
        case MZ_BUF_ERROR:       return "buf error";
        case MZ_VERSION_ERROR:   return "version error";
        case MZ_PARAM_ERROR:     return "param error";
        default:                 return "unknown";
    }
}

StreamHTTPClient_GzipDecompressor::StreamHTTPClient_GzipDecompressor(const char* enc)
    : _enc(ENC_GZIP),
      _started(false),
      _finished(false),
      _saw_gzip_magic(false),
      _total_in(0),
      _total_out(0) {
    memset(&_stream, 0, sizeof(_stream));
    memset(&_gz, 0, sizeof(_gz));
    if (enc) reset(enc);
}

StreamHTTPClient_GzipDecompressor::~StreamHTTPClient_GzipDecompressor() {
    if (_started) {
        inflateEnd(&_stream);
        _started = false;
    }
}

void StreamHTTPClient_GzipDecompressor::reset(const char* enc) {
    if (_started) {
        inflateEnd(&_stream);
        _started = false;
    }
    memset(&_stream, 0, sizeof(_stream));
    memset(&_gz, 0, sizeof(_gz));
    _finished = false;
    _saw_gzip_magic = false;
    _total_in = 0;
    _total_out = 0;
    _last_error = "";

    if (enc) {
        // Normalise: case-insensitive compare
        String e(enc);
        e.toLowerCase();
        if (e == "gzip" || e == "x-gzip") {
            _enc = ENC_GZIP;
        } else if (e == "deflate" || e == "x-deflate") {
            // "deflate" per RFC 7230 is ambiguous; we auto-detect on first bytes.
            _enc = ENC_DEFLATE;
        } else {
            _enc = ENC_RAW;
        }
    }
    // If enc is nullptr we keep the previously configured encoding.
}

void StreamHTTPClient_GzipDecompressor::init_stream() {
    // window_bits:
    //   MZ_DEFAULT_WINDOW_BITS (15)  -> zlib-wrapped
    //   -MZ_DEFAULT_WINDOW_BITS      -> gzip-wrapped (RFC 1952)
    //   -MZ_DEFAULT_WINDOW_BITS - 30 -> raw deflate (RFC 1951) per miniz docs
    int window_bits;
    if (_enc == ENC_RAW) {
        window_bits = -MZ_DEFAULT_WINDOW_BITS;
    } else if (_enc == ENC_GZIP) {
        window_bits = -MZ_DEFAULT_WINDOW_BITS; // gzip magic auto-handled by miniz when negative
    } else {
        window_bits = MZ_DEFAULT_WINDOW_BITS; // zlib wrapper
    }
    int r = inflateInit2(&_stream, window_bits);
    if (r != MZ_OK) {
        set_error("inflateInit2 failed");
        return;
    }
    _started = true;
}

void StreamHTTPClient_GzipDecompressor::set_error(const char* msg) {
    _last_error = String(msg);
}

int StreamHTTPClient_GzipDecompressor::decode(const uint8_t* in, size_t in_len,
                                              bool final_chunk,
                                              uint8_t* out_buf, size_t out_len,
                                              DecodedCallback cb, void* user) {
    if (_finished) return -1;
    if (in_len == 0 && !final_chunk) return 0;
    if (out_len == 0) return 0;

    // First call: detect actual encoding for "deflate" Content-Encoding.
    // Many servers mislabel raw deflate as "deflate"; we therefore peek at
    // the first bytes and auto-detect.
    if (!_started && _enc == ENC_DEFLATE && in_len > 0) {
        if (in[0] == 0x1f && in_len >= 2 && in[1] == 0x8b) {
            // Server labelled a gzip stream as "deflate".
            _enc = ENC_GZIP;
        } else {
            // Otherwise treat as raw deflate.
            _enc = ENC_RAW;
        }
    }

    if (!_started) {
        init_stream();
        if (!_started) return -2;
    }

    size_t consumed_in = 0;
    _stream.next_in   = const_cast<mz_uint8*>(in);
    _stream.avail_in  = in_len;
    _stream.next_out  = out_buf;
    _stream.avail_out = out_len;

    while (_stream.avail_in > 0 && _stream.avail_out > 0) {
        int r = inflate(&_stream, MZ_NO_FLUSH);
        size_t produced = out_len - _stream.avail_out;
        if (produced > 0) {
            if (cb) cb(out_buf, produced, user);
            _total_out += produced;
            // Reset output position so we can produce more
            _stream.next_out  = out_buf;
            _stream.avail_out = out_len;
        }
        size_t consumed_now = in_len - _stream.avail_in;
        consumed_in = consumed_now;

        if (r == MZ_STREAM_END) {
            _finished = true;
            break;
        } else if (r != MZ_OK) {
            String err = "inflate: ";
            err += gz_err_str(r);
            set_error(err.c_str());
            return -2;
        }
    }

    // If this is the final chunk and we still have room, flush the decoder.
    if (final_chunk && !_finished) {
        while (true) {
            int r = inflate(&_stream, MZ_FINISH);
            size_t produced = out_len - _stream.avail_out;
            if (produced > 0) {
                if (cb) cb(out_buf, produced, user);
                _total_out += produced;
                _stream.next_out  = out_buf;
                _stream.avail_out = out_len;
            }
            if (r == MZ_STREAM_END) {
                _finished = true;
                break;
            }
            if (r == MZ_BUF_ERROR) {
                // Need more input or output room - but input is exhausted.
                if (produced == 0) break;
                continue;
            }
            if (r != MZ_OK) {
                String err = "inflate(finish): ";
                err += gz_err_str(r);
                set_error(err.c_str());
                return -2;
            }
        }
    }

    _total_in += consumed_in;
    if (_finished) return -1;
    return (int)consumed_in;
}

int StreamHTTPClient_GzipDecompressor::decodeTo(const uint8_t* in, size_t in_len,
                                                bool final_chunk, Print& out) {
    uint8_t out_buf[SHC_GZIP_OUT_BUF_SIZE];
    struct Ctx { Print* out; };
    Ctx ctx{&out};
    auto cb = [](const uint8_t* data, size_t len, void* user) {
        Ctx* c = (Ctx*)user;
        c->out->write(data, len);
    };
    return decode(in, in_len, final_chunk, out_buf, sizeof(out_buf), cb, &ctx);
}
