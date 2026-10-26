// *****************************************************************************
// * Offload mode for FreeFileSync - XXH64 streaming implementation.            *
// * Algorithm: XXH64 (Yann Collet, public domain). Verified against xxhsum.    *
// *****************************************************************************

#include "hash.h"

#include <cstring>

namespace offload
{
namespace
{
constexpr uint64_t P1 = 11400714785074694791ULL;
constexpr uint64_t P2 = 14029467366897019727ULL;
constexpr uint64_t P3 = 1609587929392839161ULL;
constexpr uint64_t P4 = 9650029242287828579ULL;
constexpr uint64_t P5 = 2870177450012600261ULL;

inline uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

inline uint64_t read64le(const unsigned char* p)
{
    uint64_t v;
    std::memcpy(&v, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    return v;
}

inline uint32_t read32le(const unsigned char* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

inline uint64_t round64(uint64_t acc, uint64_t input)
{
    acc += input * P2;
    acc = rotl(acc, 31);
    acc *= P1;
    return acc;
}

inline uint64_t mergeRound(uint64_t acc, uint64_t val)
{
    acc ^= round64(0, val);
    acc = acc * P1 + P4;
    return acc;
}

inline uint64_t avalanche(uint64_t h)
{
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}
} //namespace


Xxh64::Xxh64() { reset(); }

void Xxh64::reset()
{
    v1_ = P1 + P2; //seed is 0
    v2_ = P2;
    v3_ = 0;
    v4_ = 0 - P1;
    bufSize_ = 0;
    total_ = 0;
}

void Xxh64::update(const void* dataIn, size_t len)
{
    const unsigned char* p    = static_cast<const unsigned char*>(dataIn);
    const unsigned char* pEnd = p + len;
    total_ += len;

    if (bufSize_ + len < 32)
    {
        std::memcpy(buf_ + bufSize_, p, len);
        bufSize_ += len;
        return;
    }

    if (bufSize_ > 0)
    {
        const size_t fill = 32 - bufSize_;
        std::memcpy(buf_ + bufSize_, p, fill);
        v1_ = round64(v1_, read64le(buf_ + 0));
        v2_ = round64(v2_, read64le(buf_ + 8));
        v3_ = round64(v3_, read64le(buf_ + 16));
        v4_ = round64(v4_, read64le(buf_ + 24));
        p += fill;
        bufSize_ = 0;
    }

    while (pEnd - p >= 32)
    {
        v1_ = round64(v1_, read64le(p)); p += 8;
        v2_ = round64(v2_, read64le(p)); p += 8;
        v3_ = round64(v3_, read64le(p)); p += 8;
        v4_ = round64(v4_, read64le(p)); p += 8;
    }

    if (p < pEnd)
    {
        bufSize_ = static_cast<size_t>(pEnd - p);
        std::memcpy(buf_, p, bufSize_);
    }
}

uint64_t Xxh64::digest() const
{
    uint64_t h;
    if (total_ >= 32)
    {
        h = rotl(v1_, 1) + rotl(v2_, 7) + rotl(v3_, 12) + rotl(v4_, 18);
        h = mergeRound(h, v1_);
        h = mergeRound(h, v2_);
        h = mergeRound(h, v3_);
        h = mergeRound(h, v4_);
    }
    else
        h = 0 + P5; //seed + P5

    h += total_;

    const unsigned char* p    = buf_;
    const unsigned char* pEnd = buf_ + bufSize_;

    while (pEnd - p >= 8)
    {
        h ^= round64(0, read64le(p));
        h = rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if (pEnd - p >= 4)
    {
        h ^= static_cast<uint64_t>(read32le(p)) * P1;
        h = rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < pEnd)
    {
        h ^= static_cast<uint64_t>(*p) * P5;
        h = rotl(h, 11) * P1;
        ++p;
    }
    return avalanche(h);
}


std::string getAlgoName(HashAlgo algo)
{
    switch (algo)
    {
        case HashAlgo::xxh64: return "XXH64";
        case HashAlgo::md5:   return "MD5";
        case HashAlgo::sha1:  return "SHA1";
    }
    return "XXH64";
}


std::string getHashTag(HashAlgo algo)
{
    switch (algo)
    {
        case HashAlgo::xxh64: return "xxh64";
        case HashAlgo::md5:   return "md5";
        case HashAlgo::sha1:  return "sha1";
    }
    return "xxh64";
}


bool parseHashAlgo(const std::string& name, HashAlgo& out)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);

    if (lower == "xxh64" || lower == "xxhash64") { out = HashAlgo::xxh64; return true; }
    if (lower == "md5")                            { out = HashAlgo::md5;   return true; }
    if (lower == "sha1" || lower == "sha-1")      { out = HashAlgo::sha1;  return true; }
    return false;
}


// --------------------------------------------------------------------------
// MD5 (RFC 1321)
// --------------------------------------------------------------------------
namespace
{
const uint32_t md5K[64] =
{
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
const int md5S[64] =
{
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

//message-word index per step: (i), (5i+1)%16, (3i+5)%16, (7i)%16. Spelling the table
//out keeps the modulo out of the inner loop, which is worth ~3x on scalar MD5.
const int md5G[64] =
{
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     1,  6, 11,  0,  5, 10, 15,  4,  9, 14,  3,  8, 13,  2,  7, 12,
     5,  8, 11, 14,  1,  4,  7, 10, 13,  0,  3,  6,  9, 12, 15,  2,
     0,  7, 14,  5, 12,  3, 10,  1,  8, 15,  6, 13,  4, 11,  2,  9,
};
inline uint32_t rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }
}


void Md5::transform(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d,
                    const unsigned char* block)
{
    uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = static_cast<uint32_t>(block[i * 4])            |
              (static_cast<uint32_t>(block[i * 4 + 1]) << 8)  |
              (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
              (static_cast<uint32_t>(block[i * 4 + 3]) << 24);

    uint32_t aa = a, bb = b, cc = c, dd = d;

    //four independent round groups: no function selection inside the loop
#define MD5_STEP(i, F)                                   \
    {                                                    \
        const uint32_t f   = (F); /* must see the pre-rotation b,c,d */ \
        const uint32_t tmp = dd;                         \
        dd = cc;                                         \
        cc = bb;                                         \
        bb = bb + rotl32(aa + f + md5K[i] + m[md5G[i]], md5S[i]); \
        aa = tmp;                                        \
    }

    for (int i = 0; i < 16; ++i)  MD5_STEP(i, (bb & cc) | (~bb & dd))
    for (int i = 16; i < 32; ++i) MD5_STEP(i, (dd & bb) | (~dd & cc))
    for (int i = 32; i < 48; ++i) MD5_STEP(i, bb ^ cc ^ dd)
    for (int i = 48; i < 64; ++i) MD5_STEP(i, cc ^ (bb | ~dd))

#undef MD5_STEP
    a += aa; b += bb; c += cc; d += dd;
}


Md5::Md5() { reset(); }


void Md5::reset()
{
    s0_ = 0x67452301; s1_ = 0xefcdab89; s2_ = 0x98badcfe; s3_ = 0x10325476;
    bufSize_ = 0;
    total_   = 0;
}


void Md5::update(const void* data, size_t len)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    total_ += len;

    while (len > 0)
    {
        const size_t room = 64 - bufSize_;
        const size_t n    = len < room ? len : room;
        std::memcpy(buf_ + bufSize_, p, n);
        bufSize_ += n;
        p        += n;
        len      -= n;
        if (bufSize_ == 64)
        {
            transform(s0_, s1_, s2_, s3_, buf_);
            bufSize_ = 0;
        }
    }
}


std::string Md5::hex() const
{
    uint32_t a = s0_, b = s1_, c = s2_, d = s3_;
    unsigned char tail[128];
    std::memcpy(tail, buf_, bufSize_);

    size_t n = bufSize_;
    tail[n++] = 0x80;
    if (n > 56)
    {
        while (n < 64) tail[n++] = 0;
        transform(a, b, c, d, tail);
        n = 0;
    }
    while (n < 56) tail[n++] = 0;

    const uint64_t bits = total_ * 8; //length in bits, little-endian for MD5
    for (int i = 0; i < 8; ++i)
        tail[56 + i] = static_cast<unsigned char>((bits >> (8 * i)) & 0xFF);
    transform(a, b, c, d, tail);

    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (uint32_t word : { a, b, c, d })
        for (int i = 0; i < 4; ++i) //little-endian word order
        {
            const unsigned char byte = static_cast<unsigned char>((word >> (8 * i)) & 0xFF);
            out += digits[byte >> 4];
            out += digits[byte & 0xF];
        }
    return out;
}


// --------------------------------------------------------------------------
// SHA-1 (FIPS 180-1)
// --------------------------------------------------------------------------
Sha1::Sha1() { reset(); }


void Sha1::reset()
{
    state_[0] = 0x67452301; state_[1] = 0xEFCDAB89; state_[2] = 0x98BADCFE;
    state_[3] = 0x10325476; state_[4] = 0xC3D2E1F0;
    bufSize_ = 0;
    total_   = 0;
}


void Sha1::transform(uint32_t* state, const unsigned char* block)
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                static_cast<uint32_t>(block[i * 4 + 3]);
    for (int i = 16; i < 80; ++i)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    //four round groups of 20: the round constant and the f() selection are hoisted
#define SHA1_STEP(i, F, K)                                          \
    {                                                               \
        const uint32_t tmp = rotl32(a, 5) + (F) + e + (K) + w[i];   \
        e = d;                                                      \
        d = c;                                                      \
        c = rotl32(b, 30);                                          \
        b = a;                                                      \
        a = tmp;                                                    \
    }

