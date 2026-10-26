// *****************************************************************************
// * Offload mode - C ABI over the C++ engine.                                  *
// *                                                                            *
// * One OffloadJob == one source -> destination transfer, running on its own   *
// * thread. The UI polls a snapshot at whatever rate it likes and drives pause *
// * and cancel through atomics; no C++ type crosses this boundary.             *
// *****************************************************************************

#include "capi.h"
#include "engine.h"
#include "report.h"

#include <zen/file_access.h>
#include <zen/file_error.h>
#include <zen/file_path.h>

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

using namespace zen;

namespace
{
const char* const kEngineVersion = "0.6.0";

std::string nowStamp()
{
    const std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&t, &tmBuf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmBuf);
    return buf;
}

offload::HashAlgo toHashAlgo(int32_t v)
{
    switch (v)
    {
        case OFFLOAD_HASH_MD5:  return offload::HashAlgo::md5;
        case OFFLOAD_HASH_SHA1: return offload::HashAlgo::sha1;
    }
    return offload::HashAlgo::xxh64;
}
} //namespace


struct OffloadJob
{
    offload::Options opts;
    std::atomic<bool> pauseFlag{ false };
    std::atomic<bool> cancelFlag{ false };

    std::thread worker;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    //everything below is guarded by mu
    std::mutex mu;
    offload::Progress prog;
    offload::JobReport report;
    std::string currentFile;
    std::string errorText;
    std::string reportCsv;
    bool haveReport = false;
    bool finished = false;
    uint64_t bytesWritten = 0;
    uint64_t verifiedCount = 0;
    uint64_t skippedCount = 0;
    uint64_t failedCount = 0;
    uint64_t brokenCount = 0;
    uint64_t fileCount = 0;

    void publishProgress(const offload::Progress& p)
    {
        std::lock_guard<std::mutex> lk(mu);
        prog = p;
        currentFile = zen::utfTo<std::string>(p.currentFile);
    }
};


extern "C" const char* offload_engine_version(void)
{
    return kEngineVersion;
}


namespace
{
//per-thread, so two panes measuring at the same time cannot overwrite each other
thread_local std::string g_measureError;

//The string getters at the bottom of this file hand a `const char*` to Swift, which
//reads it after we have already unlocked. The member it points into is reassigned by
//the worker thread on nearly every progress tick, so returning its c_str() handed out
//a pointer that could be freed mid-read. Copy into a per-thread buffer instead: the
//pointer stays valid until this same thread asks for another string.
thread_local std::string g_returnBuffer;

const char* copyOut(const std::string& s)
{
    g_returnBuffer = s;
    return g_returnBuffer.c_str();
}
}


extern "C" int32_t offload_source_measure(const char* source, uint64_t* outBytes, uint64_t* outFiles)
{
    g_measureError.clear();
    if (!source || !*source)
    {
        g_measureError = "no source path";
        return 0;
    }

    uint64_t bytes = 0;
    size_t   files = 0;
    std::string err;
    if (!offload::measureSource(Zstring(source), bytes, files, err))
    {
        g_measureError = err.empty() ? "cannot read the source folder" : err;
        return 0;
    }

    if (outBytes) *outBytes = bytes;
    if (outFiles) *outFiles = static_cast<uint64_t>(files);
    return 1;
}


extern "C" const char* offload_measure_error(void)
{
    return g_measureError.c_str();
}


