/*
  SSEChatGPT - consume an OpenAI streaming chat completion.

  OpenAI's streaming endpoint returns Server-Sent Events but requires a POST
  request with a JSON body. The built-in connectSSE() helper issues a GET,
  so for this case we use the async API + onData callback and parse the SSE
  lines ourselves with a tiny inline parser.

  Demonstrates:
    - HTTPS POST with Bearer token
    - Streaming response (chunked transfer) parsed as SSE
    - Detecting the [DONE] sentinel
    - Extracting the delta content from each JSON chunk

  NOTE: To run this, set OPENAI_API_KEY to a valid key. The example will not
  do anything useful with an empty key.

  The same pattern works for other LLM APIs that expose SSE streaming
  (Anthropic, Gemini, vLLM, Ollama, ...): just swap the URL and the JSON
  body.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

const char* OPENAI_API_KEY = "sk-...";

StreamHTTPClient http;

// ---------------------------------------------------------------------------
// Minimal SSE line parser - fed by the onData callback below.
// ---------------------------------------------------------------------------
struct SSELineParser {
    String buf;
    void (*onEvent)(const String& data);

    void feed(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            char c = (char)data[i];
            if (c == '\n') {
                handleLine(buf);
                buf = "";
            } else if (c != '\r') {
                buf += c;
                if (buf.length() > 4096) {
                    // Line too long - reset
                    buf = "";
                }
            }
        }
    }

    void handleLine(const String& line) {
        if (line.length() == 0) return;  // event boundary
        if (line.charAt(0) == ':') return; // comment
        if (!line.startsWith("data:")) return;
        String data = line.substring(5);
        if (data.length() > 0 && data.charAt(0) == ' ') {
            data.remove(0, 1);
        }
        if (onEvent) onEvent(data);
    }
};

SSELineParser sseParser;

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\n[WiFi] Connected");

    http.begin("https://api.openai.com/v1/chat/completions");
    WiFiClientSecure* sec = http.getSecureClient();
    if (sec) sec->setInsecure();

    http.setAuthorization(String("Bearer ") + OPENAI_API_KEY);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "text/event-stream");

    // Stream the response (don't buffer it all in RAM).
    http.useStreamMode(true);
    // We are parsing SSE text ourselves - we want the raw bytes, not a
    // decompressed body. Also avoid sending Accept-Encoding so the server
    // does not gzip the stream (which would force us to allocate a
    // decompression buffer).
    http.setDecompress(false);
    http.acceptEncoding("identity");

    const char* body =
        "{\"model\":\"gpt-3.5-turbo\","
        "\"stream\":true,"
        "\"messages\":[{\"role\":\"user\","
        "\"content\":\"Count from 1 to 5.\"}]}";

    // Hook the SSE parser up to the data callback.
    sseParser.onEvent = [](const String& data) {
        if (data == "[DONE]") {
            Serial.println("\n[OpenAI] stream complete");
            return;
        }
        // The data is a JSON object like:
        //   {"choices":[{"delta":{"content":"1"}, ...}]}
        // For brevity we extract the content substring manually. For a real
        // sketch, use ArduinoJson to parse the chunk properly.
        int p = data.indexOf("\"content\":\"");
        if (p >= 0) {
            p += 11;  // length of "content":"
            int end = p;
            while (end < (int)data.length() && data.charAt(end) != '"') {
                if (data.charAt(end) == '\\') end++;  // skip escape
                end++;
            }
            String piece = data.substring(p, end);
            // Unescape \n -> newline (very small subset of JSON unescaping)
            piece.replace("\\n", "\n");
            piece.replace("\\\"", "\"");
            Serial.print(piece);
        }
    };

    http.onData([](uint8_t* data, size_t len) {
        sseParser.feed(data, len);
    });

    http.onComplete([](int code) {
        Serial.printf("\n[OpenAI] HTTP %d\n", code);
    });

    http.onError([](int err) {
        Serial.printf("[OpenAI] error: %s\n",
                      StreamHTTPClient::errorToString(err).c_str());
    });

    Serial.println("[OpenAI] POST streaming ...");
    http.asyncPOST(String(body));

    while (http.isAsyncRunning()) {
        http.asyncPoll();
        delay(1);
    }
    http.end();
}

void loop() {}
