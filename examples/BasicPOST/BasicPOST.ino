/*
  BasicPOST - synchronous POST with a JSON body.

  Demonstrates:
    - addHeader("Content-Type", ...)
    - POST(String body)
    - reading the response body and the echo from httpbin.org
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
    Serial.println("\n[WiFi] Connected. IP: " + WiFi.localIP().toString());

    const char* url = "http://httpbin.org/post";
    const char* body = "{\"hello\":\"world\",\"n\":42,\"arr\":[1,2,3]}";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(String(body));
    Serial.printf("[HTTP] POST -> %d\n", code);
    if (code > 0) {
        String resp = http.getString();
        Serial.println("[HTTP] Response:");
        Serial.println(resp);
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
