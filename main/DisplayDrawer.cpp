#include "DisplayDrawer.h"

static constexpr int MARGIN_X = 10;
static constexpr int START_Y = 40;
static constexpr int LINE_GAP = 34;

DisplayDrawer::DisplayDrawer(
	DisplayType &display,
	int sck, int miso, int mosi, int cs,
	int preferredRotation,
	int targetW,
	int targetH)
	: _display(display),
	  _sck(sck), _miso(miso), _mosi(mosi), _cs(cs),
	  _rotation(preferredRotation),
	  _preferredRotation(preferredRotation),
	  _targetW(targetW),
	  _targetH(targetH)
{
}

int DisplayDrawer::pickRotationForTarget(int targetW, int targetH, int preferred)
{
	// Try preferred first, then the others.
	int order[4] = {preferred, 0, 1, 2};

	bool used[4] = {false, false, false, false};

	for (int i = 0; i < 4; i++)
	{
		int r = order[i];
		if (r < 0 || r > 3)
			continue;
		if (used[r])
			continue;
		used[r] = true;

		_display.setRotation(r);
		int w = _display.width();
		int h = _display.height();

		if (w == targetW && h == targetH)
			return r;
	}

	// If no exact match, keep preferred (but you’ll see it in Serial).
	_display.setRotation(preferred);
	return preferred;
}

void DisplayDrawer::begin(uint32_t serialBaudForInit)
{
	SPI.begin(_sck, _miso, _mosi, _cs);

	_display.init(serialBaudForInit, true, 2, false);

	// Choose a rotation that yields targetW x targetH (400x300)
	_rotation = pickRotationForTarget(_targetW, _targetH, _preferredRotation);
	_display.setRotation(_rotation);

	Serial.printf("[EPD] rotation=%d width=%d height=%d (target=%dx%d)\n",
				  _rotation, _display.width(), _display.height(), _targetW, _targetH);
}

void DisplayDrawer::showStatus(const char *line1, const char *line2)
{
	const char *lines[2];
	size_t count = 0;
	lines[count++] = line1;
	if (line2)
		lines[count++] = line2;

	drawLinesInternal(lines, count, true);
}

void DisplayDrawer::drawLines(const String *lines, size_t count)
{
	const size_t MAX_LOCAL = 24;
	const char *ptrs[MAX_LOCAL];
	size_t n = (count > MAX_LOCAL) ? MAX_LOCAL : count;

	for (size_t i = 0; i < n; i++)
		ptrs[i] = lines[i].c_str();

	drawLinesInternal(ptrs, n, false);
}

void DisplayDrawer::drawLines(const char *const *lines, size_t count)
{
	drawLinesInternal(lines, count, false);
}

void DisplayDrawer::drawLinesInternal(const char *const *lines, size_t count, bool isStatus)
{
	_display.setRotation(_rotation);
	_display.setFullWindow();

	_display.firstPage();
	do
	{
		_display.fillScreen(GxEPD_WHITE);
		_display.setFont(&FreeMonoBold12pt7b);
		_display.setTextColor(GxEPD_BLACK);

		int y = START_Y;

		if (isStatus)
		{
			_display.setCursor(MARGIN_X, y);
			_display.print("STATUS:");
			y += LINE_GAP;
		}

		for (size_t i = 0; i < count; i++)
		{
			if (!lines[i])
				continue;
			_display.setCursor(MARGIN_X, y);
			_display.print(lines[i]);
			y += LINE_GAP;
		}
	} while (_display.nextPage());
}

void DisplayDrawer::drawBitmap1bpp(const uint8_t *bitmap, bool invert)
{
	_display.setRotation(_rotation);

	_display.setFullWindow(); // Force full-window in the chosen orientation.

	// Critical: draw using the actual logical width/height after rotation selection.
	const int16_t w = _display.width();
	const int16_t h = _display.height();

	// If these don’t equal 400x300, your PBM stride won’t match and the image will “misplace”.
	// This print makes it obvious.
	Serial.printf("[EPD] drawBitmap1bpp w=%d h=%d invert=%d\n", w, h, invert ? 1 : 0);

	_display.firstPage();
	do
	{
		_display.fillScreen(invert ? GxEPD_BLACK : GxEPD_WHITE);

		// PBM P4: 1=black, MSB-first. This is the correct call path.
		if (invert)
			_display.drawInvertedBitmap(0, 0, bitmap, w, h, GxEPD_WHITE);
		else
			_display.drawBitmap(0, 0, bitmap, w, h, GxEPD_BLACK);

	} while (_display.nextPage());
}
