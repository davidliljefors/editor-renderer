// metrohash64.h
//
// Copyright 2015-2018 J. Andrew Rogers
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>

class MetroHash64
{
public:
    static const uint32_t bits = 64;

    // Constructor initializes the same as Initialize()
    MetroHash64(uint64_t seed=0);
    
    // Initializes internal state for new hash with optional seed
    void Initialize(uint64_t seed=0);
    
    // Update the hash state with a string of bytes. If the length
    // is sufficiently long, the implementation switches to a bulk
    // hashing algorithm directly on the argument buffer for speed.
    void Update(const uint8_t* buffer, uint64_t length);
    void Update(const int8_t* buffer, uint64_t length);
    
    // Constructs the final hash and writes it to the argument buffer.
    // After a hash is finalized, this instance must be Initialized()-ed
    // again or the behavior of Update() and Finalize() is undefined.
    void Finalize(uint8_t* hash);
    uint64_t Finalize();
    
    // A non-incremental function implementation. This can be significantly
    // faster than the incremental implementation for some usage patterns.
    static void Hash(const uint8_t* buffer, uint64_t length, uint8_t* hash, uint64_t seed=0);
    static void Hash(const char* buffer, uint64_t length, uint8_t* hash, uint64_t seed=0);

    static uint64_t Hash(const uint8_t* buffer, uint64_t length, uint64_t seed=0);
    static uint64_t Hash(const char* buffer, uint64_t length, uint64_t seed=0);
    static uint64_t HashStr(const char* buffer);

    // test vectors -- Hash(test_string, seed=0) => test_seed_0
private:
    static const uint64_t k0 = 0xD6D018F5;
    static const uint64_t k1 = 0xA2AA033B;
    static const uint64_t k2 = 0x62992FC1;
    static const uint64_t k3 = 0x30BC5B29;
    
    struct { uint64_t v[4]; } state;
    struct { uint8_t b[32]; } input;
    uint64_t bytes;
    uint64_t vseed;
};