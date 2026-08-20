/*
  BasicGET - simplest possible synchronous GET request.

  Demonstrates:
    - WiFi.begin() for station mode
    - StreamHTTPClient::begin(url)
    - GET()
    - getStatusCode() / getString()
    - end()
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

const char* URL = "http://httpbin.org/get";

StreamHTTPClient http;

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" OK");
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.localIP());

    Serial.printf("[HTTP] GET %s\n", URL);
    if (http.begin(URL)) {
        int code = http.GET();
        Serial.printf("[HTTP] Response code: %d\n", code);
        if (code > 0) {
            Serial.println("[HTTP] Body:");
            Serial.println(http.getString());
        } else {
            Serial.printf("[HTTP] Error: %s\n",
                          StreamHTTPClient::errorToString(code).c_str());
        }
        http.end();
    } else {
        Serial.println("[HTTP] begin() failed - bad URL?");
    }
}

void loop() {
    // Nothing to do here.
}