    for (int i = 0;  i < 20; ++i) SHA1_STEP(i, (b & c) | (~b & d),         0x5A827999u)
    for (int i = 20; i < 40; ++i) SHA1_STEP(i, b ^ c ^ d,                  0x6ED9EBA1u)
    for (int i = 40; i < 60; ++i) SHA1_STEP(i, (b & c) | (b & d) | (c & d), 0x8F1BBCDCu)
    for (int i = 60; i < 80; ++i) SHA1_STEP(i, b ^ c ^ d,                  0xCA62C1D6u)

#undef SHA1_STEP
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}


void Sha1::update(const void* data, size_t len)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    total_ += len;

    while (len > 0)
    {
        const size_t room = 64 - bufSize_;
        const size_t n    = len < room ? len : room;
        std::memcpy(buf_ + bufSize_, p, n);
        bufSize_ += n;
        p        += n;
        len      -= n;
        if (bufSize_ == 64)
        {
            transform(state_, buf_);
            bufSize_ = 0;
        }
    }
}


std::string Sha1::hex() const
{
    uint32_t state[5];
    std::memcpy(state, state_, sizeof state);

    unsigned char tail[128];
    std::memcpy(tail, buf_, bufSize_);

    size_t n = bufSize_;
    tail[n++] = 0x80;
    if (n > 56)
    {
        while (n < 64) tail[n++] = 0;
        transform(state, tail);
        n = 0;
    }
    while (n < 56) tail[n++] = 0;

    const uint64_t bits = total_ * 8; //big-endian length for SHA-1
    for (int i = 0; i < 8; ++i)
        tail[56 + i] = static_cast<unsigned char>((bits >> (56 - 8 * i)) & 0xFF);
    transform(state, tail);

    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (int i = 0; i < 5; ++i)
        for (int s = 28; s >= 0; s -= 4)
            out += digits[(state[i] >> s) & 0xF];
    return out;
}


// --------------------------------------------------------------------------
// Hasher facade
// --------------------------------------------------------------------------
void Hasher::update(const void* data, size_t len)
{
    switch (algo_)
    {
        case HashAlgo::xxh64: xx_.update (data, len); break;
        case HashAlgo::md5:   md5_.update(data, len); break;
        case HashAlgo::sha1:  sha1_.update(data, len); break;
    }
}


std::string Hasher::hex() const
{
    switch (algo_)
    {
        case HashAlgo::xxh64: return toHexBE(xx_.digest());
        case HashAlgo::md5:   return md5_.hex();
        case HashAlgo::sha1:  return sha1_.hex();
    }
    return std::string();
}


std::string toHexBE(uint64_t value)
{
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) //most significant nibble first
    {
        out[static_cast<size_t>(15 - i)] = digits[(value >> (i * 4)) & 0xF];
    }
    return out;
}

std::string toHexLE(uint64_t value)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (int i = 0; i < 8; ++i) //byte 0 first, each byte low nibble first
    {
        const unsigned char b = static_cast<unsigned char>((value >> (i * 8)) & 0xFF);
        out += digits[b & 0xF];
        out += digits[b >> 4];
    }
    return out;
}
}
