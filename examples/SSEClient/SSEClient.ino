/*
  SSEClient - connect to a Server-Sent Events endpoint and print events.

  Demonstrates:
    - onEvent(callback) - fired for every event in the stream
    - onSSEConnect()    - fired once when the SSE connection is up
    - onSSEError()      - fired on connection errors or unexpected status
    - connectSSE() / ssePoll() / stopSSE()
    - automatic reconnection with Last-Event-ID

  The demo endpoint below emits one event every second.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

// Public SSE test endpoint that streams the current server time every second.
const char* SSE_URL = "https://httpbin.org/stream";

StreamHTTPClient http;

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    // For https:// we need to set up TLS. Use insecure mode for testing.
    http.begin(SSE_URL);
    WiFiClientSecure* sec = http.getSecureClient();
    if (sec) sec->setInsecure();

    http.onSSEConnect([]() {
        Serial.println("[SSE] connected");
    });

    http.onSSEError([](int err) {
        Serial.printf("[SSE] error: %s\n",
                      StreamHTTPClient::errorToString(err).c_str());
    });

    http.onEvent([](const SSEEvent& ev) {
        Serial.printf("[SSE] event=%s id=%s retry=%ums\n",
                      ev.event.c_str(),
                      ev.id.length() ? ev.id.c_str() : "-",
                      ev.retry);
        Serial.printf("[SSE] data: %s\n", ev.data.c_str());
    });

    if (!http.connectSSE()) {
        Serial.println("[SSE] connectSSE() failed");
    }
}

void loop() {
    // Drive the SSE parser. Call this as often as possible.
    http.ssePoll();

    // The library automatically reconnects with Last-Event-ID if the
    // connection drops, honouring the server's retry interval.
}
