#include "Random.h"

#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")

struct Xoshiro256
{
	u64 s[4];
};

static u64 xoshiro_rotl(const u64 x, int k)
{
	return (x << k) | (x >> (64 - k));
}

static u64 xoshiro_next(Xoshiro256* state)
{
	const u64 result = xoshiro_rotl(state->s[1] * 5, 7) * 9;
	const u64 t = state->s[1] << 17;

	state->s[2] ^= state->s[0];
	state->s[3] ^= state->s[1];
	state->s[1] ^= state->s[2];
	state->s[0] ^= state->s[3];

	state->s[2] ^= t;
	state->s[3] = xoshiro_rotl(state->s[3], 45);

	return result;
}

void xoshiro_random_bytes(Xoshiro256* state, u8* buffer, u64 length)
{
	u64 i = 0;

	while (i + 8 <= length)
	{
		u64 val = xoshiro_next(state);
		memcpy(buffer + i, &val, 8);
		i += 8;
	}

	u64 bitsLet = length & 7;
	if (bitsLet > 0)
	{
		u64 val = xoshiro_next(state);
		switch (bitsLet)
		{
		case 7: buffer[i + 6] = (val >> 48) & 0xFF; [[fallthrough]];
		case 6: buffer[i + 5] = (val >> 40) & 0xFF; [[fallthrough]];
		case 5: buffer[i + 4] = (val >> 32) & 0xFF; [[fallthrough]];
		case 4: buffer[i + 3] = (val >> 24) & 0xFF; [[fallthrough]];
		case 3: buffer[i + 2] = (val >> 16) & 0xFF; [[fallthrough]];
		case 2: buffer[i + 1] = (val >> 8) & 0xFF;  [[fallthrough]];
		case 1: buffer[i] = val & 0xFF;
		}
	}
}

Xoshiro256 g_rand;

void Random_initialize_context()
{
	HCRYPTPROV hCryptProv = 0;
	u8 seed[32];

	BOOL success = CryptAcquireContext(&hCryptProv, nullptr, MS_ENHANCED_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
	if (!success)
	{
		fprintf(stderr, "CryptAcquireContext failed: %lu\n", GetLastError());
	}

	success = CryptGenRandom(hCryptProv, sizeof(seed), seed);
	if (!success)
	{
		fprintf(stderr, "CryptGenRandom failed: %lu\n", GetLastError());
		CryptReleaseContext(hCryptProv, 0);
	}

	CryptReleaseContext(hCryptProv, 0);

	memcpy(g_rand.s, seed, sizeof(g_rand.s));

	if (g_rand.s[0] == 0 && g_rand.s[1] == 0 && g_rand.s[2] == 0 && g_rand.s[3] == 0)
	{
		g_rand.s[0] = 1;
	}
}

void Random_bits(void* buffer, u64 length)
{
	xoshiro_random_bytes(&g_rand, (u8*)buffer, length);
}

Guid Random_guid()
{
	Guid g;
	Random_bits(&g, sizeof (Guid));
	return g;
}

u64 Random_u64()
{
	u64 u;
	Random_bits(&u, sizeof (u64));
	return u;
}
