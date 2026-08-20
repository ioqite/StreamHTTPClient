/*
  AsyncRequest - non-blocking GET request using the async API.

  Demonstrates:
    - onData / onComplete / onError / onHeader callbacks
    - asyncGET() + asyncPoll() (cooperative mode)
    - On ESP32 the request runs in its own FreeRTOS task; on other platforms
      it runs inline but the callback-style API is identical.

  Note: in this example the callbacks fire from the same context as the
  caller's loop(). They are safe to use for Serial.print() etc.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    http.begin("http://httpbin.org/get");

    http.onHeader([](const String& name, const String& value) {
        Serial.printf("[hdr] %s: %s\n", name.c_str(), value.c_str());
    });

    http.onData([](uint8_t* data, size_t len) {
        Serial.printf("[data] %u bytes\n", (unsigned)len);
        // Print first chunk only to keep the log readable
        Serial.write(data, len < 200 ? len : 200);
        Serial.println();
    });

    http.onComplete([](int code) {
        Serial.printf("[done] HTTP %d\n", code);
    });

    http.onError([](int err) {
        Serial.printf("[err ] %s\n",
                      StreamHTTPClient::errorToString(err).c_str());
    });

    Serial.println("[HTTP] asyncGET() ...");
    if (!http.asyncGET()) {
        Serial.println("[HTTP] could not start async request");
        return;
    }

    // On ESP32 the request runs in a FreeRTOS task and we just need to wait.
    // On other platforms asyncPoll() drives the state machine.
    while (http.isAsyncRunning()) {
        http.asyncPoll();
        delay(1);
    }

    Serial.printf("[HTTP] Final state: %d, status: %d\n",
                  (int)http.asyncState(), http.asyncStatusCode());
    http.end();
}

void loop() {}
