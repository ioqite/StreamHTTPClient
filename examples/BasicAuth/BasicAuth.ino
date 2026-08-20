/*
  BasicAuth - HTTP Basic Authentication.

  Demonstrates:
    - setBasicAuth(user, password)
    - the library base64-encodes the credentials and sends them on every
      request automatically
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

    // httpbin.org/basic-auth/user/passwd requires Basic Auth.
    http.begin("http://httpbin.org/basic-auth/user/passwd");
    http.setBasicAuth("user", "passwd");

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);
    if (code == 200) {
        Serial.println("[HTTP] Auth OK, body:");
        Serial.println(http.getString());
    } else if (code == 401) {
        Serial.println("[HTTP] 401 Unauthorized - wrong credentials");
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
