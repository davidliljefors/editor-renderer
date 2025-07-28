#pragma once

#include <string.h>

#include "Core/Types.h"

static inline u64 murmur_hash(const void* key, u32 len, u64 seed);

static inline u64 murmur_hash_combine(u64 a, u64 b);

static inline u64 murmur_hash_string(const char* s);

static inline u64 murmur_hash_tolower(const void* key, u32 len, u64 seed);

static inline u64 murmur_hash_string_tolower(const char* s);

static inline u64 murmur_hash(const void* key, u32 len, u64 seed)
{
	const u64 m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;

	u64 h = seed ^ (len * m);

	const u64* data = (const u64*)key;
	const u64* end = data + (len / 8);

	while (data != end) {
		u64 k = *data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	const unsigned char* data2 = (const unsigned char*)data;

	switch (len & 7) {
	case 7:
		h ^= (u64)(data2[6]) << 48;
	case 6:
		h ^= (u64)(data2[5]) << 40;
	case 5:
		h ^= (u64)(data2[4]) << 32;
	case 4:
		h ^= (u64)(data2[3]) << 24;
	case 3:
		h ^= (u64)(data2[2]) << 16;
	case 2:
		h ^= (u64)(data2[1]) << 8;
	case 1:
		h ^= (u64)(data2[0]);
		h *= m;
	}

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

static inline u64 private__tolower_u64(const u64* c, u8 read_bytes)
{
	char bytes[8] = { 0 };
	memcpy(bytes, c, read_bytes < sizeof(bytes) ? read_bytes : sizeof(bytes));
	for (u32 i = 0; i < 8; ++i)
		bytes[i] = (bytes[i] >= 'A' && bytes[i] <= 'Z' ? bytes[i] - 'A' + 'a' : bytes[i]);
	u64 ret;
	memcpy(&ret, bytes, sizeof(ret));
	return ret;
}

static inline u64 murmur_hash_tolower(const void* key, u32 len, u64 seed)
{
	const u64 m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;

	u64 h = seed ^ (len * m);

	const u64* data = (const u64*)key;
	const u64* end = data + (len / 8);

	while (data != end) {
		u64 k = private__tolower_u64(data++, sizeof(u64));

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	u8 remain = (u8)(len - ((const char*)data - (const char*)key));
	const u64 klast = private__tolower_u64(data, remain);
	const unsigned char* data2 = (const unsigned char*)&klast;

	switch (len & 7) {
	case 7:
		h ^= (u64)(data2[6]) << 48;
	case 6:
		h ^= (u64)(data2[5]) << 40;
	case 5:
		h ^= (u64)(data2[4]) << 32;
	case 4:
		h ^= (u64)(data2[3]) << 24;
	case 3:
		h ^= (u64)(data2[2]) << 16;
	case 2:
		h ^= (u64)(data2[1]) << 8;
	case 1:
		h ^= (u64)(data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

static inline u64 murmur_hash_combine(u64 a, u64 b)
{
	const u64 m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;

	u64 h = 7659067388010076496ULL;

	u64 k = a;

	k *= m;
	k ^= k >> r;
	k *= m;

	h ^= k;
	h *= m;

	k = b;

	k *= m;
	k ^= k >> r;
	k *= m;

	h ^= k;
	h *= m;

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

static inline u64 murmur_hash_string(const char* s)
{
	return s ? murmur_hash(s, (u32)strlen(s), 0) : 0;
}

static inline u64 murmur_hash_string_tolower(const char* s)
{
	return s ? murmur_hash_tolower(s, (u32)strlen(s), 0) : 0;
}
