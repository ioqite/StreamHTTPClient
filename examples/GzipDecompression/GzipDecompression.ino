/*
  GzipDecompression - request a gzip-encoded response and let
  StreamHTTPClient decompress it transparently.

  Demonstrates:
    - default Accept-Encoding: "gzip, deflate"
    - setDecompress(true) (default)
    - the library swaps the gzipped bytes for the decompressed bytes,
      so getString() returns the plain-text body

  Compare the Content-Encoding header before/after to see what the server sent.
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

    // /gzip returns a JSON body with Content-Encoding: gzip
    http.begin("http://httpbin.org/gzip");

    const char* keys[] = {"Content-Encoding", "Content-Length",
                          "Transfer-Encoding"};
    http.collectHeaders(keys, 3);

    // Decompression is ON by default. We keep it on.
    http.setDecompress(true);

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);

    if (code == 200) {
        Serial.printf("[HTTP] Content-Encoding: %s\n",
                      http.header("Content-Encoding").c_str());
        Serial.printf("[HTTP] Content-Length (compressed): %s\n",
                      http.header("Content-Length").c_str());

        String body = http.getString();
        Serial.printf("[HTTP] Decompressed body length: %u bytes\n",
                      (unsigned)body.length());
        Serial.println("[HTTP] Body (first 500 chars):");
        Serial.println(body.substring(0, 500));
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
