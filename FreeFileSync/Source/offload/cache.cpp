// *****************************************************************************
// * Offload mode for FreeFileSync - hash cache.                                *
// *****************************************************************************

#include "cache.h"

#include <zen/file_access.h>
#include <zen/file_error.h>
#include <zen/file_io.h>
#include <zen/utf.h>

#include <sstream>
#include <vector>

using namespace zen;

namespace offload
{
const Zstring kCacheFileName = Zstr(".ffsoffload-cache");

//File format, one entry per line:
//    ffsoffload-cache 1
//    <escaped rel path>\t<size>\t<mtime>\t<algo>\t<hash>
//Paths are escaped so a tab, newline or backslash in a file name cannot
//break the line shape: \ -> \\, tab -> \t, LF -> \n, CR -> \r.
constexpr const char* kCacheVersionLine = "ffsoffload-cache 1";

namespace
{
std::string escapePath(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

//Returns false on a bad escape (a lone trailing backslash, an unknown code).
bool unescapePath(const std::string& s, std::string& out)
{
    out.clear();
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '\\')
        {
            out += s[i];
            continue;
        }
        if (i + 1 >= s.size())
            return false;
        switch (s[++i])
        {
            case '\\': out += '\\'; break;
            case 't':  out += '\t'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            default:   return false;
        }
    }
    return true;
}

bool parseU64(const std::string& s, uint64_t& out)
{
    if (s.empty())
        return false;
    uint64_t v = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    out = v;
    return true;
}

bool parseI64(const std::string& s, int64_t& out)
{
    if (s.empty())
        return false;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-')
    {
        negative = true;
        i = 1;
        if (s.size() == 1)
            return false;
    }
    uint64_t v = 0;
    for (; i < s.size(); ++i)
    {
        if (s[i] < '0' || s[i] > '9')
            return false;
        v = v * 10 + static_cast<uint64_t>(s[i] - '0');
    }
    out = negative ? -static_cast<int64_t>(v) : static_cast<int64_t>(v);
    return true;
}
} //namespace


bool HashCache::load(const Zstring& filePath)
{
    entries_.clear();
    dirty_ = false;

    std::string content;
    try
    {
        content = getFileContent(filePath, [](int64_t) {}); //throw FileError
    }
    catch (const FileError&)
    {
        return false; //missing or unreadable: an empty cache, rebuilt later
    }

    std::istringstream in(content);
    std::string line;
    if (!std::getline(in, line) || line != kCacheVersionLine)
        return true; //wrong version (or empty/garbage): ignore wholesale, rebuild

    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        //five tab-separated fields; the path itself cannot hold a raw tab
        //(it is escaped), so the field count is exact
        std::vector<std::string> parts;
        size_t pos = 0;
        for (;;)
        {
            const size_t tab = line.find('\t', pos);
            if (tab == std::string::npos)
            {
                parts.push_back(line.substr(pos));
                break;
            }
            parts.push_back(line.substr(pos, tab - pos));
            pos = tab + 1;
        }
        if (parts.size() != 5)
            continue; //one bad line never poisons the rest

        std::string path;
        if (!unescapePath(parts[0], path))
            continue;

        Entry e;
        HashAlgo algo = HashAlgo::xxh64;
        int64_t mtime = 0;
        if (!parseU64(parts[1], e.size) || !parseI64(parts[2], mtime) ||
            !parseHashAlgo(parts[3], algo) || parts[4].empty())
            continue;
        e.modTime = static_cast<time_t>(mtime);
        e.algo = algo;
        e.hash = parts[4];
        entries_[Zstring(path.c_str())] = std::move(e);
    }
    return true;
}

std::optional<std::string> HashCache::lookup(const Zstring& relPath, uint64_t size, time_t modTime,
                                             HashAlgo algo) const
{
    const auto it = entries_.find(relPath);
    if (it == entries_.end())
        return std::nullopt;
    const Entry& e = it->second;
    if (e.size != size || e.modTime != modTime || e.algo != algo)
        return std::nullopt; //size or mtime moved: the entry is stale, re-hash
    return e.hash;
}

void HashCache::store(const Zstring& relPath, uint64_t size, time_t modTime,
                      HashAlgo algo, const std::string& hash)
{
    const auto it = entries_.find(relPath);
    if (it != entries_.end() && it->second.size == size && it->second.modTime == modTime &&
        it->second.algo == algo && it->second.hash == hash)
        return; //already there, unchanged
    entries_[relPath] = { size, modTime, algo, hash };
    dirty_ = true;
}

void HashCache::erase(const Zstring& relPath)
{
    if (entries_.erase(relPath) > 0)
        dirty_ = true; //the removal must reach the file on save
}

std::string HashCache::save(const Zstring& filePath)
{
    std::ostringstream out;
    out << kCacheVersionLine << '\n';
    for (const auto& [relPath, e] : entries_)
        out << escapePath(zen::utfTo<std::string>(relPath)) << '\t'
            << e.size << '\t'
            << static_cast<long long>(e.modTime) << '\t'
            << getHashTag(e.algo) << '\t'
            << e.hash << '\n';

    //publish through a temp + rename, like a copied file: a crash mid-write
    //leaves the old cache intact instead of a half-written one
    const Zstring tmpPath = filePath + Zstr(".ffs_tmp");
    try
    {
        setFileContent(tmpPath, out.str(), [](int64_t) {}); //throw FileError
        moveAndRenameItem(tmpPath, filePath, true);         //throw FileError
    }
    catch (const FileError& e)
    {
        try { removeFilePlain(tmpPath); } catch (const FileError&) {} //no orphan temp
        return zen::utfTo<std::string>(e.toString());
    }
    dirty_ = false;
    return std::string();
}
}
