/*
  KeepAlive - reuse a TCP/TLS connection across multiple HTTP requests.

  Demonstrates:
    - HTTP/1.1 keep-alive is the default; the connection is reused
    - HTTP/1.0 disables keep-alive (connection is closed after each request)
    - setTCPKeepAlive() enables TCP keep-alive probes (ESP32 only) for
      long-lived idle sockets

  Run this and observe the time per request - the second request is much
  faster because the TCP handshake is skipped.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

void timeGet(const char* url, bool http10) {
    http.begin(url);
    http.useHTTP10(http10);
    if (!http10) {
        // ESP32 only: enable TCP keepalive on the underlying socket.
        http.setTCPKeepAlive(/*idle=*/10, /*intv=*/5, /*count=*/3);
    }

    uint32_t start = millis();
    int code = http.GET();
    uint32_t elapsed = millis() - start;

    Serial.printf("[HTTP] %s %s -> %d  in %ums\n",
                  http10 ? "HTTP/1.0" : "HTTP/1.1",
                  url, code, elapsed);
    if (code > 0) {
        // Drain the body so the connection can be reused.
        size_t n = http.getString().length();
        (void)n;
    }
    // NOTE: do NOT call http.end() here - we want to keep the connection.
}

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    const char* url = "http://httpbin.org/get";

    Serial.println("\n--- HTTP/1.1 keep-alive (connection reused) ---");
    timeGet(url, false);
    timeGet(url, false);
    timeGet(url, false);
    http.end();

    Serial.println("\n--- HTTP/1.0 (new connection every time) ---");
    timeGet(url, true);
    timeGet(url, true);
    timeGet(url, true);
    http.end();
}

void loop() {}
