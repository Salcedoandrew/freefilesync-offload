// *****************************************************************************
// * Offload mode for FreeFileSync - broken-file check.                         *
// *****************************************************************************

#include "broken.h"

#include <zen/file_access.h>
#include <zen/file_error.h>
#include <zen/file_io.h>
#include <zen/utf.h>

#include "afs/native.h"
#include "afs/abstract.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifndef _WIN32
#include <unistd.h> //pread
#endif

using namespace zen;
using AFS = fff::AbstractFileSystem;

namespace offload
{
namespace
{
const zen::IoCallback noProgress = [](int64_t) {};

//bytes we read from the head of every media file; the longest header below
//(PNG signature + IHDR chunk) needs 33
constexpr size_t kHeadBytes = 4096;
//a PNG ends in a 12-byte IEND chunk: length 0, "IEND", CRC AE 42 60 82
constexpr size_t kTailBytes = 12;

uint32_t readU32BE(const unsigned char* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |  static_cast<uint32_t>(p[3]);
}

uint32_t readU32LE(const unsigned char* p)
{
    return  static_cast<uint32_t>(p[0])        | (static_cast<uint32_t>(p[1]) <<  8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool checkJpeg(const unsigned char* head, size_t headLen, std::string& reason)
{
    //SOI marker FF D8, then a second marker FF xx (APP0, DQT, SOF, ...)
    if (headLen < 4)
    {
        reason = "file too small for a JPEG header";
        return true;
    }
    if (head[0] != 0xFF || head[1] != 0xD8)
    {
        reason = "missing JPEG SOI marker (does not start with FF D8)";
        return true;
    }
    if (head[2] != 0xFF || head[3] == 0x00 || head[3] == 0xFF)
    {
        reason = "no valid marker after the JPEG SOI marker";
        return true;
    }
    return false;
}

bool checkPng(const unsigned char* head, size_t headLen,
              const unsigned char* tail, size_t tailLen,
              uint64_t fileSize, std::string& reason)
{
    static const unsigned char kSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (headLen < 33) //signature + length + "IHDR" + 13 bytes of IHDR data + CRC
    {
        reason = "file too small for a PNG header";
        return true;
    }
    if (std::memcmp(head, kSignature, 8) != 0)
    {
        reason = "bad PNG signature (does not start with the 8-byte PNG magic)";
        return true;
    }
    if (readU32BE(head + 8) != 13 || std::memcmp(head + 12, "IHDR", 4) != 0)
    {
        reason = "first PNG chunk is not IHDR";
        return true;
    }
    //a PNG without IEND was cut off mid-write: the image data may be there,
    //but the file is not complete
    if (fileSize < 8 + 25 + kTailBytes)
    {
        reason = "file too small to hold a complete PNG (no room for IEND)";
        return true;
    }
    static const unsigned char kIend[12] = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
                                             0xAE, 0x42, 0x60, 0x82 };
    if (tailLen < kTailBytes || std::memcmp(tail, kIend, kTailBytes) != 0)
    {
        reason = "missing PNG IEND chunk (file was cut off)";
        return true;
    }
    return false;
}

bool checkTiff(const unsigned char* head, size_t headLen, uint64_t fileSize, std::string& reason)
{
    //TIFF header: byte order ("II" little-endian, "MM" big-endian), magic 42,
    //then the offset of the first image directory. NEF and ARW are TIFF-based,
    //so the same check judges them.
    if (headLen < 8)
    {
        reason = "file too small for a TIFF header";
        return true;
    }
    const bool littleEndian = head[0] == 'I' && head[1] == 'I';
    const bool bigEndian    = head[0] == 'M' && head[1] == 'M';
    if (!littleEndian && !bigEndian)
    {
        reason = "bad TIFF byte-order mark (not II or MM)";
        return true;
    }
    const uint16_t magic = littleEndian
        ? static_cast<uint16_t>(head[2] | (head[3] << 8))
        : static_cast<uint16_t>((head[2] << 8) | head[3]);
    if (magic != 42)
    {
        reason = "bad TIFF magic number (not 42)";
        return true;
    }
    const uint32_t ifdOffset = littleEndian ? readU32LE(head + 4) : readU32BE(head + 4);
    if (ifdOffset < 8 || ifdOffset >= fileSize)
    {
        reason = "first TIFF directory offset is outside the file";
        return true;
    }
    return false;
}

bool checkCr3(const unsigned char* head, size_t headLen, std::string& reason)
{
    //CR3 is an ISO base-media file: a 4-byte box size, "ftyp", then the brand.
    //Canon's brand is "crx " (with a trailing space).
    if (headLen < 12)
    {
        reason = "file too small for a CR3 header";
        return true;
    }
    if (std::memcmp(head + 4, "ftyp", 4) != 0)
    {
        reason = "missing CR3 ftyp box";
        return true;
    }
    if (std::memcmp(head + 8, "crx ", 4) != 0)
    {
        reason = "ftyp brand is not crx (not a Canon CR3 file)";
        return true;
    }
    return false;
}
} //namespace


bool checkMediaHeader(const std::string& extLower,
                      const void* head, size_t headLen,
                      const void* tail, size_t tailLen,
                      uint64_t fileSize, std::string& reason)
{
    const auto* h = static_cast<const unsigned char*>(head);
    const auto* t = static_cast<const unsigned char*>(tail);

    if (extLower == "jpg" || extLower == "jpeg")
        return checkJpeg(h, headLen, reason);
    if (extLower == "png")
        return checkPng(h, headLen, t, tailLen, fileSize, reason);
    if (extLower == "tif" || extLower == "tiff" || extLower == "nef" || extLower == "arw")
        return checkTiff(h, headLen, fileSize, reason);
    if (extLower == "cr3")
        return checkCr3(h, headLen, reason);
    return false; //unknown type: never call it broken
}


bool checkFileBroken(const Zstring& filePath, uint64_t fileSize, std::string& reason)
{
    //the extension decides whether this file is judged at all
    const std::string path = zen::utfTo<std::string>(filePath);
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    if (ext != "jpg" && ext != "jpeg" && ext != "png" && ext != "tif" && ext != "tiff" &&
        ext != "nef" && ext != "arw" && ext != "cr3")
        return false;

    std::vector<unsigned char> head(kHeadBytes);
    size_t headLen = 0;
    try
    {
        const auto in = AFS::getInputStream(fff::createItemPathNative(filePath)); //throw FileError
        const size_t blockSize = in->getBlockSize();
        std::vector<char> buf(blockSize);
        while (headLen < kHeadBytes)
        {
            const size_t n = in->tryRead(buf.data(), std::min(blockSize, kHeadBytes - headLen), noProgress);
            if (n == 0)
                break;
            std::memcpy(head.data() + headLen, buf.data(), n);
            headLen += n;
        }
    }
    catch (const FileError&) { return false; } //unreadable: the copy reports the real error
    catch (const std::exception&) { return false; }

    //PNG's IEND lives at the very end; the AFS stream cannot seek, so read the
    //tail straight off the file descriptor (POSIX). When that is unavailable
    //(Windows) the tail is left empty and the IEND check is skipped - the
    //signature + IHDR check still runs.
    std::vector<unsigned char> tail(kTailBytes);
    size_t tailLen = 0;
#ifndef _WIN32
    if (ext == "png" && fileSize >= kTailBytes)
    {
        try
        {
            FileInputPlain plain(filePath); //throw FileError
            const ssize_t n = ::pread(plain.getHandle(), tail.data(), kTailBytes,
                                      static_cast<off_t>(fileSize - kTailBytes));
            if (n == static_cast<ssize_t>(kTailBytes))
                tailLen = kTailBytes;
        }
        catch (const FileError&) { /*no tail: IEND check is skipped, not failed*/ }
    }
#endif

    return checkMediaHeader(ext, head.data(), headLen, tail.data(), tailLen, fileSize, reason);
}
}
