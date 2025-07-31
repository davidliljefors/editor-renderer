#pragma once

#include "Core/Types.h"

void Random_initialize_context();

void Random_bits(void* buffer, u64 length);

Guid Random_guid();

u64 Random_u64();