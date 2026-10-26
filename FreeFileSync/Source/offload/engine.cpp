// *****************************************************************************
// * Offload mode for FreeFileSync - engine implementation.                     *
// *****************************************************************************

#include "engine.h"
#include "broken.h"
#include "cache.h"
#include "hash.h"

#include <zen/file_access.h>
#include <zen/file_path.h>
#include <zen/file_error.h>
#include <zen/file_traverser.h>
#include <zen/utf.h>

#include "afs/native.h"
#include "afs/abstract.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace zen;
using AFS = fff::AbstractFileSystem;

namespace offload
{
namespace
{
const zen::IoCallback noProgress = [](int64_t) {};

//Park the calling worker for as long as the pause flag is raised. Cancellation
//wins over pause so a stopped job never hangs here.
void waitWhilePaused(const Options& opts)
{
    if (!opts.pause)
        return;
    while (opts.pause->load())
    {
        if (opts.cancel && opts.cancel->load())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

const char* const kToolVersion = "0.6.0";

//One destination's progress state. Owned by that destination's worker thread, so
//nothing here is shared and nothing needs a lock.
struct Sink
{
    const Options* opts = nullptr;
    Progress       prog;
    std::chrono::steady_clock::time_point lastEmit;

    void emit(bool force = false)
    {
        if (!opts || !opts->onProgress)
            return;
        const auto now = std::chrono::steady_clock::now();
        const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEmit).count();
        if (!force && gap < static_cast<long long>(opts->progressIntervalMs))
            return;
        lastEmit = now;
        opts->onProgress(prog);
    }

    void addBytes(uint64_t n)
    {
        prog.bytesDone       += n;
        prog.currentFileDone += n;
        emit();
    }

    void startFile(const Zstring& relPath, uint64_t size)
    {
        prog.currentFile     = relPath;
        prog.currentFileSize = size;
        prog.currentFileDone = 0;
        emit(true);
    }

    void finishFile()
    {
        ++prog.filesDone;
        emit(true);
    }

    void setPhase(Progress::Phase p)
    {
        prog.phase = p;
        emit(true);
    }
};

//Our own publish temp files: a run killed mid-copy leaves "name.ffs_tmp"
//behind. They are not real files: never copy them, never index them.
bool isTempFileName(const Zstring& itemName)
{
    return zen::endsWith(itemName, Zstr(".ffs_tmp"));
}

//The hash cache lives at the destination root, but it is metadata, not data:
//never copy it, never index it. (It is also skipped on the source side, so a
//card that somehow carries one does not get it copied along.)
bool isCacheFileName(const Zstring& itemName)
{
    return itemName == kCacheFileName;
}

//Junk that lives on every camera card and must not be copied or counted.
bool isCardJunk(const Zstring& itemName)
{
    if (itemName == Zstr(".DS_Store") || itemName == Zstr("._.DS_Store") ||
        itemName == Zstr("Thumbs.db")    || itemName == Zstr("desktop.ini"))
        return true;
    if (itemName.size() >= 2 && itemName[0] == u'.' && itemName[1] == u'_') //AppleDouble "._FILE"
        return true;
    return itemName == Zstr(".Spotlight-V100") || itemName == Zstr(".fseventsd") ||
           itemName == Zstr(".Trashes")        || itemName == Zstr(".TemporaryItems") ||
           itemName == Zstr("$RECYCLE.BIN")    || itemName == Zstr("System Volume Information");
}

struct SourceFile
{
    Zstring relPath; //uses '/' as separator
    uint64_t size = 0;
    time_t modTime = 0;
    bool broken = false; //source header failed the media check (see broken.h)
    std::string brokenReason;
};

struct DestEntry
{
    uint64_t size = 0;
    time_t modTime = 0;
};

Zstring joinRel(const Zstring& dir, const Zstring& name)
{
    return dir.empty() ? name : dir + Zstr('/') + name;
}

//recursive walk; returns files relative to root, skipping card junk
void scanTree(const Zstring& absDir, const Zstring& relDir,
              std::vector<SourceFile>& filesOut, std::map<Zstring, DestEntry>& mapOut,
              const std::atomic<bool>* cancel, size_t depth = 0)
{
    if (depth > 64) //guards against pathological nesting / symlink-free loops
        return;

    traverseFolder(absDir,
                   [&](const FileInfo& fi)
    {
        if (isCardJunk(fi.itemName))
            return;
        if (isTempFileName(fi.itemName))
            return; //a stale publish temp is not a file to copy or index
        if (isCacheFileName(fi.itemName))
            return; //the hash cache is metadata, not data
        const Zstring rel = joinRel(relDir, fi.itemName);
        filesOut.push_back({ rel, fi.fileSize, fi.modTime });
        mapOut[rel] = { fi.fileSize, fi.modTime };
    },
    [&](const FolderInfo& fo)
    {
        if (isCardJunk(fo.itemName))
            return;
        if (cancel && cancel->load())
            return;
        scanTree(fo.fullPath, joinRel(relDir, fo.itemName), filesOut, mapOut, cancel, depth + 1);
    },
    [](const SymlinkInfo&) {}); //symlinks are not followed and not copied
}

bool hashFile(const Zstring& path, Hasher& hasher, std::string& err, Sink* sink = nullptr)
{
    try
    {
        const auto in = AFS::getInputStream(fff::createItemPathNative(path)); //throw FileError
        const size_t blockSize = in->getBlockSize();
        std::vector<char> buf(blockSize);

        for (;;)
        {
            const size_t n = in->tryRead(buf.data(), blockSize, noProgress); //0 == EOF
            if (n == 0)
                break;
            hasher.update(buf.data(), n);
            if (sink)
                sink->addBytes(n);
        }
        return true;
    }
    catch (const FileError& e) { err = zen::utfTo<std::string>(e.toString()); return false; }
    catch (const std::exception& e) { err = e.what(); return false; }
}

//copy while hashing; the hash covers exactly the bytes written
bool copyAndHash(const Zstring& srcPath, const Zstring& dstPath,
                 uint64_t size, time_t modTime, const Options& opts, Sink& sink,
                 std::string& hashOut, std::string& err)
{
    try
    {
        const auto in  = AFS::getInputStream (fff::createItemPathNative(srcPath));  //throw FileError

        //Publish through a temp file in the same folder, then rename: a crash,
        //kill or power cut mid-copy leaves a .ffs_tmp behind, never a partial
        //at the final name. The temp is unlinked by ~FileOutputPlain if the
        //copy throws; the rename below is atomic on one filesystem.
        const Zstring tmpPath = dstPath + Zstr(".ffs_tmp");
        const auto out = AFS::getOutputStream(fff::createItemPathNative(tmpPath),
                                              size, modTime); //throw FileError
        const size_t blockSize = in->getBlockSize();

        Hasher hasher(opts.hashAlgo);
        std::vector<char> buf(blockSize);

        for (;;)
        {
            waitWhilePaused(opts);
            const size_t n = in->tryRead(buf.data(), blockSize, noProgress);
            if (n == 0)
                break;
            hasher.update(buf.data(), n);

            size_t written = 0;
            while (written < n)
                written += out->tryWrite(buf.data() + written, n - written, noProgress);
            sink.addBytes(n);
        }
        out->finalize(noProgress); //throw FileError
        try
        {
            //no overwrite: the final name was checked against the destination
            //index before copying, so an existing file here is a real conflict
            zen::moveAndRenameItem(tmpPath, dstPath, false); //throw FileError, ErrorTargetExisting
        }
        catch (...)
        {
            try { removeFilePlain(tmpPath); } catch (const FileError&) {} //no orphan temp
            throw;
        }
        hashOut = hasher.hex();
        return true;
    }
    catch (const FileError& e) { err = zen::utfTo<std::string>(e.toString()); return false; }
    catch (const std::exception& e) { err = e.what(); return false; }
}

//Duplicate Detect, as Offshoot documents it: same name + size + mod time means
//"the same file", so skip it. A same-name file that differs gets an incremented
//name instead of being overwritten.
bool alreadyPresent(const std::map<Zstring, DestEntry>& destIndex, const SourceFile& f)
{
    const auto it = destIndex.find(f.relPath);
    return it != destIndex.end() && it->second.size == f.size && it->second.modTime == f.modTime;
}

Zstring incrementedName(const std::map<Zstring, DestEntry>& destIndex, const Zstring& relPath)
{
    //zen::Zstring has no substr(): do the surgery on std::string and convert back
    const std::string s = zen::utfTo<std::string>(relPath);
    const size_t slash = s.rfind('/');
    const std::string dir  = slash == std::string::npos ? std::string() : s.substr(0, slash + 1);
    const std::string base = slash == std::string::npos ? s : s.substr(slash + 1);

    const size_t dot = base.rfind('.');
    const std::string stem = (dot == std::string::npos || dot == 0) ? base : base.substr(0, dot);
    const std::string ext  = (dot == std::string::npos || dot == 0) ? std::string() : base.substr(dot);

    for (int n = 1; n < 10000; ++n)
    {
        const std::string candidate = dir + stem + "-" + std::to_string(n) + ext;
        const Zstring z(candidate.c_str());
        if (destIndex.find(z) == destIndex.end())
            return z;
    }
    return relPath; //give up; the copy will fail loudly rather than overwrite silently
}

//Every record for a source file carries the scan's verdict on it: the copy
//outcome goes in `state`, the media verdict in `broken`. The two are
//independent - a broken file is still copied, and a good file can still fail.
FileRecord baseRecord(const SourceFile& f)
{
    FileRecord rec;
    rec.relPath      = f.relPath;
    rec.size         = f.size;
    rec.modTime      = f.modTime;
    rec.broken       = f.broken;
    rec.brokenReason = f.brokenReason;
    return rec;
}

void recordFailure(DestReport& dest, const SourceFile& f, FileState state, const std::string& err)
{
    FileRecord rec = baseRecord(f);
    rec.state = state;
    rec.error = err;
    dest.files.push_back(std::move(rec));
}

//Delete publish temps left by a run that was killed mid-copy. Without this the
//next run's temp for the same file would collide (O_EXCL) and fail, and the
//orphan would linger forever. Best-effort: a temp we cannot delete fails
//loudly when its file is copied instead of failing the whole job here.
void removeStaleTempsIn(const Zstring& absDir, size_t depth = 0)
{
    if (depth > 64)
        return;
    traverseFolder(absDir,
                   [](const FileInfo& fi)
    {
        if (isTempFileName(fi.itemName))
            try { removeFilePlain(fi.fullPath); } catch (const FileError&) {}
    },
    [&](const FolderInfo& fo)
    {
        removeStaleTempsIn(fo.fullPath, depth + 1);
    },
    [](const SymlinkInfo&) {}); //never follow a symlink out of the destination
}

void removeStaleTemps(const Zstring& destRoot)
{
    try
    {
        removeStaleTempsIn(appendSeparator(destRoot));
    }
    catch (const FileError&) { /*destination does not exist yet: nothing to clean*/ }
}

void runDestination(const Options& opts, const std::vector<SourceFile>& source,
                    DestReport& dest)
{
    //A previous run killed mid-copy leaves .ffs_tmp files behind; clear them
    //before scanning, or the next temp for the same file collides (O_EXCL).
    removeStaleTemps(dest.destRoot);

    //what is already there, for Duplicate Detect
    std::map<Zstring, DestEntry> destIndex;
    {
        std::vector<SourceFile> ignored;
        try
        {
            scanTree(appendSeparator(dest.destRoot), Zstring(), ignored, destIndex, opts.cancel);
        }
        catch (const FileError&) { /*missing destination folder: nothing present yet*/ }
    }

    //hashes we proved on an earlier run: a matching entry (same path, size,
    //mtime and algorithm) means the copy on this destination is already
    //hash-confirmed, so Duplicate Detect does not have to hash it again.
    //--reverify never consults the cache (see below): it always hashes
    //fresh. A missing or unreadable cache is just an empty one.
    HashCache cache;
    cache.load(appendSeparator(dest.destRoot) + kCacheFileName);

    //Plan the work before touching anything, so the progress bar has an honest
    //denominator: files Duplicate Detect will skip contribute no bytes.
    uint64_t plannedBytes = 0;
    if (opts.reverifyOnly)
    {
        for (const SourceFile& f : source)
        {
            plannedBytes += f.size;
            const auto it = destIndex.find(f.relPath);
            if (it != destIndex.end())
                plannedBytes += it->second.size; //both sides are hashed
        }
    }
    else if (!opts.dryRun)
    {
        for (const SourceFile& f : source)
            if (!alreadyPresent(destIndex, f))
                plannedBytes += f.size;
    }

    Sink sink;
    sink.opts = &opts;
    sink.lastEmit = std::chrono::steady_clock::now();
    sink.prog.filesTotal = source.size();
    sink.prog.phase      = opts.reverifyOnly ? Progress::Phase::verifying : Progress::Phase::copying;
    //with read-back verification every byte is touched twice, so the bar needs
    //twice the bytes to fill; the phase tells the UI which pass is running
    sink.prog.bytesTotal = opts.verify == VerifyMode::sourceAndDest && !opts.reverifyOnly && !opts.dryRun
                           ? plannedBytes * 2 : plannedBytes;
    sink.emit(true);

    for (const SourceFile& f : source)
    {
        //every exit path from this loop body (there are seven) must count the file
        struct FileCounter
        {
            Sink& sink;
            ~FileCounter() { sink.finishFile(); }
        } fileCounter{ sink };

        sink.startFile(f.relPath, f.size);
        waitWhilePaused(opts);
        if (opts.cancel && opts.cancel->load())
        {
            FileRecord rec = baseRecord(f);
            rec.state   = FileState::failed;
            rec.error   = "cancelled";
            dest.files.push_back(std::move(rec));
            continue;
        }

        const Zstring srcPath = appendSeparator(opts.sourceRoot) + f.relPath;
        Zstring       dstRel  = f.relPath;

        //re-verification hashes what is actually on the destination and must
        //never be short-circuited by the cache: a file whose bytes changed
        //while size and mtime stayed put is invisible to a size+mtime key, so
        //a cached hash is not a proof. The cache serves only the
        //already-present check below. The source is always hashed fresh too,
        //because it is the ground truth.
        if (opts.reverifyOnly)
        {
            FileRecord rec = baseRecord(f);

            const Zstring reverifyPath = appendSeparator(dest.destRoot) + f.relPath;
            std::string err;
            Hasher srcHasher(opts.hashAlgo);
            if (!hashFile(srcPath, srcHasher, err, &sink))
            {
                rec.state = FileState::failed;
                rec.error = "source: " + err;
            }
            else if (destIndex.find(f.relPath) == destIndex.end())
            {
                rec.state = FileState::failed;
                rec.error = "missing on destination";
            }
            else
            {
                const DestEntry& de = destIndex[f.relPath];
                std::string dstHash;
                bool dstOk = false; //the destination was hashed fresh
                //always re-hash: --reverify is the byte-level proof
                Hasher dstHasher(opts.hashAlgo);
                if (!hashFile(reverifyPath, dstHasher, err, &sink))
                {
                    rec.state = FileState::failed;
                    rec.error = "destination: " + err;
                }
                else
                {
                    dstHash = dstHasher.hex();
                    ++dest.cacheMisses;
                    dstOk = true;
                }

                if (dstOk)
                {
                    rec.hash  = srcHasher.hex();
                    if (srcHasher.hex() == dstHash)
                    {
                        rec.state = FileState::verified;
                        //the destination hash is now proven fresh: remember
                        //it, so a later already-present check can confirm it
                        cache.store(f.relPath, de.size, de.modTime, opts.hashAlgo, dstHash);
                    }
                    else
                    {
                        rec.state = FileState::mismatch;
                        rec.error = "hash differs: source " + rec.hash + ", destination " + dstHash;
                        //the cached entry (if any) claimed this file was
                        //fine: drop it, so it can never be trusted again
                        cache.erase(f.relPath);
                    }
                }
            }
            dest.files.push_back(std::move(rec));
            if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
            continue;
        }

        if (alreadyPresent(destIndex, f))
        {
            FileRecord rec = baseRecord(f);
            rec.state = FileState::alreadyPresent;
            //a cache hit turns "same name+size+mtime" into "hash-confirmed":
            //the entry was stored when this exact file was verified here
            const DestEntry& de = destIndex[f.relPath];
            if (const auto hit = cache.lookup(f.relPath, de.size, de.modTime, opts.hashAlgo))
            {
                rec.hash = *hit;
                rec.hashConfirmed = true;
                ++dest.cacheHits;
            }
            else
                ++dest.cacheMisses;
            dest.files.push_back(std::move(rec));
            if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
            continue;
        }

        //same name, different content or date: never overwrite, take the next free name
        if (destIndex.find(dstRel) != destIndex.end())
            dstRel = incrementedName(destIndex, dstRel);

        const Zstring dstPath = appendSeparator(dest.destRoot) + dstRel;

        //make sure the parent folder exists before opening the output stream
        try
        {
            const std::string dstStd = zen::utfTo<std::string>(dstPath);
            const size_t slash = dstStd.rfind('/');
            if (slash != std::string::npos && slash > 0)
                AFS::createFolderIfMissingRecursion(fff::createItemPathNative(Zstring(dstStd.substr(0, slash).c_str())));
        }
        catch (const FileError& e)
        {
            recordFailure(dest, f, FileState::failed, zen::utfTo<std::string>(e.toString()));
            if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
            continue;
        }

        if (opts.dryRun)
        {
            FileRecord rec = baseRecord(f);
            rec.relPath = dstRel;
            rec.state   = FileState::copied;
            dest.files.push_back(std::move(rec));
            if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
            continue;
        }

        //--- copy, then verify -------------------------------------------------
        FileRecord rec = baseRecord(f);
        rec.relPath = dstRel; //an incremented name is where the file actually landed

        std::string hash, err;
        if (!copyAndHash(srcPath, dstPath, f.size, f.modTime, opts, sink, hash, err))
        {
            rec.state = FileState::failed;
            rec.error = err;
            dest.files.push_back(std::move(rec));
            if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
            continue;
        }
        rec.hash  = hash;
        rec.state = FileState::copied;
        dest.bytesCopied += f.size;
        ++dest.filesWritten;

        if (opts.verify == VerifyMode::sourceAndDest)
        {
            Hasher back(opts.hashAlgo);
            std::string verifyErr;
            sink.setPhase(Progress::Phase::verifying);
            if (!hashFile(dstPath, back, verifyErr, &sink))
            {
                rec.state = FileState::failed;
                rec.error = "read-back failed: " + verifyErr;
            }
            else if (back.hex() != hash)
            {
                rec.state = FileState::mismatch;
                rec.error = "read-back hash " + back.hex() + " != written " + hash;
            }
            else
                rec.state = FileState::verified;

            sink.setPhase(Progress::Phase::copying);
        }

        //Only a read-back-verified copy earns a cache entry: the hash is then
        //proven to be the bytes on this destination, not just the bytes that
        //were written. (In size-only mode nothing is read back, so nothing is
        //cached and a later run cannot "confirm" what was never checked.)
        if (rec.state == FileState::verified)
            cache.store(dstRel, f.size, f.modTime, opts.hashAlgo, rec.hash);

        dest.files.push_back(std::move(rec));
        if (opts.onFileDone) opts.onFileDone(dest.destRoot, dest.files.back());
    }

    //A dry run touches nothing, so it writes no cache either.
    if (!opts.dryRun && cache.dirty())
    {
        const std::string cacheErr = cache.save(appendSeparator(dest.destRoot) + kCacheFileName);
        if (!cacheErr.empty())
            dest.cacheError = cacheErr; //a lost cache only costs re-hashing, never data
    }

    sink.setPhase(Progress::Phase::done);
}
} //namespace


std::string getVerifyModeName(VerifyMode mode)
{
    switch (mode)
    {
        case VerifyMode::sizeOnly:      return "Transfer (size only)";
        case VerifyMode::sourceAndDest: return "Source & Destination";
    }
    return "Source & Destination";
}

std::string getStateName(FileState state)
{
    switch (state)
    {
        case FileState::copied:         return "copied";
        case FileState::alreadyPresent: return "already-present";
        case FileState::verified:       return "verified";
        case FileState::mismatch:       return "mismatch";
        case FileState::failed:         return "failed";
    }
    return "failed";
}

bool isFailure(FileState state)
{
    return state == FileState::mismatch || state == FileState::failed;
}


void tally(DestReport& dest)
{
    dest.copied = dest.skipped = dest.verified = dest.mismatched = dest.failed = dest.broken = 0;
    for (const FileRecord& f : dest.files)
    {
        switch (f.state)
        {
            case FileState::copied:         ++dest.copied;     break;
            case FileState::alreadyPresent: ++dest.skipped;    break;
            //a broken file is copied faithfully, but it is never verified-good
            case FileState::verified:       if (!f.broken) ++dest.verified; break;
            case FileState::mismatch:       ++dest.mismatched; break;
            case FileState::failed:         ++dest.failed;     break;
        }
        if (f.broken)
            ++dest.broken;
        if (f.state == FileState::verified && f.size > 0)
            ; //verified bytes are already counted in bytesCopied when copied
    }
}

bool allVerified(const JobReport& job)
{
    //a source that could not be walked never verifies, even if a caller built the
    //report by hand
    if (!job.error.empty())
        return false;
    if (job.dests.empty())
        return false;
    for (const DestReport& d : job.dests)
    {
        if (d.failed > 0 || d.mismatched > 0)
            return false;
        //Every source file must have produced exactly one record here. A worker
        //that died mid-run leaves a prefix of records; a prefix with no failures
        //must never read as "everything arrived", or a half-copied card looks
        //like a finished one.
        if (d.files.size() != d.expectedFiles)
            return false;
        //A damaged source file is copied faithfully, but it is not a verified-good
        //copy: the job cannot claim every file verified while one of them is broken.
        if (d.broken > 0)
            return false;
    }
    return true;
}

size_t totalFiles(const JobReport& job)
{
    size_t n = 0;
    for (const DestReport& d : job.dests)
        n += d.files.size();
    return n;
}

size_t totalSucceeded(const JobReport& job)
{
    size_t n = 0;
    for (const DestReport& d : job.dests)
        n += d.verified + d.skipped + d.copied;
    return n;
}


bool measureSource(const Zstring& sourceRoot, uint64_t& bytesOut, size_t& filesOut, std::string& err)
{
    bytesOut = 0;
    filesOut = 0;

    std::vector<SourceFile> files;
    std::map<Zstring, DestEntry> ignored;
    try
    {
        scanTree(appendSeparator(sourceRoot), Zstring(), files, ignored, nullptr);
    }
    catch (const FileError& e)
    {
        err = zen::utfTo<std::string>(e.toString());
        return false;
    }

    for (const SourceFile& f : files)
        bytesOut += f.size;
    filesOut = files.size();
    return true;
}


JobReport runJob(const Options& opts)
{
    JobReport job;
    job.sourceRoot  = opts.sourceRoot;
    job.verifyMode  = getVerifyModeName(opts.verify);
    job.hashAlgo    = opts.hashAlgo;
    job.toolVersion = kToolVersion;
    job.startedAt   = std::time(nullptr);
    job.dryRun      = opts.dryRun;
    job.reverifyOnly = opts.reverifyOnly;

    //scan the source once; every destination shares the same file list
    std::vector<SourceFile> source;
    {
        std::map<Zstring, DestEntry> ignored;
        try
        {
            scanTree(appendSeparator(opts.sourceRoot), Zstring(), source, ignored, opts.cancel);
        }
        catch (const FileError& e)
        {
            //A failed scan is a failure, not a cancellation: record the reason
            //and run no destination, so no caller can read this as "completed".
            job.finishedAt = std::time(nullptr);
            job.error = zen::utfTo<std::string>(e.toString());
            return job;
        }
        catch (const std::exception& e)
        {
            job.finishedAt = std::time(nullptr);
            job.error = e.what();
            return job;
        }
    }
    std::sort(source.begin(), source.end(), [](const SourceFile& l, const SourceFile& r) { return l.relPath < r.relPath; });

    //Judge the media headers once, up front, while the scan is still the
    //phase in progress: every destination shares the same verdict, and the
    //check reads only file heads, so it stays cheap next to the copy.
    for (SourceFile& f : source)
    {
        const Zstring srcPath = appendSeparator(opts.sourceRoot) + f.relPath;
        std::string reason;
        if (checkFileBroken(srcPath, f.size, reason))
        {
            f.broken = true;
            f.brokenReason = reason;
        }
    }

    job.dests.resize(opts.destRoots.size());
    for (size_t i = 0; i < opts.destRoots.size(); ++i)
    {
        job.dests[i].destRoot = opts.destRoots[i];
        //pinned before the workers start, so a short report can be recognised
        job.dests[i].expectedFiles = source.size();
    }

    //one worker per destination: a slow drive must not throttle a fast one
    const size_t workerCount = std::min(opts.destRoots.size(), std::max<size_t>(opts.maxDestinations, 1));
    std::vector<std::thread> workers;
    std::atomic<size_t> next{ 0 };

    for (size_t w = 0; w < workerCount; ++w)
        workers.emplace_back([&]()
    {
        for (;;)
        {
            const size_t idx = next.fetch_add(1);
            if (idx >= job.dests.size())
                break;
            try
            {
                runDestination(opts, source, job.dests[idx]);
            }
            catch (const std::exception& e)
            {
                //never let one destination kill the others
                DestReport& d = job.dests[idx];
                if (d.files.empty())
                {
                    FileRecord rec;
                    rec.relPath = Zstring();
                    rec.state   = FileState::failed;
                    rec.error   = e.what();
                    d.files.push_back(std::move(rec));
                }
            }
        }
    });

    for (std::thread& t : workers)
        t.join();

    for (DestReport& d : job.dests)
        tally(d);

    job.finishedAt = std::time(nullptr);
    job.cancelled  = opts.cancel && opts.cancel->load();
    return job;
}
}
