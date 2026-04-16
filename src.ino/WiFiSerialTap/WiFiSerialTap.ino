// WiFiSerialTap — Wireless Serial Sniffer & Power Pass-Through
// Target: ESP32-WROOM-32 (Arduino Core)
// Depends: ESP32WiFi (v01dma1n), ESPAsyncWebServer, AsyncTCP

#include "WSTConfig.h"
#include "WSTPreferences.h"
#include "WSTAccessPointManager.h"
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

// ---------------------------------------------------------------------------
// Hardware pins
// ---------------------------------------------------------------------------
static constexpr uint8_t TAP_RX_PIN = 16;
static constexpr uint8_t TAP_TX_PIN = 17;

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static constexpr int      WIFI_CONNECT_ATTEMPTS = 20;
static constexpr size_t   SERIAL_BUF_SIZE       = 512;
static constexpr uint32_t RECONNECT_INTERVAL_MS = 30000;

static const char* WST_HOSTNAME = "WiFiSerialTap";

// ---------------------------------------------------------------------------
// Telnet server (single client)
// ---------------------------------------------------------------------------
class WSTTelnetServer {
public:
    void begin(uint16_t port) {
        _server = new WiFiServer(port);
        _server->begin();
        _server->setNoDelay(true);
        Serial.printf("[telnet] listening on port %u\n", port);
    }

    void loop() {
        if (!_server) return;
        if (_server->hasClient()) {
            if (_client && _client.connected()) {
                _client.println("[wst] replaced by new client");
                _client.stop();
            }
            _client = _server->accept();
            _client.setNoDelay(true);
            Serial.printf("[telnet] client %s connected\n",
                          _client.remoteIP().toString().c_str());
        }
    }

    bool hasClient() { return _client && _client.connected(); }

    void send(const uint8_t* buf, size_t len) {
        if (hasClient() && len > 0) _client.write(buf, len);
    }

    size_t recv(uint8_t* buf, size_t maxLen) {
        if (!hasClient()) return 0;
        size_t avail = _client.available();
        if (avail == 0) return 0;
        if (avail > maxLen) avail = maxLen;
        return _client.read(buf, avail);
    }

    void stop() {
        if (_client) _client.stop();
        if (_server) { _server->stop(); delete _server; _server = nullptr; }
    }

private:
    WiFiServer* _server = nullptr;
    WiFiClient  _client;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static WSTConfig              cfg;
static WSTPreferences         prefs(cfg);
static WSTAccessPointManager  apMgr(prefs);
static WSTTelnetServer        telnet;
static uint8_t                rxBuf[SERIAL_BUF_SIZE];

static bool     wifiUp        = false;
static bool     telnetUp      = false;
static uint32_t lastReconnect = 0;

// ---------------------------------------------------------------------------
// Non-blocking WiFi reconnect
// ---------------------------------------------------------------------------
static void tryReconnect() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiUp) {
            wifiUp = true;
            Serial.printf("[wifi] connected  ip=%s\n",
                          WiFi.localIP().toString().c_str());
        }
        return;
    }
    wifiUp = false;
    if (millis() - lastReconnect < RECONNECT_INTERVAL_MS) return;
    lastReconnect = millis();
    Serial.println("[wifi] attempting reconnect...");
    WiFi.disconnect();
    WiFi.begin(cfg.ssid, cfg.password);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) { ; }
    Serial.println("\n[wst] WiFiSerialTap starting");

    // Suppress floating noise when target is unpowered
    pinMode(TAP_RX_PIN, INPUT_PULLUP);

    // Load persisted configuration (WiFi creds + baud + port)
    prefs.setup();
    prefs.dumpPreferences();

    // Tap serial
    Serial2.begin(cfg.baud_rate, SERIAL_8N1, TAP_RX_PIN, TAP_TX_PIN);
    Serial.printf("[wst] Serial2 @ %u baud  RX=%u TX=%u\n",
                  cfg.baud_rate, TAP_RX_PIN, TAP_TX_PIN);

    // Attempt WiFi STA connection (blocking, bounded by attempt count)
    bool connected = WiFiConnect(WST_HOSTNAME, cfg.ssid, cfg.password,
                                 WIFI_CONNECT_ATTEMPTS);
    if (!connected) {
        Serial.println("[wst] STA failed — launching captive portal");
        apMgr.setup(WST_HOSTNAME);
        // Blocking portal loop; device restarts after user saves config
        apMgr.runBlocking([](bool clientConnected) {
            // Mirror tap data to Serial0 even while portal is active
            size_t avail = Serial2.available();
            if (avail > 0) {
                if (avail > SERIAL_BUF_SIZE) avail = SERIAL_BUF_SIZE;
                size_t n = Serial2.readBytes(rxBuf, avail);
                Serial.write(rxBuf, n);
            }
        });
        // runBlocking never returns — device restarts on save
    }

    wifiUp = true;
    Serial.printf("[wifi] connected  ip=%s\n",
                  WiFi.localIP().toString().c_str());

    // Start telnet server
    telnet.begin(cfg.telnet_port);
    telnetUp = true;
}

// ---------------------------------------------------------------------------
// loop() — non-blocking after setup completes
// ---------------------------------------------------------------------------
void loop() {
    tryReconnect();

    // Manage telnet lifecycle based on WiFi state
    if (wifiUp && !telnetUp) {
        telnet.begin(cfg.telnet_port);
        telnetUp = true;
    }
    if (!wifiUp && telnetUp) {
        telnet.stop();
        telnetUp = false;
    }

    if (telnetUp) telnet.loop();

    // Serial2 → Serial0 (local mirror) + Telnet client
    size_t avail = Serial2.available();
    if (avail > 0) {
        if (avail > SERIAL_BUF_SIZE) avail = SERIAL_BUF_SIZE;
        size_t n = Serial2.readBytes(rxBuf, avail);
        Serial.write(rxBuf, n);
        telnet.send(rxBuf, n);
    }

    // Telnet client → Serial2 (reverse channel to target)
    if (telnetUp) {
        size_t n = telnet.recv(rxBuf, SERIAL_BUF_SIZE);
        if (n > 0) Serial2.write(rxBuf, n);
    }
}
