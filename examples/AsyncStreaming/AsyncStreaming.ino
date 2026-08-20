/*
  AsyncStreaming - async streaming download.

  Demonstrates:
    - asyncGET() with a data callback that fires repeatedly
    - processing the body in chunks without buffering it all
    - keeping the loop() free for other work (here, blinking the LED)

  This is the recommended pattern for downloading large files on ESP32
  while keeping the main task responsive.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

size_t   g_received = 0;
uint32_t g_start    = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(LED_BUILTIN, OUTPUT);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    // A ~600 KB file.
    http.begin("http://speedtest.ftp.otenet.gr/files/test100k.db");
    http.useStreamMode(true);
    http.setDecompress(false);

    http.onData([](uint8_t* data, size_t len) {
        g_received += len;
        // ---- Process the chunk here ----
        // For a real file download, write `data` (length `len`) to SPIFFS/SD.
    });

    http.onProgress([](const StreamHTTPClient_Progress& p) {
        Serial.printf("[progress] received=%u / %u, elapsed=%ums\n",
                      (unsigned)p.bytes_received, (unsigned)p.total_received,
                      p.elapsed_ms);
    });

    http.onComplete([](int code) {
        uint32_t elapsed = millis() - g_start;
        Serial.printf("[done] HTTP %d, %u bytes in %ums (%.1f KB/s)\n",
                      code, (unsigned)g_received, elapsed,
                      (float)g_received / 1024.0f / (elapsed / 1000.0f));
    });

    http.onError([](int err) {
        Serial.printf("[err] %s\n",
                      StreamHTTPClient::errorToString(err).c_str());
    });

    g_start = millis();
    http.asyncGET();
}

void loop() {
    // Free-running loop. The async task on ESP32 drives the request.
    // We can do other work here, like blinking an LED.
    static uint32_t last_blink = 0;
    if (millis() - last_blink > 250) {
        last_blink = millis();
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }

    if (!http.isAsyncRunning() && g_received > 0) {
        // Request finished; we can stop the sketch's work loop here.
        http.end();
        g_received = 0;
        Serial.println("[loop] Done.");
    }
}
