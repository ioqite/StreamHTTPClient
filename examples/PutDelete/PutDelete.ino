/*
  PutDelete - PUT and DELETE methods.

  Demonstrates:
    - PUT(body)            - replace a resource
    - DELETE(body="")      - delete a resource (with optional body)
    - reading the JSON echo from httpbin.org
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

    // ---- PUT ----
    http.begin("http://httpbin.org/put");
    http.addHeader("Content-Type", "application/json");
    int put_code = http.PUT(String("{\"id\":1,\"name\":\"Widget\"}"));
    Serial.printf("[HTTP] PUT -> %d\n", put_code);
    if (put_code > 0) {
        Serial.println(http.getString().substring(0, 300));
    }
    http.end();

    // ---- DELETE ----
    http.begin("http://httpbin.org/delete");
    int del_code = http.DELETE();
    Serial.printf("\n[HTTP] DELETE -> %d\n", del_code);
    if (del_code > 0) {
        Serial.println(http.getString().substring(0, 300));
    }
    http.end();

    // ---- DELETE with body ----
    http.begin("http://httpbin.org/delete");
    http.addHeader("Content-Type", "application/json");
    int del_body_code = http.DELETE(String("{\"reason\":\"obsolete\"}"));
    Serial.printf("\n[HTTP] DELETE (with body) -> %d\n", del_body_code);
    if (del_body_code > 0) {
        Serial.println(http.getString().substring(0, 300));
    }
    http.end();
}

void loop() {}
