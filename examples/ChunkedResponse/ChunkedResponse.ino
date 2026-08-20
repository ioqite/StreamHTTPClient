/*
  ChunkedResponse - read a server response that uses HTTP/1.1 chunked
  transfer-encoding.

  Demonstrates:
    - StreamHTTPClient transparently de-chunks the response
    - reading the de-chunked bytes via getStreamPtr() / readBytes
    - the "Transfer-Encoding: chunked" response header

  Try the chunked URL on httpbin.org which emits a chunked JSON response.
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

    // /stream/10 returns 10 JSON objects, each on its own line, sent as
    // separate chunks. https://httpbin.org/#/Dynamic_methods/get_stream
    http.begin("http://httpbin.org/stream/10");
    http.useStreamMode(true);
    http.collectHeaders(nullptr, 0);  // do not collect anything

    // We *do* want to inspect Transfer-Encoding manually:
    const char* keys[] = {"Transfer-Encoding"};
    http.collectHeaders(keys, 1);

    int code = http.GET();
    Serial.printf("[HTTP] GET -> %d\n", code);

    if (code == 200) {
        String te = http.header("Transfer-Encoding");
        Serial.printf("[HTTP] Transfer-Encoding: '%s'\n",
                      te.length() ? te.c_str() : "(none / Content-Length used)");

        // Read the body line by line. The library has already de-chunked it,
        // so we just read raw bytes from the socket as if it were a normal
        // response.
        WiFiClient* stream = http.getStreamPtr();
        String line;
        uint32_t start = millis();
        while (http.connected() || stream->available()) {
            if (stream->available()) {
                int b = stream->read();
                if (b < 0) break;
                if (b == '\n') {
                    if (line.length() > 0) {
                        Serial.printf("[JSON] %s\n", line.c_str());
                    }
                    line = "";
                } else if (b != '\r') {
                    line += (char)b;
                }
            } else {
                if (millis() - start > 15000) {
                    Serial.println("[HTTP] Timeout");
                    break;
                }
                delay(1);
            }
        }
    } else {
        Serial.println("Error: " + StreamHTTPClient::errorToString(code));
    }
    http.end();
}

void loop() {}
