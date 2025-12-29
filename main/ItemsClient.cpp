#include "ItemsClient.h"

ItemsClient::ItemsClient(AppNetworkManager &net, const char *itemsUrl)
	: _net(net), _itemsUrl(itemsUrl) {}

struct PbmCtx
{
	int expectedW;
	int expectedH;
	uint8_t *dst;
	size_t cap;

	// header parsing
	bool inComment = false;
	int w = 0;
	int h = 0;
	bool okMagic = false;
	bool okHeader = false;

	// token parsing (magic, w, h)
	char token[32];
	size_t tokenLen = 0;
	int tokenIndex = 0; // 0=magic,1=w,2=h

	// data
	bool inData = false;
	size_t bytesNeeded = 0;
	size_t got = 0;

	// debug/fail
	bool failed = false;
	const char *failReason = nullptr;
};

static bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void failOnce(PbmCtx &ctx, const char *reason)
{
	if (ctx.failed)
		return;
	ctx.failed = true;
	ctx.failReason = reason;

	Serial.printf("[PBM] Callback abort: %s\n", reason);
	Serial.printf("[PBM] expected=%dx%d parsed=%dx%d magic=%s cap=%u need=%u got=%u idx=%d inData=%d\n",
				  ctx.expectedW, ctx.expectedH,
				  ctx.w, ctx.h,
				  ctx.okMagic ? "OK" : "BAD",
				  (unsigned)ctx.cap,
				  (unsigned)ctx.bytesNeeded,
				  (unsigned)ctx.got,
				  ctx.tokenIndex,
				  ctx.inData ? 1 : 0);
}

static void applyToken(PbmCtx &ctx, const char *tok)
{
	if (ctx.tokenIndex == 0)
	{
		ctx.okMagic = (strcmp(tok, "P4") == 0);
	}
	else if (ctx.tokenIndex == 1)
	{
		ctx.w = atoi(tok);
	}
	else if (ctx.tokenIndex == 2)
	{
		ctx.h = atoi(tok);

		if (!ctx.okMagic)
		{
			failOnce(ctx, "Bad magic (not P4)");
			return;
		}

		if (ctx.w != ctx.expectedW || ctx.h != ctx.expectedH)
		{
			failOnce(ctx, "Dimensions mismatch");
			return;
		}

		const size_t bytesPerRow = ((size_t)ctx.w + 7) / 8;
		ctx.bytesNeeded = bytesPerRow * (size_t)ctx.h;

		if (ctx.cap < ctx.bytesNeeded)
		{
			failOnce(ctx, "Output buffer too small");
			return;
		}

		ctx.okHeader = true;
	}
	else
	{
		// Ignore any extra tokens (shouldn't exist in valid P4 header)
	}
}

static void finishToken(PbmCtx &ctx)
{
	if (ctx.tokenLen == 0)
		return;
	ctx.token[ctx.tokenLen] = '\0';

	applyToken(ctx, ctx.token);

	ctx.tokenLen = 0;
	if (ctx.tokenIndex < 3)
		ctx.tokenIndex++; // only advance through magic,w,h
}

static bool onPbmBytes(const uint8_t *data, size_t len, void *user)
{
	PbmCtx &ctx = *(PbmCtx *)user;

	for (size_t i = 0; i < len; i++)
	{
		uint8_t b = data[i];
		char c = (char)b;

		// If we've already failed, abort immediately.
		if (ctx.failed)
			return false;

		// ---------------- header state machine ----------------
		if (!ctx.inData)
		{
			// Comments start with '#', end at newline
			if (ctx.inComment)
			{
				if (c == '\n' || c == '\r')
					ctx.inComment = false;
				continue;
			}

			if (c == '#')
			{
				// If a token was being built and a comment starts, that's invalid for PBM,
				// but we'll just treat it as token boundary.
				finishToken(ctx);
				ctx.inComment = true;
				continue;
			}

			// Whitespace separates tokens
			if (isWs(c))
			{
				if (ctx.tokenIndex < 3)
					finishToken(ctx);

				// After header tokens are complete, PBM allows whitespace before binary data.
				// Keep skipping until first non-ws byte.
				continue;
			}

			// If we still need magic/w/h tokens, accumulate token characters
			if (ctx.tokenIndex < 3)
			{
				if (ctx.tokenLen + 1 >= sizeof(ctx.token))
				{
					failOnce(ctx, "Header token too long");
					return false;
				}

				ctx.token[ctx.tokenLen++] = c;
				continue;
			}

			// We already consumed magic/w/h. Current non-ws non-comment byte is first data byte.
			if (!ctx.okHeader)
			{
				failOnce(ctx, "Header invalid before data start");
				return false;
			}

			ctx.inData = true;
			// fallthrough to data write for this byte
		}

		// ---------------- data ----------------
		if (ctx.got < ctx.bytesNeeded)
		{
			ctx.dst[ctx.got++] = b;
		}
		else
		{
			// Ignore trailing bytes beyond expected bitmap (shouldn't exist, but harmless)
		}
	}

	return true;
}

bool ItemsClient::fetchPbmP4(uint8_t *outBuf, size_t outLen, int expectedW, int expectedH, uint32_t timeoutMs)
{
	PbmCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.expectedW = expectedW;
	ctx.expectedH = expectedH;
	ctx.dst = outBuf;
	ctx.cap = outLen;

	int httpCode = 0;
	String err;
	String ct;
	int contentLen = -1;

	Serial.printf("[PBM] GET %s\n", _itemsUrl);

	bool ok = _net.httpGetStream(
		_itemsUrl,
		onPbmBytes,
		&ctx,
		timeoutMs,
		&httpCode,
		&err,
		&ct,
		&contentLen);

	// If stream ended while we were still parsing the header token (rare), flush it.
	if (!ctx.inData && !ctx.failed)
	{
		finishToken(ctx);
	}

	Serial.printf("[PBM] HTTP code: %d\n", httpCode);
	Serial.printf("[PBM] Content-Type: %s\n", ct.c_str());
	Serial.printf("[PBM] Content-Length: %d\n", contentLen);

	if (!ok)
	{
		Serial.printf("[PBM] GET/stream failed: %s\n", err.c_str());
		if (ctx.failed && ctx.failReason)
			Serial.printf("[PBM] Parser reason: %s\n", ctx.failReason);
		return false;
	}

	Serial.printf("[PBM] Parsed header: %dx%d (magic=%s header=%s)\n",
				  ctx.w, ctx.h,
				  ctx.okMagic ? "OK" : "BAD",
				  ctx.okHeader ? "OK" : "BAD");
	Serial.printf("[PBM] Expect bitmap bytes: %u\n", (unsigned)ctx.bytesNeeded);
	Serial.printf("[PBM] Read bitmap bytes: %u\n", (unsigned)ctx.got);

	if (!ctx.okHeader)
	{
		Serial.printf("[PBM] Header invalid\n");
		return false;
	}

	if (ctx.got != ctx.bytesNeeded)
	{
		Serial.printf("[PBM] Incomplete bitmap (got %u need %u)\n",
					  (unsigned)ctx.got, (unsigned)ctx.bytesNeeded);
		return false;
	}

	return true;
}
