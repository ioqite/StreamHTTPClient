/*
  StreamingDownload - download a large file without buffering the whole
  response in RAM.

  Demonstrates:
    - useStreamMode(true)  -> don't buffer the body in RAM
    - getStreamPtr()       -> read bytes directly from the socket
    - honoring Content-Length for progress reporting
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

// ~600 KB binary file - far too large to buffer in ESP RAM.
const char* URL = "http://speedtest.ftp.otenet.gr/files/test100k.db";

StreamHTTPClient http;

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    Serial.printf("[HTTP] Downloading %s\n", URL);

    http.begin(URL);
    http.useStreamMode(true);   // don't buffer the body
    http.setDecompress(false);  // we are downloading a binary file

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);
    if (code == 200) {
        WiFiClient* stream = http.getStreamPtr();
        size_t total = http.getSize();
        Serial.printf("[HTTP] Content-Length: %u bytes\n", (unsigned)total);

        uint8_t buf[512];
        size_t received = 0;
        uint32_t start = millis();
        uint32_t lastReport = 0;

        while (http.connected() && (total == 0 || received < total)) {
            size_t avail = stream->available();
            if (avail == 0) {
                if (millis() - start > 30000) {
                    Serial.println("[HTTP] Timeout");
                    break;
                }
                delay(1);
                continue;
            }
            size_t want = avail > sizeof(buf) ? sizeof(buf) : avail;
            int n = stream->readBytes(buf, want);
            if (n <= 0) break;
            received += n;

            // ---- Process the chunk here ----
            // For a file download, write to SPIFFS / SD / LittleFS:
            //   file.write(buf, n);

            // Progress report every 500 ms
            if (millis() - lastReport > 500) {
                lastReport = millis();
                if (total > 0) {
                    Serial.printf("[HTTP] %u / %u bytes (%d%%)\n",
                                  (unsigned)received, (unsigned)total,
                                  (int)(received * 100 / total));
                } else {
                    Serial.printf("[HTTP] %u bytes\n", (unsigned)received);
                }
            }
        }
        uint32_t elapsed = millis() - start;
        Serial.printf("[HTTP] Done: %u bytes in %ums (%.1f KB/s)\n",
                      (unsigned)received, elapsed,
                      (float)received / 1024.0f / (elapsed / 1000.0f));
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
