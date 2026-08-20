/*
  StreamHTTPClient_Async.h - asynchronous request support

  Two execution strategies:
    1. FreeRTOS task (ESP32, RP2040 with FreeRTOS, etc.) - the request runs
       in its own task and pushes callbacks to the user. The user does not
       need to call asyncPoll(); callbacks fire from the task. However, the
       user MUST ensure their callbacks are task-safe (no blocking I/O).
    2. Cooperative polling (AVR, SAMD, ESP8266 with no task system) - the
       state machine is advanced one step at a time by calling asyncPoll()
       from the user's loop().

  In both modes, callbacks are invoked from the SAME context that drives
  the state machine (task or loop). This keeps things predictable.
*/

#pragma once

#include "StreamHTTPClient.h"

class StreamHTTPClient_AsyncImpl {
public:
    StreamHTTPClient_AsyncImpl(StreamHTTPClient* parent);
    ~StreamHTTPClient_AsyncImpl();

    // Start a request. The body variants are mutually exclusive - call only one.
    bool start(const char* method, const String& body);
    bool start(const char* method, uint8_t* body, size_t size);
    bool start(const char* method, Stream* body, size_t size);
    bool startStream(const char* method, Stream* upload, size_t upload_size, bool chunked);

    void stop();           // cancel current request
    bool poll();           // advance the state machine (cooperative mode only)
    bool isRunning() const { return _running; }
    StreamHTTPClient_State state() const { return _state; }
    int  statusCode() const { return _status_code; }
    int  lastError() const { return _last_err; }
    int  waitComplete(uint32_t timeoutMs = 0);  // blocking wait

private:
    StreamHTTPClient* _parent;

    // Request description
    String  _method;
    bool    _has_body_str;
    String  _body_str;
    bool    _has_body_buf;
    const uint8_t* _body_buf;
    size_t  _body_size;
    bool    _has_body_stream;
    Stream* _body_stream;
    size_t  _body_stream_size;
    bool    _stream_chunked;

    // State
    volatile bool _running;
    volatile StreamHTTPClient_State _state;
    volatile int  _status_code;
    volatile int  _last_err;

#if SHC_HAS_FREERTOS
    // FreeRTOS task handle
    void*  _task_handle;  // TaskHandle_t
    bool   _use_task;     // true = use task, false = cooperative
    static void taskEntry(void* arg);
    void   runInTask();
#endif

    void runInline();      // runs synchronously inside start() (used when no task)
    void execute();        // performs the actual request
};
