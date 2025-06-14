#pragma once

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using i8 = char;
using i16 = short;
using i32 = int;
using i64 = long long;

using f32 = float;
using f64 = double;

#define TM_STATIC_HASH(s, v) (sizeof("" s "") ? v : v)

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define PAD(n) char CONCAT(_padding_, __LINE__)[n]