/*
  RedirectFollow - HTTP redirect handling.

  Demonstrates:
    - setFollowRedirects(SHC_REDIRECT_FOLLOW_GET_ONLY)  (default)
    - setFollowRedirects(SHC_REDIRECT_OFF)
    - setRedirectLimit(n)
    - inspecting the Location header when redirects are disabled

  httpbin.org/redirect/3 issues 3 chained 302 redirects before returning 200.
*/

#include <WiFi.h>
#include <StreamHTTPClient.h>

const char* WIFI_SSID     = "your-ssid";
const char* WIFI_PASSWORD = "your-password";

StreamHTTPClient http;

void tryRedirect(StreamHTTPClient_RedirectPolicy policy, const char* label) {
    Serial.printf("\n--- %s ---\n", label);
    http.begin("http://httpbin.org/redirect/3");
    http.setFollowRedirects(policy);
    http.setRedirectLimit(10);

    // Capture the Location header in case we are NOT following redirects.
    const char* keys[] = {"Location"};
    http.collectHeaders(keys, 1);

    int code = http.GET();
    Serial.printf("[HTTP] Final status: %d\n", code);
    if (code > 0 && code < 400 && http.header("Location").length() == 0) {
        Serial.println("[HTTP] Body:");
        Serial.println(http.getString().substring(0, 200));
    } else if (http.hasHeader("Location")) {
        Serial.printf("[HTTP] Server redirected us to: %s\n",
                      http.header("Location").c_str());
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

    // 1. Default: follow GET-only redirects.
    tryRedirect(SHC_REDIRECT_FOLLOW_GET_ONLY, "Follow GET-only (default)");

    // 2. Disable redirect following.
    tryRedirect(SHC_REDIRECT_OFF, "No redirect following");

    // 3. Follow all redirects (including on POST/PUT).
    tryRedirect(SHC_REDIRECT_FOLLOW_ALL, "Follow all redirects");
}

void loop() {}
