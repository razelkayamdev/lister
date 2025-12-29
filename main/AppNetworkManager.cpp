#include "AppNetworkManager.h"
#include <esp_bt.h>
#include <time.h>

// ---------------- utils ----------------

static bool startsWith(const char *s, const char *p)
{
	return s && p && strncmp(s, p, strlen(p)) == 0;
}

// Supports http://host[:port]/path and https://host[:port]/path
static bool parseUrl(const char *url, String &host, uint16_t &port, String &path)
{
	host = "";
	path = "/";
	port = 443;

	if (!url)
		return false;

	if (startsWith(url, "https://"))
	{
		url += 8;
		port = 443;
	}
	else if (startsWith(url, "http://"))
	{
		url += 7;
		port = 80;
	}
	else
	{
		return false;
	}

	const char *slash = strchr(url, '/');
	size_t hostLen = slash ? (size_t)(slash - url) : strlen(url);
	if (hostLen == 0)
		return false;

	String hostPort = String(url).substring(0, hostLen);
	int colon = hostPort.indexOf(':');
	if (colon >= 0)
	{
		host = hostPort.substring(0, colon);
		int prt = hostPort.substring(colon + 1).toInt();
		if (prt <= 0 || prt > 65535)
			return false;
		port = (uint16_t)prt;
	}
	else
	{
		host = hostPort;
	}

	if (slash)
		path = String(slash);
	return host.length() > 0;
}

// Stream-safe read: never call readBytes() for fixed-length bodies.
// Instead, read only what is available, and fail on "no-progress" stall.
static bool streamReadExact(
	WiFiClient &client,
	int totalLen,
	AppNetworkManager::ChunkCallback cb,
	void *user,
	uint32_t overallTimeoutMs,
	String *outError,
	uint32_t stallTimeoutMs = 8000)
{
	uint8_t buf[1024];

	const uint32_t startMs = millis();
	uint32_t lastProgressMs = startMs;

	int remaining = totalLen;
	while (remaining > 0)
	{
		if (overallTimeoutMs && (millis() - startMs) > overallTimeoutMs)
		{
			if (outError)
				*outError = "Body read timeout";
			return false;
		}

		int avail = client.available();

		if (avail <= 0)
		{
			// Important: with TLS, connected() may go false even if bytes are still pending.
			// Only consider it "lost" if we have neither connected nor available.
			if (!(client.connected() || client.available()))
			{
				if (outError)
					*outError = "Body read lost (socket closed early)";
				return false;
			}

			if ((millis() - lastProgressMs) > stallTimeoutMs)
			{
				if (outError)
					*outError = "Body read stalled";
				return false;
			}

			delay(1);
			yield();
			continue;
		}

		int toRead = avail;
		if (toRead > (int)sizeof(buf))
			toRead = (int)sizeof(buf);
		if (toRead > remaining)
			toRead = remaining;

		int r = client.read(buf, toRead);
		if (r < 0)
		{
			if (outError)
				*outError = "Body read error";
			return false;
		}
		if (r == 0)
		{
			// No bytes right now; keep looping until stall timeout
			delay(1);
			yield();
			continue;
		}

		if (!cb(buf, (size_t)r, user))
		{
			if (outError)
				*outError = "Aborted by callback";
			return false;
		}

		remaining -= r;
		lastProgressMs = millis();
	}

	return true;
}

// Similar helper for chunked bodies: reads "n" data bytes exactly (no CRLF).
static bool streamReadExactChunk(
	WiFiClient &client,
	int totalLen,
	AppNetworkManager::ChunkCallback cb,
	void *user,
	uint32_t overallTimeoutMs,
	String *outError,
	uint32_t stallTimeoutMs = 8000)
{
	return streamReadExact(client, totalLen, cb, user, overallTimeoutMs, outError, stallTimeoutMs);
}

// ---------------- class ----------------

AppNetworkManager::AppNetworkManager(const char *ssid, const char *pass)
	: _ssid(ssid), _pass(pass) {}

void AppNetworkManager::setInsecureHttps(bool enabled)
{
	_insecureHttps = enabled;
}

void AppNetworkManager::disableBluetooth()
{
	btStop();
}

bool AppNetworkManager::isHttpsUrl(const char *url) const
{
	return url && (strncmp(url, "https://", 8) == 0);
}

