// *****************************************************************************
// * Offload mode for FreeFileSync - hash cache.                                *
// *                                                                            *
// * One cache file per destination (".ffsoffload-cache" at the destination      *
// * root) remembers the hash of every file we have verified there, keyed by    *
// * relative path + size + mtime. A later run can then trust a matching entry  *
// * instead of re-hashing the whole destination: Duplicate Detect can say      *
// * "already present, hash-confirmed" instead of trusting name+size+mtime      *
// * alone (the weak spot audit finding S7 called out). That is the cache's     *
// * whole job: it accelerates the already-present check.                        *
// *                                                                            *
// * --reverify never consults the cache: it always hashes the destination      *
// * fresh, because a file whose bytes change while size AND mtime stay put is  *
// * invisible to a size+mtime key, and --reverify is the byte-level proof. A   *
// * mismatch also drops the stale entry, so it can never be trusted again.     *
// *                                                                            *
// * The format is versioned, so a future format change never silently trusts   *
// * stale entries: a cache whose version line we do not recognise is ignored   *
// * wholesale and rebuilt. An entry is only trusted when size, mtime and       *
// * algorithm all match; anything else is a miss and the file is re-hashed.    *
// *                                                                            *
// * Known limit, stated plainly: the same size+mtime trust still bounds the    *
// * already-present check — a destination file whose bytes changed with size    *
// * and mtime unmoved still confirms from the cache. That is the same trust    *
// * Duplicate Detect already places in the date, and the price of not          *
// * re-hashing every file on every run. --reverify has no such blind spot:     *
// * it always re-hashes, so the corruption is caught there.                     *
// *****************************************************************************
#pragma once

#include "hash.h"

#include <zen/zstring.h>

#include <cstdint>
#include <ctime>
#include <map>
#include <optional>
#include <string>

namespace offload
{
//Name of the cache file at a destination root. Excluded from scans like the
//.ffs_tmp publish temps, so it is never copied as data.
extern const Zstring kCacheFileName;

class HashCache
{
public:
    //Reads the cache file if it exists. A missing file is an empty cache; a
    //corrupt or wrong-version file is ignored wholesale (never trusted, never
    //an error - it is simply rebuilt). Returns false only when the file
    //exists but cannot be read at all.
    bool load(const Zstring& filePath);

    //A hit only when path, size, mtime AND algorithm all match the entry.
    std::optional<std::string> lookup(const Zstring& relPath, uint64_t size, time_t modTime,
                                      HashAlgo algo) const;

    void store(const Zstring& relPath, uint64_t size, time_t modTime,
               HashAlgo algo, const std::string& hash);

    //Drops an entry, if present. Used when a fresh hash proves the cached
    //one wrong: the stale entry must not survive as trusted.
    void erase(const Zstring& relPath);

    bool empty() const { return entries_.empty(); }
    bool dirty() const { return dirty_; }
    size_t size() const { return entries_.size(); }

    //Writes the cache back. Publishes through a .ffs_tmp temp + rename, like
    //a copied file, so a crash cannot leave a half-written cache behind.
    //Returns the error text, empty on success.
    std::string save(const Zstring& filePath);

private:
    struct Entry
    {
        uint64_t size = 0;
        time_t modTime = 0;
        HashAlgo algo = HashAlgo::xxh64;
        std::string hash;
    };

    std::map<Zstring, Entry> entries_;
    bool dirty_ = false;
};
}
