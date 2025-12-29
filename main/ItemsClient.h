#pragma once
#include <Arduino.h>
#include "AppNetworkManager.h"

class ItemsClient
{
public:
	ItemsClient(AppNetworkManager &net, const char *itemsUrl);

	// P4 PBM -> outBuf must be >= bytesNeeded = ((w+7)/8)*h
	bool fetchPbmP4(uint8_t *outBuf, size_t outLen, int expectedW, int expectedH, uint32_t timeoutMs = 15000);

private:
	AppNetworkManager &_net;
	const char *_itemsUrl;
};