// ---------------- time sync ----------------

bool AppNetworkManager::syncTimeNtp(uint32_t timeoutMs)
{
	configTime(0, 0, "pool.ntp.org", "time.nist.gov");

	time_t now = 0;
	uint32_t start = millis();
	while ((millis() - start) < timeoutMs)
	{
		time(&now);
		if (now > 1700000000) // ~late 2023
		{
			Serial.printf("[TIME] synced: %ld\n", (long)now);
			return true;
		}
		delay(200);
	}

	Serial.println("[TIME] sync failed");
	return false;
}

// ---------------- WiFi ----------------

bool AppNetworkManager::connectWiFi(uint32_t timeoutMs)
{
	disableBluetooth();

	WiFi.mode(WIFI_STA);
	WiFi.disconnect(true, true);
	delay(100);

	Serial.printf("Connecting to WiFi: %s\n", _ssid);
	WiFi.begin(_ssid, _pass);

	uint32_t start = millis();
	while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
	{
		delay(250);
		Serial.print(".");
	}
	Serial.println();

	if (WiFi.status() == WL_CONNECTED)
	{
		Serial.println("WiFi connected");
		Serial.println(WiFi.localIP());
		syncTimeNtp(15000);
		return true;
	}

	Serial.println("WiFi FAILED (timeout)");
	return false;
}

bool AppNetworkManager::isConnected() const
{
	return WiFi.status() == WL_CONNECTED;
}

// ---------------- public GET -> String ----------------

bool AppNetworkManager::httpGet(const char *url, String &responseBody, uint32_t timeoutMs)
{
	responseBody = "";

	String err;
	int code = 0;

	bool ok = httpGetStream(
		url,
		[](const uint8_t *data, size_t len, void *user) -> bool
		{
			String *out = (String *)user;
			out->reserve(out->length() + (int)len);
			for (size_t i = 0; i < len; i++)
				out->concat((char)data[i]);
			return true;
		},
		&responseBody,
		timeoutMs,
		&code,
		&err);

	if (!ok)
	{
		Serial.printf("HTTP GET failed (%d): %s\n", code, err.c_str());
		return false;
	}

	return true;
}

// ---------------- stream GET ----------------

bool AppNetworkManager::httpGetStream(
	const char *url,
	ChunkCallback cb,
	void *user,
	uint32_t timeoutMs,
	int *outHttpCode,
	String *outError,
	String *outContentType,
	int *outContentLength)
{
	if (outHttpCode)
		*outHttpCode = 0;
	if (outError)
		*outError = "";
	if (outContentType)
		*outContentType = "";
	if (outContentLength)
		*outContentLength = -1;

	if (!isConnected())
	{
		if (outError)
			*outError = "WiFi not connected";
		Serial.println("HTTP GET skipped: WiFi not connected");
		return false;
	}
	if (!url || !cb)
	{
		if (outError)
			*outError = "Bad args";
		return false;
	}

	Serial.printf("[HTTP] GET %s\n", url);
	Serial.printf("[HTTP] RSSI: %d dBm\n", WiFi.RSSI());

	if (isHttpsUrl(url))
	{
		return httpsGetRaw(url, cb, user, timeoutMs, outHttpCode, outError, outContentType, outContentLength);
	}

	// Plain HTTP fallback
	HTTPClient http;
	http.setTimeout(timeoutMs);
	http.setReuse(false);
	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	http.addHeader("Connection", "close");
	http.addHeader("Accept-Encoding", "identity");
	http.addHeader("User-Agent", "ESP32");

	if (!http.begin(url))
	{
		if (outError)
			*outError = "HTTP begin() failed";
		return false;
	}

	int code = http.GET();
	if (outHttpCode)
		*outHttpCode = code;

	if (code <= 0)
	{
		String e = http.errorToString(code);
		if (outError)
			*outError = e;
		Serial.printf("[HTTP] GET failed (%d): %s\n", code, e.c_str());
		http.end();
		return false;
	}

	String ct = http.header("Content-Type");
	int len = http.getSize();

	if (outContentType)
		*outContentType = ct;
	if (outContentLength)
		*outContentLength = len;

	Serial.printf("[HTTP] Status: %d\n", code);
	Serial.printf("[HTTP] Content-Type: %s\n", ct.c_str());
	Serial.printf("[HTTP] Content-Length: %d\n", len);

	if (!(code >= 200 && code < 300))
	{
		if (outError)
			*outError = "Non-2xx";
		http.end();
		return false;
	}

	Stream &s = http.getStream();
	uint8_t buf[1024];

	uint32_t startMs = millis();
	uint32_t lastProgressMs = startMs;
	const uint32_t stallTimeoutMs = 8000;

	while (true)
	{
		// If we have a known length, stop exactly at len==0
		if (len == 0)
			break;

		if (timeoutMs && (millis() - startMs) > timeoutMs)
		{
			if (outError)
				*outError = "Body read timeout";
			http.end();
			return false;
		}

		int avail = s.available();
		if (avail <= 0)
		{
			if (!http.connected())
				break;

			if ((millis() - lastProgressMs) > stallTimeoutMs)
			{
				if (outError)
					*outError = "Body read stalled";
				http.end();
				return false;
			}

			delay(1);
			yield();
			continue;
		}

		int toRead = avail;
		if (toRead > (int)sizeof(buf))
			toRead = (int)sizeof(buf);
		if (len > 0 && toRead > len)
			toRead = len;

		int r = s.readBytes((char *)buf, toRead);
		if (r <= 0)
		{
			if (!http.connected())
				break;

			// treat as stall, not immediate loss
			if ((millis() - lastProgressMs) > stallTimeoutMs)
			{
				if (outError)
					*outError = "Body read stalled";
				http.end();
				return false;
			}
			delay(1);
			yield();
			continue;
		}

		if (!cb(buf, (size_t)r, user))
		{
			if (outError)
				*outError = "Aborted by callback";
			http.end();
			return false;
		}

		lastProgressMs = millis();

		if (len > 0)
		{
			len -= r;
			if (len <= 0)
				break;
		}
	}

	http.end();
	return true;
}

