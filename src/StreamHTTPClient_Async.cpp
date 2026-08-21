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
  StreamHTTPClient_Async.cpp - asynchronous request support implementation
*/

#include "StreamHTTPClient_Async.h"

#if SHC_HAS_FREERTOS
  // ESP32 / RP2040 with FreeRTOS
  #if defined(ESP32)
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    // xTaskCreateUniversal signature changed between Arduino-ESP32 v2 and v3
    // (v3 added an xCoreID parameter at the end). xTaskCreatePinnedToCore has
    // a stable signature across all versions, so we use it directly.
  #else
    #include <FreeRTOS.h>
    #include <task.h>
  #endif
  #define SHC_TASK_TYPE TaskHandle_t
  #define SHC_TASK_DELETE(handle) vTaskDelete((TaskHandle_t)(handle))
  #define SHC_TASK_DELAY_MS(ms)   vTaskDelay(pdMS_TO_TICKS(ms))

  // Spawn the async task. Returns pdPASS on success. Implementation chosen
  // per-platform so we work across Arduino-ESP32 v2/v3, RP2040, etc.
  static inline BaseType_t shcSpawnAsyncTask(TaskFunction_t entry,
                                              const char* name,
                                              uint32_t stack,
                                              void* arg,
                                              UBaseType_t prio,
                                              TaskHandle_t* handle) {
#if defined(ESP32)
      // On ESP32 we use xTaskCreatePinnedToCore so we can target a specific
      // core or fall back to "any core" via tskNO_AFFINITY. This function has
      // a stable signature in both Arduino-ESP32 v2.x and v3.x.
      BaseType_t core_id;
  #if CONFIG_FREERTOS_UNICORE
      // Single-core variant (ESP32-S2, ESP32-C3, ESP32-S3 with unicore, ...).
      core_id = 0;
  #else
      // Dual-core: tskNO_AFFINITY lets the scheduler choose.
      core_id = (SHC_ASYNC_TASK_CORE >= 0 && SHC_ASYNC_TASK_CORE < portNUM_PROCESSORS)
                ? (BaseType_t)SHC_ASYNC_TASK_CORE
                : tskNO_AFFINITY;
  #endif
      return xTaskCreatePinnedToCore(entry, name, stack, arg, prio, handle, core_id);
#else
      // Generic FreeRTOS (RP2040, etc.) - xTaskCreate with no core pinning.
      return xTaskCreate(entry, name, stack, arg, prio, handle);
#endif
  }
#else
  #define SHC_TASK_TYPE void*
  #define SHC_TASK_DELAY_MS(ms) delay(ms)
#endif

StreamHTTPClient_AsyncImpl::StreamHTTPClient_AsyncImpl(StreamHTTPClient* parent)
    : _parent(parent),
      _has_body_str(false),
      _has_body_buf(false),
      _body_buf(nullptr),
      _body_size(0),
      _has_body_stream(false),
      _body_stream(nullptr),
      _body_stream_size(0),
      _stream_chunked(false),
      _running(false),
      _state(SHC_STATE_IDLE),
      _status_code(0),
      _last_err(0)
#if SHC_HAS_FREERTOS
      , _task_handle(nullptr), _use_task(true)
#endif
{
}

StreamHTTPClient_AsyncImpl::~StreamHTTPClient_AsyncImpl() {
    stop();
}

bool StreamHTTPClient_AsyncImpl::start(const char* method, const String& body) {
    if (_running) { _last_err = SHC_ERROR_ALREADY_RUNNING; return false; }
    _method = method;
    _has_body_str = true; _body_str = body;
    _has_body_buf = false; _has_body_stream = false;
    _running = true;
    _state = SHC_STATE_CONNECTING;
    _status_code = 0;
    _last_err = 0;

#if SHC_HAS_FREERTOS
    if (_use_task) {
        TaskHandle_t h = nullptr;
        BaseType_t r = shcSpawnAsyncTask((TaskFunction_t)taskEntry,
                                         "shc_async",
                                         SHC_ASYNC_TASK_STACK,
                                         this,
                                         SHC_ASYNC_TASK_PRIORITY,
                                         &h);
        if (r != pdPASS) {
            _last_err = SHC_ERROR_TASK_CREATE;
            _running = false;
            _state = SHC_STATE_ERROR;
            return false;
        }
        _task_handle = (void*)h;
        return true;
    }
#endif
    // Inline execution fallback
    runInline();
    return true;
}

bool StreamHTTPClient_AsyncImpl::start(const char* method, uint8_t* body, size_t size) {
    if (_running) { _last_err = SHC_ERROR_ALREADY_RUNNING; return false; }
    _method = method;
    _has_body_buf = true;
    _body_buf = body;
    _body_size = size;
    _has_body_str = false; _has_body_stream = false;
    _running = true;
    _state = SHC_STATE_CONNECTING;
    _status_code = 0;
    _last_err = 0;

#if SHC_HAS_FREERTOS
    if (_use_task) {
        TaskHandle_t h = nullptr;
        BaseType_t r = shcSpawnAsyncTask((TaskFunction_t)taskEntry,
                                         "shc_async",
                                         SHC_ASYNC_TASK_STACK,
                                         this,
                                         SHC_ASYNC_TASK_PRIORITY,
                                         &h);
        if (r != pdPASS) {
            _last_err = SHC_ERROR_TASK_CREATE;
            _running = false;
            _state = SHC_STATE_ERROR;
            return false;
        }
        _task_handle = (void*)h;
        return true;
    }
#endif
    runInline();
    return true;
}

