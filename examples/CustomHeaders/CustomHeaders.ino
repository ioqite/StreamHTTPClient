/*
  CustomHeaders - send custom request headers and collect response headers.

  Demonstrates:
    - addHeader() - add / replace / prepend
    - collectHeaders() - declare which response headers to keep
    - header(name) / hasHeader(name) / headers() / headerName(i) / headerValue(i)
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

    // /headers echoes back the request headers as JSON.
    http.begin("http://httpbin.org/headers");

    // Custom request headers
    http.addHeader("X-My-Header", "hello");          // append
    http.addHeader("X-Trace-Id", "abc-123");          // append
    http.addHeader("User-Agent", "MySketch/2.0");     // replace built-in
    http.addHeader("X-First", "1", /*first=*/true);   // prepend

    // Response headers we want to be able to query by name later.
    const char* collect[] = {
        "Content-Type",
        "Content-Length",
        "Server",
        "Date",
        "Connection",
    };
    http.collectHeaders(collect, sizeof(collect) / sizeof(collect[0]));

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);

    if (code > 0) {
        Serial.println("[HTTP] Collected response headers:");
        for (int i = 0; i < http.headers(); i++) {
            Serial.printf("  %s: %s\n",
                          http.headerName(i).c_str(),
                          http.headerValue(i).c_str());
        }
        Serial.println();

        Serial.println("[HTTP] Body:");
        Serial.println(http.getString());
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