extern "C" OffloadJob* offload_job_start(const char* source, const char* dest, const char* reportDir,
                                         int32_t hashAlgo, int32_t verifyMode, int32_t dryRun)
{
    if (!source || !*source || !dest || !*dest)
        return nullptr;

    const Zstring sourcePath(source);
    const Zstring destPath(dest);

    //Fail here, with a message the UI can show, rather than inside the worker.
    {
        std::optional<ItemType> type;
        try { type = getItemTypeIfExists(sourcePath); }
        catch (const FileError& e)
        {
            auto* job = new OffloadJob;
            job->errorText = "cannot read the source: " + zen::utfTo<std::string>(e.toString());
            job->finished = true;
            return job;
        }
        if (!type)
        {
            auto* job = new OffloadJob;
            job->errorText = "the source folder is gone";
            job->finished = true;
            return job;
        }
        if (*type != ItemType::folder)
        {
            auto* job = new OffloadJob;
            job->errorText = "the source must be a folder, not a single file";
            job->finished = true;
            return job;
        }
    }

    auto* job = new OffloadJob;
    job->opts.sourceRoot   = sourcePath;
    job->opts.destRoots    = { destPath };
    job->opts.hashAlgo     = toHashAlgo(hashAlgo);
    job->opts.verify       = verifyMode == OFFLOAD_VERIFY_SIZE ? offload::VerifyMode::sizeOnly
                                                              : offload::VerifyMode::sourceAndDest;
    job->opts.dryRun       = dryRun != 0;
    job->opts.pause        = &job->pauseFlag;
    job->opts.cancel       = &job->cancelFlag;
    job->opts.onProgress   = [job](const offload::Progress& p) { job->publishProgress(p); };

    const std::string reportDirStr = reportDir ? reportDir : "";
    job->started = std::chrono::steady_clock::now();

    job->worker = std::thread([job, reportDirStr]()
    {
        offload::JobReport r;
        std::string error;
        try
        {
            r = offload::runJob(job->opts);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        catch (...)
        {
            error = "unknown engine failure";
        }
        //A scan failure is the primary failure: it outranks any report error,
        //and without it offload_job_error() would stay empty while ok is 0.
        if (error.empty())
            error = r.error;

        //Reports are part of the deliverable, not an optional extra: the MHL goes
        //to the destination (archive-interoperable) and CSV/JSON to the report dir.
        std::string csvPath;
        if (!reportDirStr.empty() && !r.dests.empty())
        {
            try { createDirectoryIfMissingRecursion(Zstring(reportDirStr.c_str())); }
            catch (const FileError&) {}

            const Zstring dir(reportDirStr.c_str());
            const std::string e1 = offload::writeCsv (r, appendSeparator(dir) + Zstr("report.csv"));
            const std::string e2 = offload::writeJson(r, appendSeparator(dir) + Zstr("report.json"));
            if (!e1.empty() && error.empty()) error = "csv: " + e1;
            if (!e2.empty() && error.empty()) error = "json: " + e2;
            csvPath = zen::utfTo<std::string>(appendSeparator(dir) + Zstr("report.csv"));
        }
        for (const offload::DestReport& d : r.dests)
            if (!d.destRoot.empty())
                if (const std::string e = offload::writeMhl(r, d, appendSeparator(d.destRoot) + Zstr(".ffsoffload.mhl"));
                    !e.empty() && error.empty())
                    error = "mhl: " + e;

        std::lock_guard<std::mutex> lk(job->mu);
        job->report      = std::move(r);
        job->haveReport  = true;
        job->finished    = true;
        job->reportCsv   = csvPath;
        if (!error.empty())
            job->errorText = error;

        for (const offload::DestReport& d : job->report.dests)
        {
            job->bytesWritten  += d.bytesCopied;
            job->verifiedCount += d.verified;
            job->skippedCount  += d.skipped;
            job->failedCount   += d.failed + d.mismatched;
            job->brokenCount   += d.broken;
            job->fileCount     += d.files.size();
        }
    });

    return job;
}


extern "C" void offload_job_poll(OffloadJob* job, OffloadProgress* out)
{
    if (!job || !out)
        return;
    std::lock_guard<std::mutex> lk(job->mu);

    out->bytesDone        = job->prog.bytesDone;
    out->bytesTotal       = job->prog.bytesTotal;
    out->currentFileDone  = job->prog.currentFileDone;
    out->currentFileSize  = job->prog.currentFileSize;
    out->filesDone        = job->prog.filesDone;
    out->filesTotal       = job->prog.filesTotal;
    out->phase            = static_cast<int32_t>(job->prog.phase);
    out->finished         = job->finished ? 1 : 0;
    out->cancelled        = job->cancelFlag.load() ? 1 : 0;
    out->bytesWritten     = job->finished ? job->bytesWritten : job->prog.bytesDone;
    out->verifiedCount    = job->verifiedCount;
    out->skippedCount     = job->skippedCount;
    out->failedCount      = job->failedCount;
    out->brokenCount      = job->brokenCount;
    out->fileCount        = job->fileCount;
    out->ok               = (job->finished && job->haveReport &&
                             offload::allVerified(job->report) &&
                             !job->report.cancelled &&
                             job->errorText.empty()) ? 1 : 0;
    out->elapsedSeconds   = std::chrono::duration<double>(std::chrono::steady_clock::now() - job->started).count();
}


extern "C" const char* offload_job_current_file(OffloadJob* job)
{
    if (!job)
        return "";
    std::lock_guard<std::mutex> lk(job->mu);
    return copyOut(job->currentFile);
}


extern "C" const char* offload_job_error(OffloadJob* job)
{
    if (!job)
        return "";
    std::lock_guard<std::mutex> lk(job->mu);
    return copyOut(job->errorText);
}


extern "C" const char* offload_job_report_csv(OffloadJob* job)
{
    if (!job)
        return "";
    std::lock_guard<std::mutex> lk(job->mu);
    return copyOut(job->reportCsv);
}


extern "C" void offload_job_pause(OffloadJob* job, int32_t paused)
{
    if (!job)
        return;
    job->pauseFlag.store(paused != 0);
}


extern "C" int32_t offload_job_paused(OffloadJob* job)
{
    return job && job->pauseFlag.load() ? 1 : 0;
}


extern "C" void offload_job_cancel(OffloadJob* job)
{
    if (!job)
        return;
    job->cancelFlag.store(true);
    job->pauseFlag.store(false); //a paused worker must still wake up to see the cancel
}


extern "C" void offload_job_destroy(OffloadJob* job)
{
    if (!job)
        return;
    if (job->worker.joinable())
    {
        job->cancelFlag.store(true);
        job->pauseFlag.store(false);
        job->worker.join();
    }
    delete job;
}
