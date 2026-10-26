// *****************************************************************************
// * Offload mode for FreeFileSync - engine.                                   *
// *                                                                            *
// * Copies a source tree to N destinations in parallel, hashing every byte on  *
// * the way through the same AbstractFileSystem layer FreeFileSync's sync      *
// * engine uses. The source is opened read-only and is never written, renamed  *
// * or deleted.                                                               *
// *****************************************************************************
#pragma once

#include "hash.h"
#include "job.h"

#include <atomic>
#include <functional>

namespace offload
{
//Live counters for one source -> destination transfer. Emitted from the worker
//thread that owns that destination; the caller must not assume any ordering
//beyond "filesDone never decreases".
struct Progress
{
    enum class Phase
    {
        scanning,  //walking the source tree
        copying,   //writing bytes to the destination
        verifying, //reading the copy back and hashing it
        done,
    };

    Phase    phase     = Phase::scanning;
    uint64_t bytesDone = 0; //bytes written (copying) or hashed (verifying)
    uint64_t bytesTotal = 0; //planned work for this transfer
    size_t   filesDone = 0;
    size_t   filesTotal = 0;

    Zstring  currentFile;
    uint64_t currentFileSize = 0;
    uint64_t currentFileDone = 0;
};

//Throttled to about 10 Hz by the engine, plus one forced call per file boundary.
using ProgressCallback = std::function<void(const Progress&)>;

struct Options
{
    Zstring sourceRoot;
    std::vector<Zstring> destRoots;

    VerifyMode verify = VerifyMode::sourceAndDest;

    //digest algorithm used for verification and for the report/MHL
    HashAlgo hashAlgo = HashAlgo::xxh64;

    //hash what is already on the destinations and compare, copying nothing
    bool reverifyOnly = false;

    //scan and report what would happen, touch nothing
    bool dryRun = false;

    //The Skip field's patterns, as typed: `*.LRV | MISC/` (see filter.h). Empty
    //means copy everything the junk rules allow. Parsed once per job, and the
    //same walk that plans the copy applies it, so the fit check and the transfer
    //cannot disagree about how many files are coming.
    std::string exclude;

    //limit parallelism when a slow destination must not be throttled by a fast one
    size_t maxDestinations = 8;

    //called from worker threads, once per finished file
    std::function<void(const Zstring& destRoot, const FileRecord&)> onFileDone;

    //live progress for the UI; may be called a few thousand times per job
    ProgressCallback onProgress;

    //minimum gap between two onProgress calls, in milliseconds
    size_t progressIntervalMs = 80;

    //set to true to stop after the file currently in flight
    std::atomic<bool>* cancel = nullptr;

    //while true, workers park between blocks; the file in flight stays intact and
    //resumes from the next block, so a pause never re-copies a partial file
    std::atomic<bool>* pause = nullptr;
};

//Runs the whole job. Failures never escape as exceptions: they land in the report,
//one FileRecord at a time.
JobReport runJob(const Options& opts);

//Total bytes and file count a transfer from this source would carry, counted with
//exactly the same walk, junk rules and Skip patterns runJob uses. This is what the
//window's fit check compares against free space, so "what will be copied" has one
//owner. Returns false with err set when the source cannot be walked.
bool measureSource(const Zstring& sourceRoot, const std::string& excludeText,
                   uint64_t& bytesOut, size_t& filesOut, std::string& err);
}
