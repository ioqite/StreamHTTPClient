/*
  BidirectionalStream - stream a request body *up* while simultaneously
  streaming the response body *down*. This is the pattern you need for:
    - OpenAI/Anthropic streaming chat completions (upload prompt, stream tokens)
    - Chunked file uploads where the server returns a streaming acknowledgement
    - WebDAV / CalDAV with chunked responses

  Demonstrates:
    - sendRequestStream("POST", upload_stream, size, chunked)
    - onData callback fires while the upload is still in progress
    - useStreamMode(true) prevents the library from buffering the whole
      response in RAM
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

// A small in-RAM Stream that produces a known sequence of bytes.
class CounterStream : public Stream {
public:
    CounterStream(size_t total) : _total(total), _pos(0) {}

    int available() override {
        return (int)(_total - _pos);
    }
    int read() override {
        if (_pos >= _total) return -1;
        return (int)(_pos++ & 0xFF);
    }
    int peek() override {
        if (_pos >= _total) return -1;
        return (int)(_pos & 0xFF);
    }
    size_t write(uint8_t) override { return 0; }

private:
    size_t _total;
    size_t _pos;
};

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    // /anything echoes the request body back as JSON.
    http.begin("http://httpbin.org/anything");
    http.useStreamMode(true);
    http.setDecompress(false);
    http.addHeader("Content-Type", "application/octet-stream");

    // 50 KB upload from a Stream. We let the library send it with
    // Transfer-Encoding: chunked so we don't have to know the size up front.
    CounterStream upload(50 * 1024);

    size_t received = 0;
    http.onData([&](uint8_t* data, size_t len) {
        received += len;
        if (received % 1024 == 0) {
            Serial.printf("[down] %u bytes\n", (unsigned)received);
        }
    });

    http.onComplete([](int code) {
        Serial.printf("[done] HTTP %d\n", code);
    });

    uint32_t start = millis();
    int code = http.sendRequestStream("POST", &upload, (size_t)-1, /*chunked=*/true);
    uint32_t elapsed = millis() - start;

    Serial.printf("[HTTP] POST stream -> %d  in %ums, received %u bytes\n",
                  code, elapsed, (unsigned)received);
    http.end();
}

void loop() {}
