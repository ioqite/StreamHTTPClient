/*
  HTTPSRequest - HTTPS / TLS GET request.

  Demonstrates:
    - begin("https://...")
    - using WiFiClientSecure in insecure mode (no certificate validation)
    - For production: load a root CA with setCACert() before begin()

  NOTE: Skipping certificate validation is fine for testing but is insecure.
        For real deployments, see the comments at the bottom of this file.
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

    const char* url = "https://www.howsmyssl.com/a/check";

    // begin() will auto-select WiFiClientSecure because the URL is https://.
    if (!http.begin(url)) {
        Serial.println("[HTTP] begin() failed");
        return;
    }

    // Grab the secure client and skip certificate validation.
    // WARNING: insecure! Only for testing. See end of file for secure setup.
    WiFiClientSecure* sec = http.getSecureClient();
    if (sec) {
        sec->setInsecure();
    }

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);
    if (code > 0) {
        Serial.println(http.getString());
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}

/*
  For production-grade HTTPS:

  1. Download the root CA certificate for the server you are connecting to
     (PEM format) and store it as a const char* (or in PROGMEM / SPIFFS).

  2. Replace the setInsecure() call with setCACert():

        const char* ROOT_CA =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
            "... (full PEM) ...\n"
            "-----END CERTIFICATE-----\n";

        WiFiClientSecure* sec = http.getSecureClient();
        sec->setCACert(ROOT_CA);

  3. (ESP32 only) For mutual TLS you can also call setCertificate() and
     setPrivateKey() with the client cert/key PEM strings.
*/
