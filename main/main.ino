#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include "esp_sleep.h"
#include <time.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#include "DisplayDrawer.h"
#include "AppNetworkManager.h"
#include "ItemsClient.h"

// ==================== CONFIG ====================

static const char *WIFI_SSID = "";
static const char *WIFI_PASS = "";
static const char *ITEMS_URL = "http://raspberrypi4.local:3001/list/items.pbm";

static constexpr uint64_t SLEEP_MINUTES = 10;
static constexpr uint64_t uS_TO_S_FACTOR = 1000000ULL;
static constexpr uint64_t SLEEP_DURATION_US = SLEEP_MINUTES * 60ULL * uS_TO_S_FACTOR;

// Waveshare ESP32 e-Paper Driver Board pins
#define EPD_SCK 13
#define EPD_MOSI 14
#define EPD_MISO 19
#define EPD_CS 15
#define EPD_DC 27
#define EPD_RST 26
#define EPD_BUSY 25

static uint8_t pbmBuf[15000]; // 400x300 => ((400+7)/8)*300 = 15000

static void printWakeReason()
{
	esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
	Serial.print("[WAKE] cause=");
	switch (cause)
	{
	case ESP_SLEEP_WAKEUP_TIMER:
		Serial.println("TIMER");
		break;
	case ESP_SLEEP_WAKEUP_UNDEFINED:
		Serial.println("UNDEFINED (power-on/reset)");
		break;
	default:
		Serial.println((int)cause);
		break;
	}
}

static bool syncTimeUtc(uint32_t timeoutMs = 5000)
{
	configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // SNTP: returns UTC epoch; we will format with gmtime_r()

	const uint32_t start = millis();
	while (millis() - start < timeoutMs)
	{
		time_t now = time(nullptr);
		if (now > 1700000000) // sanity: ~2023-11+
			return true;
		delay(100);
	}
	return false;
}

static String nowStringUtc()
{
	time_t now = time(nullptr);
	if (now <= 0)
		return String("UTC unavailable");

	struct tm tm;
	gmtime_r(&now, &tm);

	char buf[32];
	strftime(buf, sizeof(buf), "%d-%m-%Y @ %H:%M UTC", &tm); // Example: 29-12-2025 08:10 UTC
	return String(buf);
}

class App
{
public:
	App()
		: display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)),
		  drawer(display,
				 EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS,
				 /*preferredRotation*/ 1,
				 /*targetW*/ 400,
				 /*targetH*/ 300),
		  net(WIFI_SSID, WIFI_PASS),
		  itemsClient(net, ITEMS_URL)
	{
	}

	void begin()
	{
		Serial.begin(115200);
		delay(300);

		printWakeReason();

		drawer.begin(115200);

		net.setInsecureHttps(true); // TLS policy: insecure by choice

		bootFlow();

		goToSleep(); // Always go to sleep after the one-shot flow
	}

	void loop()
	{
		// one-shot; we never stay awake
	}

private:
	void bootFlow()
	{
		drawer.showStatus("Loading...", nullptr);
		// drawer.showStatus("WiFi", "Connecting...");
		if (!net.connectWiFi(15000))
		{
			drawer.showStatus("WiFi FAILED", "Timeout");
			return;
		}

		// drawer.showStatus("TIME", "Syncing UTC..."); // Sync UTC time via NTP (optional but needed for timestamps)
		const bool timeOk = syncTimeUtc(5000);

		String ip = WiFi.localIP().toString();
		// drawer.showStatus("WiFi Connected", ip.c_str());

		// drawer.showStatus("HTTP", "Fetching PBM...");
		if (!itemsClient.fetchPbmP4(pbmBuf, sizeof(pbmBuf), 400, 300, 15000))
		{
			const String ts = timeOk ? nowStringUtc() : String("UTC unavailable");
			drawer.showStatus("Fetching FAILED", ts.c_str());
			return;
		}

		// drawer.showStatus("Display", "Rendering...");
		drawer.drawBitmap1bpp(pbmBuf, false);

		display.hibernate(); // Put panel/controller into low power; image remains on e-ink
	}

	void goToSleep()
	{
		WiFi.disconnect(true);
		WiFi.mode(WIFI_OFF);

		esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);

		Serial.printf("[SLEEP] deep sleep for %llu minutes (%llu us)\n",
					  (unsigned long long)SLEEP_MINUTES,
					  (unsigned long long)SLEEP_DURATION_US);
		Serial.flush();

		esp_deep_sleep_start();
	}

private:
	// IMPORTANT: declaration order matters (constructed top → bottom)
	GxEPD2_BW<GxEPD2_420_GDEY042T81,
			  GxEPD2_420_GDEY042T81::HEIGHT>
		display;

	DisplayDrawer drawer;
	AppNetworkManager net;
	ItemsClient itemsClient;
};

static App app;

void setup()
{
	app.begin();
}

void loop()
{
	app.loop();
}
