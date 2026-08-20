/*
  FullFeatured - kitchen-sink example combining every StreamHTTPClient
  feature in a single sketch.

  Demonstrates:
    - HTTPS with a custom root CA (replace ROOT_CA with a real one!)
    - Bearer-token authentication
    - Custom request headers
    - gzip decompression
    - chunked upload via a Stream
    - collecting response headers
    - redirect following with a custom limit
    - TCP keep-alive on the underlying socket
    - progress callback
    - complete / error callbacks

  Pick the parts you need for your project; you do not have to use them all.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

const char* API_URL = "https://httpbin.org/anything";
const char* TOKEN   = "demo-token";

StreamHTTPClient http;

class CounterStream : public Stream {
public:
    CounterStream(size_t total) : _total(total), _pos(0) {}
    int available() override { return (int)(_total - _pos); }
    int read() override {
        if (_pos >= _total) return -1;
        return (int)(_pos++ & 0xFF);
    }
    int peek() override { return (_pos < _total) ? (int)(_pos & 0xFF) : -1; }
    size_t write(uint8_t) override { return 0; }
private:
    size_t _total, _pos;
};

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected. IP: " + WiFi.localIP().toString());

    // ---- Configure the client ----
    http.begin(API_URL);

    // TLS: skip validation for the demo. Replace with a real CA!
    WiFiClientSecure* sec = http.getSecureClient();
    if (sec) sec->setInsecure();

    // Auth + headers
    http.setAuthorization(String("Bearer ") + TOKEN);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("X-Request-Id", "demo-001");

    // Behaviour
    http.setConnectTimeout(8000);
    http.setTcpTimeout(8000);
    http.setFollowRedirects(SHC_REDIRECT_FOLLOW_GET_ONLY);
    http.setRedirectLimit(5);
    http.useHTTP10(false);                  // HTTP/1.1 keep-alive
    http.useStreamMode(false);              // buffer body so getString() works
    http.setDecompress(true);               // auto-decompress gzip/deflate
    http.setTCPKeepAlive(15, 5, 3);         // ESP32 only

    // Collect interesting response headers
    const char* collect[] = {
        "Content-Type", "Content-Length", "Content-Encoding",
        "Server", "Connection", "Location",
    };
    http.collectHeaders(collect, sizeof(collect) / sizeof(collect[0]));

    // Callbacks
    http.onHeader([](const String& name, const String& value) {
        Serial.printf("  [hdr] %s: %s\n", name.c_str(), value.c_str());
    });
    http.onProgress([](const StreamHTTPClient_Progress& p) {
        Serial.printf("  [prog] up=%u/%u  down=%u/%u  t=%ums\n",
                      (unsigned)p.bytes_sent, (unsigned)p.total_sent,
                      (unsigned)p.bytes_received, (unsigned)p.total_received,
                      p.elapsed_ms);
    });
    http.onComplete([](int code) {
        Serial.printf("  [ok ] HTTP %d\n", code);
    });
    http.onError([](int err) {
        Serial.printf("  [err] %s\n",
                      StreamHTTPClient::errorToString(err).c_str());
    });

    // ---- Issue the request: streaming chunked upload ----
    CounterStream upload(20 * 1024); // 20 KB

    Serial.println("\n[HTTP] POST chunked stream ...");
    int code = http.sendRequestStream("POST", &upload, (size_t)-1, /*chunked=*/true);
    Serial.printf("\n[HTTP] Final code: %d\n", code);

    if (code > 0) {
        Serial.println("\n[HTTP] Collected headers:");
        for (int i = 0; i < http.headers(); i++) {
            Serial.printf("  %s: %s\n",
                          http.headerName(i).c_str(),
                          http.headerValue(i).c_str());
        }
        String body = http.getString();
        Serial.printf("\n[HTTP] Body (%u bytes, first 300):\n",
                      (unsigned)body.length());
        Serial.println(body.substring(0, 300));
    }

    http.end();
    Serial.println("\n[done]");
}

void loop() {}