// ---------------- raw HTTPS (ngrok-friendly) ----------------

bool AppNetworkManager::httpsGetRaw(
	const char *url,
	ChunkCallback cb,
	void *user,
	uint32_t timeoutMs,
	int *outHttpCode,
	String *outError,
	String *outContentType,
	int *outContentLength)
{
	String host, path;
	uint16_t port = 443;

	if (!parseUrl(url, host, port, path))
	{
		if (outError)
			*outError = "Bad URL";
		return false;
	}

	WiFiClientSecure client;
	if (_insecureHttps)
		client.setInsecure();

	client.setHandshakeTimeout(30); // seconds
	// Stream::setTimeout is milliseconds on Arduino; keep it moderately large.
	client.setTimeout((timeoutMs > 0) ? timeoutMs : 15000);

	Serial.printf("[TLS] free heap: %u\n", (unsigned)ESP.getFreeHeap());
	Serial.printf("[RAW] Connect %s:%u\n", host.c_str(), port);

	if (!client.connect(host.c_str(), port))
	{
		if (outError)
			*outError = "TLS connect failed";
		if (outHttpCode)
			*outHttpCode = -1;
		return false;
	}

	// Request
	client.print("GET ");
	client.print(path);
	client.println(" HTTP/1.1");
	client.print("Host: ");
	client.println(host);
	client.println("Connection: close");
	client.println("Accept-Encoding: identity");
	client.println("User-Agent: ESP32");
	client.println("ngrok-skip-browser-warning: true");
	client.println();

	// Wait for response bytes (status line)
	uint32_t waitStart = millis();
	while ((millis() - waitStart) < timeoutMs && client.available() == 0)
	{
		if (!client.connected())
			break;
		delay(1);
		yield();
	}

	// Status line
	String line = client.readStringUntil('\n');
	if (line.length() == 0)
	{
		if (outError)
			*outError = "No status line";
		if (outHttpCode)
			*outHttpCode = 0;
		client.stop();
		return false;
	}

	line.trim();
	Serial.printf("[RAW] Status line: %s\n", line.c_str());

	if (!line.startsWith("HTTP/"))
	{
		if (outError)
			*outError = "Bad HTTP status line";
		if (outHttpCode)
			*outHttpCode = 0;
		client.stop();
		return false;
	}

	int code = 0;
	int sp1 = line.indexOf(' ');
	if (sp1 > 0)
	{
		int sp2 = line.indexOf(' ', sp1 + 1);
		String codeStr = (sp2 > sp1) ? line.substring(sp1 + 1, sp2) : line.substring(sp1 + 1);
		code = codeStr.toInt();
	}

	if (outHttpCode)
		*outHttpCode = code;
	Serial.printf("[RAW] Status: %d\n", code);

	// Headers
	int contentLen = -1;
	bool chunked = false;
	String ct;
	String location;

	while (true)
	{
		line = client.readStringUntil('\n');
		if (line.length() == 0)
		{
			if (outError)
				*outError = "Header read timeout";
			client.stop();
			return false;
		}

		line.trim();
		if (line.length() == 0)
			break;

		int colon = line.indexOf(':');
		if (colon <= 0)
			continue;

		String key = line.substring(0, colon);
		String val = line.substring(colon + 1);
		key.toLowerCase();
		val.trim();

		if (key == "content-length")
			contentLen = val.toInt();
		else if (key == "content-type")
			ct = val;
		else if (key == "transfer-encoding")
		{
			val.toLowerCase();
			if (val.indexOf("chunked") >= 0)
				chunked = true;
		}
		else if (key == "location")
			location = val;
	}

	if (outContentType)
		*outContentType = ct;
	if (outContentLength)
		*outContentLength = contentLen;

	if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && location.length() > 0)
	{
		if (outError)
			*outError = "Redirect not handled";
		client.stop();
		return false;
	}

	if (!(code >= 200 && code < 300))
	{
		if (outError)
			*outError = "Non-2xx";
		client.stop();
		return false;
	}

	// Body
	const uint32_t stallTimeoutMs = 8000;

	if (chunked)
	{
		while (true)
		{
			String szLine = client.readStringUntil('\n');
			if (szLine.length() == 0)
			{
				if (outError)
					*outError = "Chunk size timeout";
				client.stop();
				return false;
			}

			szLine.trim();
			int sz = (int)strtol(szLine.c_str(), nullptr, 16);
			if (sz <= 0)
				break;

			// Read exactly sz data bytes (stall-safe, no readBytes)
			if (!streamReadExactChunk(client, sz, cb, user, timeoutMs, outError, stallTimeoutMs))
			{
				client.stop();
				return false;
			}

			// Consume trailing CRLF after the chunk data
			// (best-effort; tolerate both \r\n and \n)
			int c1 = -1;
			uint32_t t0 = millis();
			while ((millis() - t0) < stallTimeoutMs && (c1 = client.read()) < 0)
			{
				if (!(client.connected() || client.available()))
					break;
				delay(1);
				yield();
			}
			if (c1 == '\r')
				client.read(); // try read '\n'
			else if (c1 != '\n' && c1 >= 0)
			{
				// unexpected; ignore
			}
		}
	}
	else if (contentLen >= 0)
	{
		// Read exactly contentLen bytes (stall-safe)
		if (!streamReadExact(client, contentLen, cb, user, timeoutMs, outError, stallTimeoutMs))
		{
			client.stop();
			return false;
		}
	}
	else
	{
		// Unknown length: drain until close with stall protection
		uint8_t buf[1024];
		uint32_t startMs = millis();
		uint32_t lastProgressMs = startMs;

		while (client.connected() || client.available())
		{
			if (timeoutMs && (millis() - startMs) > timeoutMs)
			{
				if (outError)
					*outError = "Body read timeout";
				client.stop();
				return false;
			}

			int avail = client.available();
			if (avail <= 0)
			{
				if ((millis() - lastProgressMs) > stallTimeoutMs)
					break;
				delay(1);
				yield();
				continue;
			}

			int toRead = avail;
			if (toRead > (int)sizeof(buf))
				toRead = (int)sizeof(buf);

			int r = client.read(buf, toRead);
			if (r <= 0)
			{
				if ((millis() - lastProgressMs) > stallTimeoutMs)
					break;
				delay(1);
				yield();
				continue;
			}

			if (!cb(buf, (size_t)r, user))
			{
				if (outError)
					*outError = "Aborted by callback";
				client.stop();
				return false;
			}

			lastProgressMs = millis();
		}
	}

	client.stop();
	return true;
}