bool StreamHTTPClient_AsyncImpl::start(const char* method, Stream* body, size_t size) {
    if (_running) { _last_err = SHC_ERROR_ALREADY_RUNNING; return false; }
    _method = method;
    _has_body_stream = true;
    _body_stream = body;
    _body_stream_size = size;
    _has_body_str = false; _has_body_buf = false;
    _stream_chunked = (size == (size_t)-1);
    _running = true;
    _state = SHC_STATE_CONNECTING;
    _status_code = 0;
    _last_err = 0;

#if SHC_HAS_FREERTOS
    if (_use_task) {
        TaskHandle_t h = nullptr;
        BaseType_t r = shcSpawnAsyncTask((TaskFunction_t)taskEntry,
                                         "shc_async",
                                         SHC_ASYNC_TASK_STACK,
                                         this,
                                         SHC_ASYNC_TASK_PRIORITY,
                                         &h);
        if (r != pdPASS) {
            _last_err = SHC_ERROR_TASK_CREATE;
            _running = false;
            _state = SHC_STATE_ERROR;
            return false;
        }
        _task_handle = (void*)h;
        return true;
    }
#endif
    runInline();
    return true;
}

bool StreamHTTPClient_AsyncImpl::startStream(const char* method, Stream* upload,
                                             size_t upload_size, bool chunked) {
    if (_running) { _last_err = SHC_ERROR_ALREADY_RUNNING; return false; }
    _method = method;
    _has_body_stream = true;
    _body_stream = upload;
    _body_stream_size = upload_size;
    _stream_chunked = chunked;
    _has_body_str = false; _has_body_buf = false;
    _running = true;
    _state = SHC_STATE_CONNECTING;
    _status_code = 0;
    _last_err = 0;

#if SHC_HAS_FREERTOS
    if (_use_task) {
        TaskHandle_t h = nullptr;
        BaseType_t r = shcSpawnAsyncTask((TaskFunction_t)taskEntry,
                                         "shc_async",
                                         SHC_ASYNC_TASK_STACK,
                                         this,
                                         SHC_ASYNC_TASK_PRIORITY,
                                         &h);
        if (r != pdPASS) {
            _last_err = SHC_ERROR_TASK_CREATE;
            _running = false;
            _state = SHC_STATE_ERROR;
            return false;
        }
        _task_handle = (void*)h;
        return true;
    }
#endif
    runInline();
    return true;
}

void StreamHTTPClient_AsyncImpl::stop() {
    if (!_running) return;
    _parent->_cancelled = true;
    // Wait briefly for the task to notice
    uint32_t start = millis();
    while (_running && (millis() - start) < 200) {
        SHC_TASK_DELAY_MS(1);
    }
#if SHC_HAS_FREERTOS
    if (_task_handle) {
        SHC_TASK_DELETE(_task_handle);
        _task_handle = nullptr;
    }
#endif
    _running = false;
    _state = SHC_STATE_IDLE;
}

bool StreamHTTPClient_AsyncImpl::poll() {
#if SHC_HAS_FREERTOS
    if (_use_task) {
        // Task mode: nothing to do here; the task drives itself.
        return _running;
    }
#endif
    // Cooperative mode: state machine is already finished because runInline()
    // blocked until completion. So poll() just returns whether we are still
    // running (we never are in cooperative mode after start() returns).
    return _running;
}

int StreamHTTPClient_AsyncImpl::waitComplete(uint32_t timeoutMs) {
    if (!_running) return _last_err ? _last_err : _status_code;
    uint32_t start = millis();
    while (_running) {
        if (timeoutMs && (millis() - start) >= timeoutMs) {
            return SHC_ERROR_NO_DATA;
        }
        SHC_TASK_DELAY_MS(1);
    }
    return _last_err ? _last_err : _status_code;
}

void StreamHTTPClient_AsyncImpl::runInline() {
    execute();
}

#if SHC_HAS_FREERTOS
void StreamHTTPClient_AsyncImpl::taskEntry(void* arg) {
    StreamHTTPClient_AsyncImpl* self = (StreamHTTPClient_AsyncImpl*)arg;
    self->runInTask();
    // Task self-deletes
    vTaskDelete(nullptr);
}

void StreamHTTPClient_AsyncImpl::runInTask() {
    execute();
    _running = false;
    _state = _last_err ? SHC_STATE_ERROR : SHC_STATE_DONE;
    _task_handle = nullptr;
}
#endif

void StreamHTTPClient_AsyncImpl::execute() {
    // Drive the parent's performRequest with the chosen body source.
    int result = 0;
    const char* method = _method.c_str();

    if (_has_body_str) {
        const uint8_t* p = (const uint8_t*)_body_str.c_str();
        result = _parent->performRequest(method, p, _body_str.length(),
                                         nullptr, false, false);
    } else if (_has_body_buf) {
        result = _parent->performRequest(method, _body_buf, _body_size,
                                         nullptr, false, false);
    } else if (_has_body_stream) {
        result = _parent->performRequest(method, nullptr, _body_stream_size,
                                         _body_stream, _stream_chunked, true);
    } else {
        result = _parent->performRequest(method, nullptr, 0,
                                         nullptr, false, false);
    }

    if (result < 0) {
        _last_err = result;
        _state = SHC_STATE_ERROR;
    } else {
        _status_code = result;
        _state = SHC_STATE_DONE;
    }
    _running = false;
}
