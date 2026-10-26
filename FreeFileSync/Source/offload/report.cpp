// *****************************************************************************
// * Offload mode for FreeFileSync - report writers.                            *
// *****************************************************************************
#include "report.h"
#include "hash.h"

#include <zen/file_error.h>
#include <zen/file_io.h>
#include <zen/sys_info.h>
#include <zen/utf.h>

#include <cstdio>
#include <sstream>

using namespace zen;

namespace offload
{
namespace
{
const zen::IoCallback noProgress = [](int64_t) {};

std::string csvEscape(const std::string& s)
{
    if (s.find_first_of(",\"\n\r") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + '"';
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[7];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
        }
    }
    return out;
}

std::string xmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string writeRaw(const Zstring& path, const std::string& content)
{
    try
    {
        setFileContent(path, content, noProgress); //throw FileError
        return std::string();
    }
    catch (const FileError& e) { return zen::utfTo<std::string>(e.toString()); }
}
} //namespace


std::string toIso8601(time_t t)
{
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string humanBytes(uint64_t bytes)
{
    const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4)
    {
        v /= 1024.0;
        ++u;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}


std::string writeCsv(const JobReport& job, const Zstring& filePath)
{
    std::ostringstream out;
    out << "destination,relative_path,size_bytes,modified_epoch,hash_"
        << getHashTag(job.hashAlgo) << ",state,error\n";
    for (const DestReport& d : job.dests)
        for (const FileRecord& f : d.files)
        {
            out << csvEscape(zen::utfTo<std::string>(d.destRoot)) << ','
                << csvEscape(zen::utfTo<std::string>(f.relPath)) << ','
                << f.size << ','
                << static_cast<long long>(f.modTime) << ','
                << f.hash << ','
                << getStateName(f.state) << ','
                << csvEscape(f.error) << '\n';
        }
    return writeRaw(filePath, out.str());
}


std::string writeJson(const JobReport& job, const Zstring& filePath)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"tool\": \"ffsoffload " << job.toolVersion << "\",\n";
    out << "  \"source\": \"" << jsonEscape(zen::utfTo<std::string>(job.sourceRoot)) << "\",\n";
    out << "  \"verification\": \"" << jsonEscape(job.verifyMode) << "\",\n";
    out << "  \"hash_algorithm\": \"" << getAlgoName(job.hashAlgo) << "\",\n";
    out << "  \"started_at\": \"" << toIso8601(job.startedAt) << "\",\n";
    out << "  \"finished_at\": \"" << toIso8601(job.finishedAt) << "\",\n";
    out << "  \"cancelled\": " << (job.cancelled ? "true" : "false") << ",\n";
    out << "  \"dry_run\": " << (job.dryRun ? "true" : "false") << ",\n";
    out << "  \"reverify_only\": " << (job.reverifyOnly ? "true" : "false") << ",\n";
    out << "  \"destinations\": [\n";

    for (size_t i = 0; i < job.dests.size(); ++i)
    {
        const DestReport& d = job.dests[i];
        out << "    {\n";
        out << "      \"path\": \"" << jsonEscape(zen::utfTo<std::string>(d.destRoot)) << "\",\n";
        out << "      \"bytes_copied\": " << d.bytesCopied << ",\n";
        out << "      \"counts\": { \"copied\": " << d.copied << ", \"already_present\": " << d.skipped
            << ", \"verified\": " << d.verified << ", \"mismatch\": " << d.mismatched
            << ", \"failed\": " << d.failed << " },\n";
        out << "      \"files\": [\n";
        for (size_t k = 0; k < d.files.size(); ++k)
        {
            const FileRecord& f = d.files[k];
            out << "        { \"path\": \"" << jsonEscape(zen::utfTo<std::string>(f.relPath)) << "\""
                << ", \"size\": " << f.size
                << ", \"modified\": " << static_cast<long long>(f.modTime)
                << ", \"hash\": \"" << f.hash << "\""
                << ", \"state\": \"" << getStateName(f.state) << "\"";
            if (!f.error.empty())
                out << ", \"error\": \"" << jsonEscape(f.error) << "\"";
            out << " }" << (k + 1 < d.files.size() ? "," : "") << "\n";
        }
        out << "      ]\n";
        out << "    }" << (i + 1 < job.dests.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return writeRaw(filePath, out.str());
}


std::string writeMhl(const JobReport& job, const DestReport& dest, const Zstring& filePath)
{
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<hashlist version=\"1.1\">\n";
    out << "  <creatorinfo>\n";
    out << "    <creationdate>" << toIso8601(job.finishedAt) << "</creationdate>\n";
    out << "    <hostname>" << xmlEscape(zen::utfTo<std::string>(zen::getHostName())) << "</hostname>\n";
    out << "    <tool>ffsoffload</tool>\n";
    out << "    <version>" << job.toolVersion << "</version>\n";
    out << "    <source>" << xmlEscape(zen::utfTo<std::string>(job.sourceRoot)) << "</source>\n";
    out << "    <verification>" << xmlEscape(job.verifyMode) << "</verification>\n";
    out << "  </creatorinfo>\n";
    out << "  <hashlist>\n";
    out << "    <hashes>\n";
    for (const FileRecord& f : dest.files)
    {
        if (f.hash.empty())
            continue; //skipped files have no digest of ours; they are listed in the CSV/JSON log
        out << "      <hash>\n";
        out << "        <file>" << xmlEscape(zen::utfTo<std::string>(f.relPath)) << "</file>\n";
        out << "        <size>" << f.size << "</size>\n";
        const std::string tag = getHashTag(job.hashAlgo);
        out << "        <" << tag << ">" << f.hash << "</" << tag << ">\n";
        out << "      </hash>\n";
    }
    out << "    </hashes>\n";
    out << "  </hashlist>\n";
    out << "</hashlist>\n";
    return writeRaw(filePath, out.str());
}
}
