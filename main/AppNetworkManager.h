#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

class AppNetworkManager
{
public:
	using ChunkCallback = bool (*)(const uint8_t *data, size_t len, void *user);

	AppNetworkManager(const char *ssid, const char *pass);

	void setInsecureHttps(bool enabled);
	void disableBluetooth();

	bool connectWiFi(uint32_t timeoutMs);
	bool isConnected() const;

	bool syncTimeNtp(uint32_t timeoutMs);

	bool httpGet(const char *url, String &responseBody, uint32_t timeoutMs);

	bool httpGetStream(
		const char *url,
		ChunkCallback cb,
		void *user,
		uint32_t timeoutMs,
		int *outHttpCode = nullptr,
		String *outError = nullptr,
		String *outContentType = nullptr,
		int *outContentLength = nullptr);

private:
	bool isHttpsUrl(const char *url) const;

	bool httpsGetRaw(
		const char *url,
		ChunkCallback cb,
		void *user,
		uint32_t timeoutMs,
		int *outHttpCode,
		String *outError,
		String *outContentType,
		int *outContentLength);

private:
	const char *_ssid;
	const char *_pass;
	bool _insecureHttps = false;
};
