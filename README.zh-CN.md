# StreamHTTPClient
### [English](./README.md) | 简体中文

一个面向 Arduino 的流式 HTTP/HTTPS 客户端，提供同步**与**异步 API，支持双向流式传输、Server-Sent Events (SSE)、分块传输编码、gzip/deflate 解压、Keep-Alive、重定向以及基本认证（Basic Authentication）。

公共 API 与上游 Arduino
[`HTTPClient`](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)
在常见调用模式（`begin`、`GET`、`POST`、`addHeader`、`getString`……）上**完全兼容**，因此现有 sketch 只需极少改动即可完成移植。在此基础上，StreamHTTPClient 还提供了原生 `HTTPClient` 所缺失的一流流式传输、异步、SSE 以及解压支持。

参考实现：[github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32)。

---

## 功能特性

| 特性 | 说明 |
| --- | --- |
| HTTP 与 HTTPS | HTTPS 使用 `WiFiClientSecure`（平台原生 TLS） |
| 双向流式传输 | 从任意 `Stream` 上传请求体；通过 `getStreamPtr()` 或 `onData` 回调下载响应体 |
| 分块传输编码 | 同时支持发送和接收 |
| Content-Length | 透明处理 |
| Keep-Alive | HTTP/1.1 默认开启；可通过 `useHTTP10(true)` 关闭 |
| 重定向 | `SHC_REDIRECT_OFF` / `SHC_REDIRECT_FOLLOW_GET_ONLY`（默认）/ `SHC_REDIRECT_FOLLOW_ALL`；可配置跳转上限 |
| 基本认证 | `setBasicAuth(user, pass)`；通过 `setAuthorization("Bearer ...")` 设置原始 Authorization 头 |
| 自定义请求头 | `addHeader(name, value, first, replace)` |
| gzip / deflate 解压 | 由 [miniz](https://github.com/richgel999/miniz) 提供支持（已内置，MIT 许可证） |
| Server-Sent Events (SSE) | 通过 `connectSSE()` / `onEvent()` 集成在主类中 |
| 同步 API | `GET`、`POST`、`PUT`、`PATCH`、`DELETE`、`HEAD`、`OPTIONS`、`sendRequest`、`sendRequestStream` |
| 异步 API | ESP32 / RP2040 上使用 FreeRTOS 任务；其他内核上使用协作式轮询回退方案 |
| TCP 保活 | `setTCPKeepAlive(idle, intv, count)`（仅 ESP32） |

---

## 支持的平台

| 平台 | 状态 | 说明 |
| --- | --- | --- |
| ESP32（Arduino-ESP32） | 完整支持 | 基于 FreeRTOS 任务的异步；TCP 保活 |
| ESP8266（Arduino-ESP8266） | 完整支持 | 协作式轮询异步（无 FreeRTOS 任务） |
| Raspberry Pi Pico W（arduino-pico） | 完整支持 | 协作式轮询 |
| SAMD / SAM（WiFi101） | 完整支持 | 协作式轮询 |
| 通用 Arduino WiFi（UNO WiFi Rev2、Nano 33 IoT 等） | 完整支持 | 协作式轮询 |

本库通过 `#ifdef` 自动检测平台。如果 `WiFiClientSecure` 不可用，HTTPS 请求会从 `begin("https://...")` 返回 `false`。

---

## 安装方式

### 方式 A：Arduino 库管理器（发布后可用）

Sketch → Include Library → Manage Libraries... → 搜索 "StreamHTTPClient" → Install。

### 方式 B：手动安装

1. 从 [releases 页面](https://github.com/your-org/StreamHTTPClient/releases) 下载最新的 `StreamHTTPClient-x.y.z.zip`。
2. Sketch → Include Library → Add .ZIP Library... → 选择该 zip 文件。
3. 重启 Arduino IDE。

### 方式 C：PlatformIO

```ini
lib_deps =
    StreamHTTPClient=symlink://file:///path/to/StreamHTTPClient
```

或者将 `StreamHTTPClient/` 文件夹复制到你项目的 `lib/` 目录下。

---

## 快速上手

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

## 示例

本库在 `examples/` 目录下附带 16 个示例：

| 示例 | 演示内容 |
| --- | --- |
| `BasicGET` | 最简单的 GET 请求 |
| `BasicPOST` | 带 JSON 请求体的 POST |
| `HTTPSRequest` | HTTPS / TLS，附带不安全模式与 CA 证书说明 |
| `StreamingDownload` | 大文件下载且不进行缓冲 |
| `StreamingUpload` | 从 `File`（SPIFFS）流式上传，含 Content-Length 与分块两种方式 |
| `ChunkedResponse` | 读取分块 HTTP 响应 |
| `GzipDecompression` | 透明的 gzip 解压 |
| `AsyncRequest` | 带回调的异步 GET |
| `AsyncStreaming` | 边进行异步流式下载边闪烁 LED |
| `SSEClient` | Server-Sent Events 订阅 |
| `SSEChatGPT` | 通过 SSE 接入 OpenAI 流式聊天补全 |
| `BasicAuth` | HTTP 基本认证 |
| `CustomHeaders` | 发送自定义请求头、收集响应头 |
| `RedirectFollow` | HTTP 重定向处理策略 |
| `KeepAlive` | HTTP/1.1 连接复用对比 HTTP/1.0 |
| `BidirectionalStream` | 同时进行流式上传与下载 |
| `PutDelete` | PUT 与 DELETE 方法 |
| `FullFeatured` | 综合所有功能的"大杂烩"示例 |

---

## API 参考

### 生命周期

| 方法 | 描述 |
| --- | --- |
| `bool begin(const String& url)` | 解析 `http(s)://host[:port]/path` 并初始化客户端。 |
| `bool begin(const String& host, uint16_t port, const String& uri = "/")` | 直接连接（HTTP）。 |
| `bool begin(WiFiClient* client, const String& url)` | 使用外部管理的传输层。 |
| `void end()` | 关闭连接并释放资源。 |

### 认证与请求头

| 方法 | 描述 |
| --- | --- |
| `setBasicAuth(user, pass)` | HTTP 基本认证。 |
| `setAuthorization(auth)` | 原始 `Authorization` 头，例如 `"Bearer xyz"`。 |
| `addHeader(name, value, first=false, replace=true)` | 添加/替换/前置一个自定义请求头。 |
| `collectHeaders(keys, count)` | 声明需要捕获哪些响应头。 |
| `header(name)` / `hasHeader(name)` / `headers()` | 查询已捕获的响应头。 |
| `headerName(i)` / `headerValue(i)` | 按索引遍历已捕获的响应头。 |

### 配置

| 方法 | 描述 |
| --- | --- |
| `setUserAgent(ua)` | 默认 `"StreamHTTPClient/1.0"`。 |
| `setConnectTimeout(ms)` | 默认 5000。 |
| `setTcpTimeout(ms)` | 默认 5000。 |
| `setFollowRedirects(policy)` | `SHC_REDIRECT_OFF`、`SHC_REDIRECT_FOLLOW_GET_ONLY`、`SHC_REDIRECT_FOLLOW_ALL`。 |
| `setRedirectLimit(n)` | 默认 10。 |
| `setTCPKeepAlive(idleSec, intvSec, count)` | 仅 ESP32。 |
| `useHTTP10(true)` | 使用 HTTP/1.0（无 keep-alive）。 |
| `useStreamMode(true)` | 不将响应体缓存在 RAM 中。 |
| `setDecompress(true)` | 启用 gzip/deflate 解压（默认开启）。 |
| `acceptEncoding(enc)` | 覆盖 `Accept-Encoding` 头。 |

### 同步请求

| 方法 | 描述 |
| --- | --- |
| `GET()` | 发起一次 `GET`。 |
| `POST(body)` / `POST(buf, size)` / `POST(stream, size=-1)` | 发起带请求体的 `POST`。 |
| `PUT`、`PATCH`、`DELETE`、`HEAD`、`OPTIONS` | 同样支持上述请求体变体。 |
| `sendRequest(method, body)` | 通用入口。 |
| `sendRequestStream(method, upload, size=-1, chunked=false)` | 双向流式传输。`size=-1` 表示使用分块传输。 |

### 响应访问

| 方法 | 描述 |
| --- | --- |
| `getStatusCode()` | HTTP 状态码。 |
| `getString()` | 以 String 形式返回响应体（缓冲模式）。 |
| `getSize()` | `Content-Length`，或已接收字节数。 |
| `getStreamPtr()` | 直接访问底层的 `WiFiClient*`。 |
| `getStream()` | 同上，但返回 `Stream*`。 |
| `connected()` | 传输层是否仍然连接。 |
| `available()` | 响应体中可读取的字节数。 |
| `readBytes(buf, len)` | 从响应体读取原始字节。 |

### 异步 API

| 方法 | 描述 |
| --- | --- |
| `asyncGET()` / `asyncPOST(...)` / `asyncPUT(...)` / `asyncDELETE(...)` | 启动一个异步请求。 |
| `asyncSendRequest(method, body)` / `asyncSendRequestStream(method, upload, size, chunked)` | 通用异步入口。 |
| `asyncPoll()` | 推动状态机前进（仅协作模式）。 |
| `asyncStop()` | 取消当前请求。 |
| `isAsyncRunning()` | 请求进行中返回 true。 |
| `asyncState()` | 返回 `SHC_STATE_IDLE`、`SHC_STATE_CONNECTING`、……、`SHC_STATE_DONE`、`SHC_STATE_ERROR` 之一。 |
| `asyncStatusCode()` / `asyncLastError()` | 最终状态码 / 错误码。 |
| `asyncWaitComplete(timeoutMs=0)` | 阻塞直到完成。 |

### 回调

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
// 在 loop() 中：
http.ssePoll();
```

本库会自动完成以下工作：
- 发送 `Accept: text/event-stream`
- 在连接断开时自动重连
- 在重连时发送 `Last-Event-ID`
- 遵循服务器的 `retry:` 字段

---

## 架构

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

- **miniz** 静态链接进本库。仅编译 inflate 路径（`MINIZ_NO_ARCHIVE_APIS`、`MINIZ_NO_STDIO`、`MINIZ_NO_TIME`），从而将 flash 占用控制在较小范围（ESP32 上约 30 KB）。
- **异步实现** 在 ESP32 上通过 `xTaskCreateUniversal` 创建任务，栈大小为 `SHC_ASYNC_TASK_STACK` 字节（默认 8 KB，可配置）。在没有 FreeRTOS 的平台上，`asyncPoll()` 会从调用者的 `loop()` 中以协作方式推动同一套状态机。
- **SSE 实现** 会以 `Accept: text/event-stream` 发起一次普通 `GET`，然后将响应字节喂给一个轻量的行解析器。该解析器遵循 RFC 8895：处理 `event:`、`data:`、`id:`、`retry:` 字段以及 `:` 注释行，遇到空行时分发事件，并用 `\n` 拼接多行 `data:`。

---

## 配置宏

以下宏均可在包含 `StreamHTTPClient.h` 之前通过 `#define` 覆盖：

| 宏 | 默认值 | 描述 |
| --- | --- | --- |
| `SHC_MAX_HEADERS` | 16 | 自定义请求头最大数量 |
| `SHC_MAX_COLLECT_HEADERS` | 16 | 最多捕获的响应头数量 |
| `SHC_MAX_HEADER_LINE` | 256 | 单条响应头行的最大长度 |
| `SHC_MAX_REDIRECTS` | 10 | 默认重定向上限 |
| `SHC_DEFAULT_STRING_BUFFER` | 4096 | 初始响应体缓冲（按需增长） |
| `SHC_DEFAULT_CONNECT_TIMEOUT` | 5000 | TCP 连接超时（ms） |
| `SHC_DEFAULT_TCP_TIMEOUT` | 5000 | TCP 读取超时（ms） |
| `SHC_DEFAULT_USER_AGENT` | `"StreamHTTPClient/1.0"` | 默认 User-Agent |
| `SHC_CHUNK_LINE_BUFFER` | 32 | 分块大小行缓冲 |
| `SHC_SSE_LINE_BUFFER` | 1024 | SSE 行解析器缓冲 |
| `SHC_ASYNC_TASK_STACK` | 8192 | 异步 FreeRTOS 任务栈 |
| `SHC_ASYNC_TASK_PRIORITY` | 1 | FreeRTOS 任务优先级 |
| `SHC_ASYNC_TASK_CORE` | -1 | FreeRTOS 任务内核（-1 = 任意） |
| `SHC_GZIP_OUT_BUF_SIZE` | 16384 | 解压器输出缓冲 |

---

## 错误码

负返回值表示错误，正值表示 HTTP 状态码。

| 代码 | 常量 | 含义 |
| --- | --- | --- |
| -1  | `SHC_ERROR_CONNECTION_REFUSED` | TCP/TLS 连接失败 |
| -2  | `SHC_ERROR_SEND_HEADER_FAILED` | 无法写入请求行/请求头 |
| -3  | `SHC_ERROR_SEND_PAYLOAD_FAILED` | 无法写入请求体 |
| -4  | `SHC_ERROR_NO_HTTP_SERVER` | 服务器未以 HTTP/1.x 响应 |
| -5  | `SHC_ERROR_CONNECTION_LOST` | 请求过程中 TCP 连接断开 |
| -6  | `SHC_ERROR_NO_DATA` | 超时未收到响应数据 |
| -7  | `SHC_ERROR_READ_RESPONSE` | 响应头中存在异常数据 |
| -8  | `SHC_ERROR_TOO_LESS_RAM` | 内存分配失败 |
| -9  | `SHC_ERROR_STREAM_WRITE` | 调用方提供的上传流出错 |
| -10 | `SHC_ERROR_CHUNK_SIZE` | 分块大小行格式错误 |
| -11 | `SHC_ERROR_DECOMPRESSION` | gzip/deflate 解码错误 |
| -13 | `SHC_ERROR_CACERT` | TLS 证书被拒绝 |
| -15 | `SHC_ERROR_REDIRECT_EXCEEDED` | 重定向次数过多 |
| -16 | `SHC_ERROR_INVALID_URL` | URL 无法解析 |
| -17 | `SHC_ERROR_TASK_CREATE` | 无法创建 FreeRTOS 任务 |
| -18 | `SHC_ERROR_ALREADY_RUNNING` | 已有异步请求正在进行 |
| -19 | `SHC_ERROR_USER_CANCEL` | 通过 `asyncStop()` 取消 |
| -20 | `SHC_ERROR_UNSUPPORTED_SCHEME` | 仅支持 `http` 与 `https` |
| -99 | `SHC_ERROR_UNKNOWN` | 未知错误 |

使用 `StreamHTTPClient::errorToString(code)` 可获取可读的错误字符串。

---

## 线程安全说明（ESP32）

异步路径在独立的 FreeRTOS 任务中执行请求，回调也会从该任务中触发。在回调内部：

- ✅ 安全：`Serial.print`、写入 `WiFiClient`、分配内存、设置标志位、写入 `Stream`/`File`。
- ❌ 不安全：回调中再次调用同一个 `StreamHTTPClient` 实例（例如在 `onComplete()` 中调用 `http.end()` 可能导致死锁）。建议改用标志位，并在 `loop()` 中调用 `end()`。

如果回调需要执行耗时操作，建议先将字节拷贝出来，再在 `loop()` 中处理。

---

## 许可证

- StreamHTTPClient 源代码：**MIT**（见 `LICENSE`）
- 内置的 [miniz](https://github.com/richgel999/miniz)：**MIT / Public Domain**（见 `src/miniz.c` 头部）

---

## 致谢

- Espressif 的 [`HTTPClient`](https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient)，本库与之兼容的原始 API 设计来源于此。
- Rich Geldreich 及各位贡献者开发的 [miniz](https://github.com/richgel999/miniz)。
- Arduino 社区提供的 `WiFiClient` / `WiFiClientSecure` 接口。

---
*AI生成*
