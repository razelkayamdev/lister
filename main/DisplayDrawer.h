#pragma once

#include <Arduino.h>
#include <SPI.h>

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// Explicit display type — DO NOT infer from main.ino
using DisplayType =
	GxEPD2_BW<GxEPD2_420_GDEY042T81,
			  GxEPD2_420_GDEY042T81::HEIGHT>;

class DisplayDrawer
{
public:
	DisplayDrawer(
		DisplayType &display,
		int sck, int miso, int mosi, int cs,
		int preferredRotation,
		int targetW,
		int targetH);

	void begin(uint32_t serialBaudForInit);

	void showStatus(const char *line1, const char *line2);

	void drawLines(const String *lines, size_t count);
	void drawLines(const char *const *lines, size_t count);

	void drawBitmap1bpp(const uint8_t *bitmap, bool invert);

private:
	void drawLinesInternal(const char *const *lines, size_t count, bool isStatus);
	int pickRotationForTarget(int targetW, int targetH, int preferred);

private:
	DisplayType &_display;

	int _sck, _miso, _mosi, _cs;
	int _rotation;
	int _preferredRotation;
	int _targetW;
	int _targetH;
};
