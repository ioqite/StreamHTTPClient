/*
  StreamingUpload - stream a large request body from an Arduino Stream
  (e.g. a File from SPIFFS/LittleFS) without loading it into RAM.

  Demonstrates:
    - sendRequestStream("POST", &file, size)  with a known Content-Length
    - the chunked variant when size is unknown
*/

#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

void uploadWithContentLength(File& f) {
    http.begin("http://httpbin.org/post");
    http.addHeader("Content-Type", "application/octet-stream");

    size_t size = f.size();
    Serial.printf("[HTTP] Uploading %u bytes with Content-Length\n",
                  (unsigned)size);

    int code = http.sendRequestStream("POST", &f, size);
    Serial.printf("[HTTP] Response code: %d\n", code);
    if (code > 0) {
        Serial.println(http.getString());
    }
    http.end();
}

void uploadChunked(File& f) {
    http.begin("http://httpbin.org/post");
    http.addHeader("Content-Type", "application/octet-stream");

    // size = (size_t)-1  triggers chunked transfer encoding.
    Serial.println("[HTTP] Uploading with chunked transfer encoding");

    int code = http.sendRequestStream("POST", &f, (size_t)-1, true);
    Serial.printf("[HTTP] Response code: %d\n", code);
    if (code > 0) {
        Serial.println(http.getString());
    }
    http.end();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    if (!SPIFFS.begin(true)) {
        Serial.println("[FS] SPIFFS mount failed");
        return;
    }

    // Create a test file to upload (~10 KB).
    File f = SPIFFS.open("/upload.bin", "w");
    if (!f) {
        Serial.println("[FS] cannot create file");
        return;
    }
    for (int i = 0; i < 10240; i++) {
        f.write((uint8_t)i);
    }
    f.close();

    // Variant 1: known size (Content-Length)
    f = SPIFFS.open("/upload.bin", "r");
    uploadWithContentLength(f);
    f.close();

    // Variant 2: chunked (server discovers length as we go)
    f = SPIFFS.open("/upload.bin", "r");
    uploadChunked(f);
    f.close();

    SPIFFS.remove("/upload.bin");
}

void loop() {}
