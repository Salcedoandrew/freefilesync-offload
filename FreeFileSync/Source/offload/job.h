// *****************************************************************************
// * Offload mode for FreeFileSync - job/report data model.                     *
// *****************************************************************************
#pragma once

#include "hash.h"

#include <zen/zstring.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace offload
{
enum class VerifyMode
{
    sizeOnly,      //"Transfer": compare sizes only (fastest, weakest)
    sourceAndDest, //"Source & Destination": read back every copy and compare hashes (default)
};

std::string getVerifyModeName(VerifyMode mode);

enum class FileState
{
    copied,         //written, not yet read back
    alreadyPresent, //skipped: destination already had name+size+mtime (Duplicate Detect)
    verified,       //hash of destination == hash of source
    mismatch,       //read-back hash differs (corrupt copy, dying cable, bad drive)
    failed,         //I/O error: unreadable source, full disk, permissions
};

std::string getStateName(FileState state);
bool isFailure(FileState state);

struct FileRecord
{
    Zstring relPath; //relative to the source root, separator-normalised with '/'
    uint64_t size = 0;
    time_t modTime = 0;
    std::string hash;     //XXH64 hex, big-endian; empty when the file was not hashed
    FileState state = FileState::failed;
    std::string error;    //populated for mismatch/failed
};

struct DestReport
{
    Zstring destRoot;
    std::vector<FileRecord> files;

    size_t copied = 0;
    size_t skipped = 0;
    size_t verified = 0;
    size_t mismatched = 0;
    size_t failed = 0;
    uint64_t bytesCopied = 0; //bytes this run actually wrote
    size_t filesWritten = 0;   //files this run actually wrote (copied or verified)
};

struct JobReport
{
    Zstring sourceRoot;
    std::vector<DestReport> dests;

    std::string verifyMode;
    HashAlgo hashAlgo = HashAlgo::xxh64;
    std::string toolVersion;
    time_t startedAt = 0;
    time_t finishedAt = 0;
    bool cancelled = false;
    //Job-level failure: the source could not be walked, so no destination ran.
    //Empty when the scan succeeded; a job with an error here never verifies.
    std::string error;
    bool dryRun = false;
    bool reverifyOnly = false;
};

//recompute the counters of one destination from its file records
void tally(DestReport& dest);

//true only if every destination accounted for every file and nothing failed
bool allVerified(const JobReport& job);

size_t totalFiles(const JobReport& job);
size_t totalSucceeded(const JobReport& job);
}
