// *****************************************************************************
// * Offload mode for FreeFileSync - hashing.                                   *
// * FreeFileSync itself has no hash support at all (its "verify copied files"  *
// * is a live byte comparison), so the hashers live here.                     *
// * XXH64 is implemented locally and verified against xxhsum; MD5 and SHA-1   *
// * are implemented locally and verified against the platform shasum/md5.     *
// *****************************************************************************
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace offload
{
enum class HashAlgo
{
    xxh64, //default: XXH64 with seed 0, digest printed big-endian (Hedge/Offshoot convention)
    md5,   //legacy, for clients that insist on it
    sha1,  //legacy
};

std::string getAlgoName(HashAlgo algo);  //"XXH64", "MD5", "SHA1" - shown to people
std::string getHashTag  (HashAlgo algo); //"xxh64", "md5", "sha1"   - MHL <hash> child element

// Parses "xxh64"/"md5"/"sha1" (case-insensitive). Returns false on anything else.
bool parseHashAlgo(const std::string& name, HashAlgo& out);

// Streaming XXH64 (seed 0). Byte-for-byte identical to `xxhsum -H1`.
class Xxh64
{
public:
    Xxh64();

    void update(const void* data, size_t len);
    uint64_t digest() const; //does not disturb the streaming state
    void reset();

    uint64_t bytesHashed() const { return total_; }

private:
    uint64_t v1_;
    uint64_t v2_;
    uint64_t v3_;
    uint64_t v4_;
    unsigned char buf_[32];
    size_t bufSize_;
    uint64_t total_;
};

//Streaming MD5 (RFC 1321). Byte-for-byte identical to `md5 -q`.
class Md5
{
public:
    Md5();
    void update(const void* data, size_t len);
    void reset();
    std::string hex() const; //lowercase, does not disturb the streaming state

private:
    static void transform(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d,
                          const unsigned char* block);

    uint32_t s0_, s1_, s2_, s3_;
    unsigned char buf_[64];
    size_t bufSize_;
    uint64_t total_;
};

//Streaming SHA-1 (FIPS 180-1). Byte-for-byte identical to `shasum -a 1`.
class Sha1
{
public:
    Sha1();
    void update(const void* data, size_t len);
    void reset();
    std::string hex() const;

private:
    static void transform(uint32_t* state, const unsigned char* block);

    uint32_t state_[5];
    unsigned char buf_[64];
    size_t bufSize_;
    uint64_t total_;
};

//One hasher the rest of the engine can hold without caring which algorithm is
//selected. Only the active algorithm is updated.
class Hasher
{
public:
    explicit Hasher(HashAlgo algo) : algo_(algo) {}

    void update(const void* data, size_t len);
    std::string hex() const;

    HashAlgo algo() const { return algo_; }

private:
    HashAlgo algo_;
    Xxh64 xx_;
    Md5   md5_;
    Sha1  sha1_;
};

std::string toHexBE(uint64_t value); //byte order used by MHL/Offshoot
std::string toHexLE(uint64_t value);
}
