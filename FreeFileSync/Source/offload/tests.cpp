// *****************************************************************************
// * ffsoffload tests.                                                          *
// *                                                                            *
// * Hash vectors are ground truth from `xxhsum -H1` (XXH64, seed 0), so a      *
// * broken hasher cannot pass silently. The filesystem tests assert the things *
// * that would actually lose a wedding: nothing is lost, corruption is caught, *
// * and the card is never written to.                                          *
// *****************************************************************************

#include "engine.h"
#include "broken.h"
#include "cache.h"
#include "filter.h"
#include "hash.h"
#include "report.h"

#include <zen/file_access.h>
#include <zen/file_io.h>
#include <zen/file_path.h>
#include <zen/file_traverser.h>
#include <zen/utf.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace offload;
using namespace zen;

namespace
{
int failures = 0;
int checks   = 0;

void check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::cout << "  FAIL  " << what << "\n";
    }
}

std::string hashOf(const std::string& bytes, size_t chunk = 0)
{
    Xxh64 h;
    if (chunk == 0)
        h.update(bytes.data(), bytes.size());
    else
        for (size_t i = 0; i < bytes.size(); i += chunk)
            h.update(bytes.data() + i, std::min(chunk, bytes.size() - i));
    return toHexBE(h.digest());
}

std::string pattern(size_t n)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i)
        s[i] = static_cast<char>(i % 251);
    return s;
}

void writeText(const Zstring& path, const std::string& bytes)
{
    setFileContent(path, bytes, [](int64_t) {});
}

std::string readText(const Zstring& path)
{
    return getFileContent(path, [](int64_t) {});
}

//--------------------------------------------------------------- hash vectors
void testHashing()
{
    std::cout << "hashing\n";
    //ground truth: xxhsum -H1 on the same bytes
    check(hashOf("")            == "ef46db3751d8e999", "XXH64(\"\") vector");
    check(hashOf("a")           == "d24ec4f1a98c6e5b", "XXH64(\"a\") vector");
    check(hashOf("abc")         == "44bc2cf5ad770999", "XXH64(\"abc\") vector");
    check(hashOf(pattern(32))   == "cbf59c5116ff32b4", "XXH64(32 bytes) boundary vector");
    check(hashOf(pattern(33))   == "0c535d1acafb8ead", "XXH64(33 bytes) tail vector");
    check(hashOf(pattern(100000)) == "4cf75ee72cd8f4cc", "XXH64(100000 bytes) vector");

    //streaming in awkward chunk sizes must equal the one-shot digest
    const std::string big = pattern(100000);
    for (size_t chunk : { 1u, 7u, 8u, 31u, 32u, 33u, 4096u })
        check(hashOf(big, chunk) == "4cf75ee72cd8f4cc", "streamed in " + std::to_string(chunk) + "-byte chunks");

    check(toHexBE(0x0123456789abcdefULL) == "0123456789abcdef", "big-endian hex");

    //MD5 and SHA-1 ground truth: python hashlib / `md5 -q` / `shasum -a 1`
    const auto digestOf = [](HashAlgo algo, const std::string& bytes)
    {
        Hasher h(algo);
        h.update(bytes.data(), bytes.size());
        return h.hex();
    };

    check(digestOf(HashAlgo::md5, "")  == "d41d8cd98f00b204e9800998ecf8427e", "MD5(\"\") vector");
    check(digestOf(HashAlgo::sha1, "") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA1(\"\") vector");
    check(digestOf(HashAlgo::md5, "abc")  == "900150983cd24fb0d6963f7d28e17f72", "MD5(\"abc\") vector");
    check(digestOf(HashAlgo::sha1, "abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA1(\"abc\") vector");

    //padding boundaries: 55/56 and 63/64 are where both length-padding paths split
    check(digestOf(HashAlgo::md5,  pattern(55))  == "6912ee65fff2d9f9ce2508cddf8bcda0", "MD5(55 bytes) no-extra-block vector");
    check(digestOf(HashAlgo::md5,  pattern(56))  == "51fdd1acda72405dfdfa03fcb85896d7", "MD5(56 bytes) extra-block vector");
    check(digestOf(HashAlgo::md5,  pattern(64))  == "b2d3f56bc197fd985d5965079b5e7148", "MD5(64 bytes) vector");
    check(digestOf(HashAlgo::sha1, pattern(55))  == "8ae2d46729cfe68ff927af5eec9c7d1b66d65ac2", "SHA1(55 bytes) vector");
    check(digestOf(HashAlgo::sha1, pattern(56))  == "636e2ec698dac903498e648bd2f3af641d3c88cb", "SHA1(56 bytes) vector");
    check(digestOf(HashAlgo::sha1, pattern(65))  == "69bd728ad6e13cd76ff19751fde427b00e395746", "SHA1(65 bytes) vector");
    check(digestOf(HashAlgo::sha1, pattern(100000)) == "23a1065a0f6a485119049bf2799179dd0154efbb", "SHA1(100000 bytes) vector");

    //chunked updates must equal the one-shot digest for every algorithm
    for (HashAlgo algo : { HashAlgo::xxh64, HashAlgo::md5, HashAlgo::sha1 })
    {
        const std::string oneShot = digestOf(algo, pattern(100000));
        for (size_t chunk : { 1u, 7u, 64u, 65u, 4096u })
        {
            Hasher h(algo);
            const std::string big = pattern(100000);
            for (size_t i = 0; i < big.size(); i += chunk)
                h.update(big.data() + i, std::min(chunk, big.size() - i));
            check(h.hex() == oneShot,
                  getAlgoName(algo) + " streamed in " + std::to_string(chunk) + "-byte chunks");
        }
    }

    HashAlgo parsed = HashAlgo::xxh64;
    check(parseHashAlgo("SHA1", parsed) && parsed == HashAlgo::sha1, "--hash parsing is case-insensitive");
    check(parseHashAlgo("sha-1", parsed) && parsed == HashAlgo::sha1, "--hash accepts sha-1");
    check(!parseHashAlgo("crc32", parsed), "--hash rejects an unknown algorithm");
}

//------------------------------------------------------------- scenario setup
//Minimal valid media headers, so the fixture files pass the broken-file
//check: a real JPEG starts SOI + APP0, a real CR3 starts with an ftyp box
//branded "crx ". The filler after the header is the same deterministic
//pattern the hash tests already pin down.
std::string validJpeg(size_t total)
{
    std::string s;
    s += static_cast<char>(0xFF);
    s += static_cast<char>(0xD8);
    s += static_cast<char>(0xFF);
    s += static_cast<char>(0xE0); //SOI + APP0
    s += pattern(total - 4);
    return s;
}

std::string validCr3(size_t total)
{
    std::string s("\x00\x00\x00\x18" "ftyp" "crx ", 12); //box size + ftyp + brand
    s += pattern(total - 12);
    return s;
}

struct Scratch
{
    Zstring root;
    Zstring source;
    explicit Scratch(const Zstring& name)
    {
        root   = appendSeparator(getTempFolderPath()) + Zstr("ffsoffload-test-") + name;
        source = appendSeparator(root) + Zstr("CARD");
        try { if (itemExists(root)) removeDirectoryPlainRecursion(root); } catch (const FileError&) {} //start from a clean slate
    }

    void buildSource()
    {
        createDirectoryIfMissingRecursion(appendSeparator(source) + Zstr("DCIM/100CANON"));
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0001.JPG"), validJpeg(5000));
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0002.JPG"), validJpeg(12345));
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0003.CR3"), validCr3(70000));
        writeText(appendSeparator(source) + Zstr("clip.mov"),                     pattern(250000));
    }

    Zstring dest(const char* name) const { return appendSeparator(root) + name; }
};

Options makeOptions(const Scratch& s, std::vector<Zstring> dests)
{
    Options o;
    o.sourceRoot = s.source;
    o.destRoots  = std::move(dests);
    return o;
}

//-------------------------------------------------- copy + verify + immutability
void testCopyAndVerify()
{
    std::cout << "copy and verify\n";
    Scratch s(Zstr("copy"));
    s.buildSource();

    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    //record the source tree so we can prove afterwards that nothing touched it
    std::vector<std::pair<Zstring, uint64_t>> before;
    traverseFolder(appendSeparator(s.source) + Zstr("DCIM/100CANON"),
                   [&](const FileInfo& fi) { before.emplace_back(fi.itemName, fi.fileSize); },
                   [](const FolderInfo&) {}, [](const SymlinkInfo&) {});
    const std::string srcHashBefore = hashOf(readText(appendSeparator(s.source) + Zstr("clip.mov")));

    const JobReport job = runJob(makeOptions(s, { destA }));

    check(job.dests.size() == 1, "one destination reported");
    check(allVerified(job), "every file verified");
    check(job.dests[0].verified == 4, "four files verified (got " + std::to_string(job.dests[0].verified) + ")");
    check(job.dests[0].mismatched == 0 && job.dests[0].failed == 0, "no mismatches or failures");

    //byte-identical output, and the recorded hash matches the source bytes
    check(readText(appendSeparator(destA) + Zstr("clip.mov")) == pattern(250000), "destination bytes identical");
    check(job.dests[0].files.at(3).hash == hashOf(pattern(250000)), "reported hash matches xxh64 of the content");

    //source untouched
    std::vector<std::pair<Zstring, uint64_t>> after;
    traverseFolder(appendSeparator(s.source) + Zstr("DCIM/100CANON"),
                   [&](const FileInfo& fi) { after.emplace_back(fi.itemName, fi.fileSize); },
                   [](const FolderInfo&) {}, [](const SymlinkInfo&) {});
    check(before == after, "source file list and sizes unchanged");
    check(hashOf(readText(appendSeparator(s.source) + Zstr("clip.mov"))) == srcHashBefore, "source bytes unchanged");
}

//--------------------------------------------------------------- duplicate skip
void testDuplicateDetect()
{
    std::cout << "duplicate detect\n";
    Scratch s(Zstr("dup"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    const JobReport first = runJob(makeOptions(s, { destA }));
    check(allVerified(first), "first run verifies");

    const JobReport second = runJob(makeOptions(s, { destA }));
    check(allVerified(second), "second run verifies");
    check(second.dests[0].skipped == 4, "second run skips all four (already present, mtime preserved)");
    check(second.dests[0].bytesCopied == 0, "second run writes zero bytes");
}

//------------------------------------------------------------ corruption is caught
void testCorruptionDetected()
{
    std::cout << "corruption detected\n";
    Scratch s(Zstr("corrupt"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    runJob(makeOptions(s, { destA }));

    //flip bytes inside the copy - the exact failure a bad cable or a dying
    //drive produces. Size and date stay put on purpose: --reverify always
    //re-hashes the destination fresh, so a content change that keeps both
    //must still be caught (the cache is never consulted there, see cache.h).
    const Zstring victim = appendSeparator(destA) + Zstr("DCIM/100CANON/IMG_0002.JPG");
    std::string bytes = readText(victim);
    bytes[100] = static_cast<char>(bytes[100] ^ 0xFF);
    writeText(victim, bytes);

    Options o = makeOptions(s, { destA });
    o.reverifyOnly = true;
    const JobReport job = runJob(o);

    check(!allVerified(job), "re-verify reports failure");
    check(job.dests[0].mismatched == 1, "exactly one mismatch (got " + std::to_string(job.dests[0].mismatched) + ")");
    check(job.dests[0].verified == 3, "the other three still verify");

    bool found = false;
    for (const FileRecord& f : job.dests[0].files)
        if (f.state == FileState::mismatch &&
            zen::utfTo<std::string>(f.relPath).find("IMG_0002") != std::string::npos)
            found = true;
    check(found, "mismatch attributed to the corrupted file");

    //the mismatch also drops the stale cache entry, so a later run can never
    //trust the corrupted file as "hash-confirmed"
    const std::string cacheText = readText(appendSeparator(destA) + Zstr(".ffsoffload-cache"));
    check(cacheText.find("IMG_0002.JPG") == std::string::npos,
          "the corrupted file's cache entry is gone after the mismatch");
}

//------------------------------------------------------ name clash never overwrites
void testNameClash()
{
    std::cout << "name clash\n";
    Scratch s(Zstr("clash"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    //same name, different content, different size: must not overwrite
    writeText(appendSeparator(destA) + Zstr("clip.mov"), "something else entirely");

    const JobReport job = runJob(makeOptions(s, { destA }));
    check(allVerified(job), "clash run verifies");

    check(readText(appendSeparator(destA) + Zstr("clip.mov")) == "something else entirely",
          "pre-existing file left untouched");
    check(readText(appendSeparator(destA) + Zstr("clip-1.mov")) == pattern(250000),
          "new copy written alongside as clip-1.mov");
}

//------------------------------------------------ one destination failing, one fine
void testOneDestinationFails()
{
    std::cout << "multiple destinations\n";
    Scratch s(Zstr("multi"));
    s.buildSource();
    const Zstring good = s.dest("GOOD");
    const Zstring blocked = s.dest("BLOCKED");
    createDirectoryIfMissingRecursion(good);
    //a regular FILE where the destination root should be: every write must fail
    writeText(blocked, "not a directory");

    const JobReport job = runJob(makeOptions(s, { good, blocked }));

    check(job.dests.size() == 2, "both destinations reported");
    check(job.dests[0].verified == 4, "the good destination completed all four files");
    check(!allVerified(job), "job overall fails when one destination fails");
    check(job.dests[1].failed > 0, "the blocked destination records failures");
    check(readText(appendSeparator(good) + Zstr("clip.mov")) == pattern(250000), "good copy is intact");
}

//----------------------------------------------------------------- card junk
void testCardJunkSkipped()
{
    std::cout << "card junk\n";
    Scratch s(Zstr("junk"));
    s.buildSource();
    writeText(appendSeparator(s.source) + Zstr(".DS_Store"), "junk");
    writeText(appendSeparator(s.source) + Zstr("._clip.mov"), "resource fork");

    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    const JobReport job = runJob(makeOptions(s, { destA }));
    check(allVerified(job), "junk run verifies");
    check(job.dests[0].verified == 4, "card junk is not copied (got " + std::to_string(job.dests[0].verified) + ")");
    check(!itemExists(appendSeparator(destA) + Zstr(".DS_Store")), ".DS_Store not copied");
}

//------------------------------------------------------------------- reports
void testReports()
{
    std::cout << "reports\n";
    Scratch s(Zstr("report"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    const JobReport job = runJob(makeOptions(s, { destA }));

    const Zstring csv  = appendSeparator(s.root) + Zstr("report.csv");
    const Zstring json = appendSeparator(s.root) + Zstr("report.json");
    const Zstring mhl  = appendSeparator(destA) + Zstr(".ffsoffload.mhl");

    check(writeCsv (job, csv ).empty(), "csv written");
    check(writeJson(job, json).empty(), "json written");
    check(writeMhl (job, job.dests[0], mhl).empty(), "mhl written");

    const std::string csvText = readText(csv);
    check(csvText.find("destination,relative_path") == 0, "csv header");
    check(csvText.find("verified") != std::string::npos, "csv records state");

    const std::string mhlText = readText(mhl);
    check(mhlText.find("<hashlist version=\"1.1\">") != std::string::npos, "mhl root element");
    check(mhlText.find(hashOf(pattern(250000))) != std::string::npos, "mhl carries the xxh64 digest");
    check(mhlText.find("clip.mov") != std::string::npos, "mhl lists the file");
}

//---------------------------------------------------------------- dry run
void testDryRun()
{
    std::cout << "dry run\n";
    Scratch s(Zstr("dry"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    Options o = makeOptions(s, { destA });
    o.dryRun = true;
    const JobReport job = runJob(o);

    check(job.dests[0].files.size() == 4, "dry run enumerates the files");
    check(!itemExists(appendSeparator(destA) + Zstr("clip.mov")), "dry run writes nothing");
}

//-------------------------------------------------- pause/resume and cancel
void testPauseAndCancel()
{
    std::cout << "pause, resume and cancel\n";
    {
        Scratch s(Zstr("pause"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);

        std::atomic<bool> pause{ true }, cancel{ false };
        std::atomic<size_t> done{ 0 };

        Options o = makeOptions(s, { destA });
        o.pause  = &pause;
        o.cancel = &cancel;
        o.onFileDone = [&](const Zstring&, const FileRecord&) { ++done; };

        JobReport job;
        std::thread worker([&] { job = runJob(o); });
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        check(done.load() == 0, "a paused job processes no file");

        pause.store(false); //resume
        worker.join();

        check(done.load() == 4, "every file is processed after resume");
        check(allVerified(job), "the job still verifies after a pause");
        check(job.dests[0].filesWritten == 4, "pause does not duplicate or skip writes");
    }
    {
        Scratch s(Zstr("cancel"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);

        std::atomic<bool> cancel{ false };
        Options o = makeOptions(s, { destA });
        o.cancel = &cancel;

        //cancel from inside the first completion, so the stop is mid-job and reproducible
        o.onFileDone = [&](const Zstring&, const FileRecord&) { cancel.store(true); };

        const JobReport job = runJob(o);

        check(job.cancelled, "a cancelled job is reported as cancelled");
        check(!allVerified(job), "a cancelled job does not claim success");
        check(job.dests[0].filesWritten == 1, "the file in flight finished and was published");
        check(job.dests[0].files.size() == 4, "every source file is still accounted for");
        check(job.dests[0].failed == 3, "the untouched files are recorded as not done");

        size_t cancelledRecords = 0;
        for (const FileRecord& f : job.dests[0].files)
            if (f.error == "cancelled")
                ++cancelledRecords;
        check(cancelledRecords == 3, "each untouched file carries a cancellation reason");
    }
    {
        //cancelled before the scan even starts: nothing enumerated, nothing written
        Scratch s(Zstr("cancel-early"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);

        std::atomic<bool> cancel{ true };
        Options o = makeOptions(s, { destA });
        o.cancel = &cancel;
        const JobReport job = runJob(o);

        check(job.cancelled, "an early cancel is reported as cancelled");
        check(job.dests[0].filesWritten == 0, "an early cancel writes nothing");
    }
}

//------------------------------------------------- algorithm reaches the report
void testHashAlgorithmReported()
{
    std::cout << "hash algorithm selection\n";
    Scratch s(Zstr("algo"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    Options o = makeOptions(s, { destA });
    o.hashAlgo = HashAlgo::sha1;
    const JobReport job = runJob(o);

    check(allVerified(job), "sha1 job verifies");
    check(job.dests[0].files.at(0).hash.size() == 40, "reported digest is a 40-hex sha1");

    const Zstring csvPath = appendSeparator(s.root) + Zstr("algo.csv");
    const Zstring mhlPath = appendSeparator(s.root) + Zstr("algo.mhl");
    check(writeCsv(job, csvPath).empty(), "csv writes");
    check(writeMhl(job, job.dests[0], mhlPath).empty(), "mhl writes");

    const std::string csv = readText(csvPath);
    const std::string mhl = readText(mhlPath);
    check(csv.find("hash_sha1") != std::string::npos, "csv header names the selected algorithm");
    check(csv.find("hash_xxh64") == std::string::npos, "csv header does not claim xxh64");
    check(mhl.find("<sha1>") != std::string::npos, "mhl uses the matching element");
    check(mhl.find("<xxh64>") == std::string::npos, "mhl does not emit an xxh64 element");
}
} //namespace


//------------------------------------------------------------------- fit check
//The window refuses a destination on the strength of this number, so it has to be
//the same number the copy produces - not a second, approximate walk.
void testMeasureSource()
{
    std::cout << "fit check measurement\n";
    Scratch s(Zstr("measure"));
    s.buildSource();
    writeText(appendSeparator(s.source) + Zstr(".DS_Store"), "junk");
    writeText(appendSeparator(s.source) + Zstr("._clip.mov"), "resource fork");

    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);
    const JobReport job = runJob(makeOptions(s, { destA }));

    uint64_t copiedBytes = 0;
    size_t   copiedFiles = 0;
    for (const FileRecord& f : job.dests[0].files)
        if (f.state == FileState::verified)
        {
            copiedBytes += f.size;
            ++copiedFiles;
        }

    uint64_t measuredBytes = 0;
    size_t   measuredFiles = 0;
    std::string err;
    check(measureSource(s.source, "", measuredBytes, measuredFiles, err), "measureSource succeeds");
    check(err.empty(), "measureSource reports no error on success");
    check(measuredFiles == copiedFiles, "measured file count matches what was copied (" +
          std::to_string(measuredFiles) + " vs " + std::to_string(copiedFiles) + ")");
    check(measuredBytes == copiedBytes, "measured bytes match what was copied (" +
          std::to_string(measuredBytes) + " vs " + std::to_string(copiedBytes) + ")");
    check(measuredBytes > 0, "measurement is not silently zero");

    //a source that cannot be walked must say so, never report "0 bytes, fits"
    uint64_t missingBytes = 0;
    size_t   missingFiles = 0;
    std::string missingErr;
    check(!measureSource(appendSeparator(s.source) + Zstr("no-such-folder"), "",
                         missingBytes, missingFiles, missingErr),
          "measuring a folder that does not exist fails");
    check(!missingErr.empty(), "failed measurement carries a reason");
}

//------------------------------------------------------- a failed scan is a failure
//M1: a source that cannot be walked must produce an error report, never an
//empty job that reads as "completed".
void testScanFailure()
{
    std::cout << "scan failure\n";
    Scratch s(Zstr("scanfailure"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    {
        Options o = makeOptions(s, { destA });
        o.sourceRoot = appendSeparator(s.root) + Zstr("NO-SUCH-CARD");
        const JobReport job = runJob(o);

        check(!job.error.empty(), "a missing source reports an error");
        check(!job.cancelled, "a failed scan is not a cancellation");
        check(job.dests.empty(), "a failed scan runs no destination");
        check(!allVerified(job), "a failed scan does not claim success");
        check(!itemExists(appendSeparator(destA) + Zstr("clip.mov")), "a failed scan writes nothing");
    }
    {
        //a regular file where the source folder should be
        const Zstring notAFolder = appendSeparator(s.root) + Zstr("NOT-A-FOLDER");
        writeText(notAFolder, "not a folder");
        Options o = makeOptions(s, { destA });
        o.sourceRoot = notAFolder;
        const JobReport job = runJob(o);

        check(!job.error.empty(), "a file as source reports an error");
        check(!job.cancelled, "a file as source is not a cancellation");
        check(job.dests.empty(), "a file as source runs no destination");
        check(!allVerified(job), "a file as source does not claim success");
    }
}

//------------------------------------------------------------ temp file + rename
size_t countTempsIn(const Zstring& absDir, size_t depth = 0)
{
    if (depth > 64)
        return 0;
    size_t n = 0;
    traverseFolder(absDir,
                   [&](const FileInfo& fi) { if (zen::endsWith(fi.itemName, Zstr(".ffs_tmp"))) ++n; },
                   [&](const FolderInfo& fo) { n += countTempsIn(fo.fullPath, depth + 1); },
                   [](const SymlinkInfo&) {});
    return n;
}

//M2: a copy publishes through a .ffs_tmp file and rename, so no temp survives
//a successful run and a partial is never visible at the final name.
void testTempPublish()
{
    std::cout << "temp publish\n";
    Scratch s(Zstr("temppublish"));
    s.buildSource();
    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    const JobReport job = runJob(makeOptions(s, { destA }));
    check(allVerified(job), "temp-publish run verifies");
    check(countTempsIn(destA) == 0, "no .ffs_tmp files remain after a successful copy");
    check(itemExists(appendSeparator(destA) + Zstr("clip.mov")), "the published file is at its final name");
    check(readText(appendSeparator(destA) + Zstr("clip.mov")) == pattern(250000), "the published file has the right bytes");
}

//M2: a stale temp left by a killed run must not block the next run, be mistaken
//for a real file, or be copied along.
void testStaleTemps()
{
    std::cout << "stale temps\n";
    {
        Scratch s(Zstr("staletemps"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);

        //orphans from a run killed mid-copy, root and subfolder
        writeText(appendSeparator(destA) + Zstr("clip.mov.ffs_tmp"), "partial garbage");
        createDirectoryIfMissingRecursion(appendSeparator(destA) + Zstr("DCIM/100CANON"));
        writeText(appendSeparator(destA) + Zstr("DCIM/100CANON/IMG_0001.JPG.ffs_tmp"), "partial garbage");

        const JobReport job = runJob(makeOptions(s, { destA }));
        check(allVerified(job), "a stale temp does not block the copy");
        check(!itemExists(appendSeparator(destA) + Zstr("clip.mov.ffs_tmp")), "the stale temp at the root is cleaned up");
        check(!itemExists(appendSeparator(destA) + Zstr("DCIM/100CANON/IMG_0001.JPG.ffs_tmp")), "the stale temp in a subfolder is cleaned up");
        check(readText(appendSeparator(destA) + Zstr("clip.mov")) == pattern(250000), "the real file still lands at its final name");
        check(countTempsIn(destA) == 0, "no .ffs_tmp files remain after the run");
    }
    {
        //a stale temp sitting on the source side is not a file to copy
        Scratch s(Zstr("staletemps-src"));
        s.buildSource();
        writeText(appendSeparator(s.source) + Zstr("clip.mov.ffs_tmp"), "partial garbage");
        const Zstring destB = s.dest("DEST_B");
        createDirectoryIfMissingRecursion(destB);

        const JobReport job = runJob(makeOptions(s, { destB }));
        check(allVerified(job), "a source-side stale temp does not break the run");
        check(job.dests[0].files.size() == 4, "the source-side temp is not enumerated (got " +
              std::to_string(job.dests[0].files.size()) + ")");
        check(!itemExists(appendSeparator(destB) + Zstr("clip.mov.ffs_tmp")), "the source-side temp is not copied");
    }
}
//------------------------------------------------------------ broken headers
std::string validPng(size_t filler)
{
    std::string s;
    const auto add = [&s](const char* bytes, size_t n) { s.append(bytes, n); };
    add("\x89PNG\r\n\x1a\n", 8);          //signature
    add("\x00\x00\x00\x0d", 4);           //IHDR length 13
    add("IHDR", 4);
    s.append(13, '\0');                   //IHDR data (width/height/etc.)
    add("\x00\x00\x00\x00", 4);           //CRC (shape only - the checker does not verify it)
    s += pattern(filler);
    add("\x00\x00\x00\x00" "IEND" "\xae\x42\x60\x82", 12); //IEND chunk
    return s;
}

std::string validTiff(bool bigEndian)
{
    std::string s;
    if (bigEndian)
    {
        s.append("MM\0*", 4);
        s.append("\x00\x00\x00\x08", 4); //first IFD at offset 8
    }
    else
    {
        s.append("II*\0", 4);
        s.append("\x08\x00\x00\x00", 4);
    }
    s += pattern(200);
    return s;
}

void testBrokenHeaderUnits()
{
    std::cout << "broken header units\n";
    std::string reason;
    const auto broken = [&](const std::string& ext, const std::string& head,
                            const std::string& tail, uint64_t size)
    {
        reason.clear();
        return checkMediaHeader(ext, head.data(), head.size(), tail.data(), tail.size(), size, reason);
    };
    const std::string iend("\x00\x00\x00\x00" "IEND" "\xae\x42\x60\x82", 12);

    //JPEG: SOI + a marker
    check(!broken("jpg", validJpeg(100), "", 100), "a valid JPEG is not broken");
    check(broken("jpg", pattern(100), "", 100) && !reason.empty(), "random bytes as .jpg are broken");
    check(broken("jpg", std::string("\xff\xd8", 2), "", 2), "a 2-byte JPEG is broken");
    check(broken("jpeg", std::string("\xff\xd8\xff\x00", 4), "", 4), "SOI followed by padding is broken");

    //PNG: signature + IHDR + IEND
    check(!broken("png", validPng(100), iend, 45 + 100), "a valid PNG is not broken");
    check(broken("png", pattern(200), iend, 200) && !reason.empty(), "random bytes as .png are broken");
    check(broken("png", validPng(100), pattern(12), 45 + 100), "a PNG with a wrong tail is broken");
    check(broken("png", validPng(100), "", 45 + 100), "a PNG with no IEND is broken");
    check(broken("png", validPng(0).substr(0, 33), iend, 33), "a PNG too small for IEND is broken");

    //TIFF (and TIFF-based NEF/ARW): byte order, magic 42, IFD offset in range
    check(!broken("tif", validTiff(false), "", 208), "a valid little-endian TIFF is not broken");
    check(!broken("tiff", validTiff(true), "", 208), "a valid big-endian TIFF is not broken");
    check(!broken("nef", validTiff(false), "", 208), "a TIFF-based NEF is judged as TIFF");
    check(!broken("arw", validTiff(true), "", 208), "a TIFF-based ARW is judged as TIFF");
    check(broken("tif", pattern(100), "", 100), "random bytes as .tif are broken");
    check(broken("tif", std::string("II+\0\x08\x00\x00\x00", 8), "", 208), "a TIFF with magic 43 is broken");
    check(broken("tif", std::string("II*\0\x00\x04\x00\x00", 8), "", 208), "a TIFF with an IFD past the end is broken");

    //CR3: ftyp box branded crx
    check(!broken("cr3", validCr3(100), "", 100), "a valid CR3 is not broken");
    check(broken("cr3", pattern(100), "", 100), "random bytes as .cr3 are broken");
    check(broken("cr3", std::string("\x00\x00\x00\x18" "ftyp" "mp42", 12) + pattern(88), "", 100),
          "an ftyp box branded mp42 is not a CR3");

    //unknown types are never broken - only formats we can judge get judged
    check(!broken("mov", pattern(100), "", 100), "a .mov is never flagged");
    check(!broken("txt", pattern(100), "", 100), "a .txt is never flagged");
    check(!broken("", pattern(100), "", 100), "a file with no extension is never flagged");
}

//------------------------------------------------- broken files still copy
//A broken file is copied faithfully and hash-verified like any other, but it
//is flagged in the report and never counted as verified-good.
void testBrokenFiles()
{
    std::cout << "broken files\n";
    Scratch s(Zstr("broken"));
    createDirectoryIfMissingRecursion(appendSeparator(s.source) + Zstr("DCIM"));
    writeText(appendSeparator(s.source) + Zstr("DCIM/good.jpg"), validJpeg(5000));
    writeText(appendSeparator(s.source) + Zstr("DCIM/bad.jpg"),  pattern(5000));
    writeText(appendSeparator(s.source) + Zstr("DCIM/good.png"), validPng(5000));
    writeText(appendSeparator(s.source) + Zstr("DCIM/bad.png"),  pattern(5000));
    const std::string png = validPng(5000);
    writeText(appendSeparator(s.source) + Zstr("DCIM/cut.png"),  png.substr(0, png.size() - 12)); //no IEND
    writeText(appendSeparator(s.source) + Zstr("DCIM/good.tif"), validTiff(false));
    writeText(appendSeparator(s.source) + Zstr("DCIM/bad.tif"),  pattern(5000));
    writeText(appendSeparator(s.source) + Zstr("DCIM/good.cr3"), validCr3(7000));
    writeText(appendSeparator(s.source) + Zstr("DCIM/bad.cr3"),
              std::string("\x00\x00\x00\x18" "ftyp" "mp42", 12) + pattern(6988));
    writeText(appendSeparator(s.source) + Zstr("DCIM/good.nef"), validTiff(true));
    writeText(appendSeparator(s.source) + Zstr("DCIM/bad.arw"),  pattern(5000));

    const Zstring destA = s.dest("DEST_A");
    createDirectoryIfMissingRecursion(destA);

    const JobReport job = runJob(makeOptions(s, { destA }));
    //Damaged is copied faithfully but is never a verified-good copy, so the job
    //cannot claim everything arrived: a corrupt JPEG is exactly what the report
    //exists to catch before the card is reformatted.
    check(!allVerified(job), "a run with broken files does not verify");
    check(job.dests[0].files.size() == 11, "eleven files enumerated");
    check(job.dests[0].broken == 6, "six broken files (got " + std::to_string(job.dests[0].broken) + ")");
    check(job.dests[0].verified == 5, "five verified-good files (got " + std::to_string(job.dests[0].verified) + ")");
    check(job.dests[0].filesWritten == 11, "all eleven files are still written");
    check(job.dests[0].failed == 0 && job.dests[0].mismatched == 0, "no failures or mismatches");

    const auto find = [&](const char* name) -> const FileRecord*
    {
        for (const FileRecord& f : job.dests[0].files)
            if (zen::utfTo<std::string>(f.relPath).find(name) != std::string::npos)
                return &f;
        return nullptr;
    };

    for (const char* bad : { "bad.jpg", "bad.png", "cut.png", "bad.tif", "bad.cr3", "bad.arw" })
    {
        const FileRecord* rec = find(bad);
        check(rec != nullptr, std::string("record found for ") + bad);
        if (!rec)
            continue;
        check(rec->broken, std::string(bad) + " is flagged broken");
        check(!rec->brokenReason.empty(), std::string(bad) + " carries a reason");
        check(rec->state == FileState::verified, std::string(bad) + " still copies and verifies");
        check(!rec->hash.empty(), std::string(bad) + " still carries its hash");
        //faithful copy: the bytes on the destination are the source bytes
        const std::string srcBytes = readText(appendSeparator(s.source) + Zstr("DCIM/") + bad);
        check(readText(appendSeparator(destA) + Zstr("DCIM/") + bad) == srcBytes,
              std::string(bad) + " is copied byte-for-byte");
        check(rec->hash == hashOf(srcBytes), std::string(bad) + " hash matches the source bytes");
    }
    for (const char* good : { "good.jpg", "good.png", "good.tif", "good.cr3", "good.nef" })
    {
        const FileRecord* rec = find(good);
        check(rec != nullptr, std::string("record found for ") + good);
        if (rec)
            check(!rec->broken, std::string(good) + " is not flagged");
    }

    //the report carries the broken count and the per-file flag
    const Zstring csv  = appendSeparator(s.root) + Zstr("broken.csv");
    const Zstring json = appendSeparator(s.root) + Zstr("broken.json");
    check(writeCsv (job, csv ).empty(), "broken csv written");
    check(writeJson(job, json).empty(), "broken json written");
    const std::string csvText  = readText(csv);
    const std::string jsonText = readText(json);
    check(csvText.find(",broken,") != std::string::npos, "csv header names the broken column");
    check(csvText.find(",true,") != std::string::npos, "csv marks broken rows");
    check(jsonText.find("\"broken\": 6") != std::string::npos, "json counts six broken files");
    check(jsonText.find("\"broken\": true") != std::string::npos, "json flags broken files");
    check(jsonText.find("broken_reason") != std::string::npos, "json carries the broken reason");
}

//------------------------------------------------------------ hash cache
void testHashCache()
{
    std::cout << "hash cache\n";
    //--- the cache file itself: round trip, invalidation, version, garbage
    {
        const Zstring cachePath = appendSeparator(getTempFolderPath()) + Zstr("ffsoffload-test-cache-file");
        try { if (itemExists(cachePath)) removeFilePlain(cachePath); } catch (const FileError&) {}

        HashCache c1;
        c1.store(Zstr("DCIM/IMG_0001.JPG"), 5000, 1700000000, HashAlgo::xxh64, "abcdef0123456789");
        c1.store(Zstr("clip.mov"), 250000, 1700000001, HashAlgo::xxh64, "0123456789abcdef");
        check(c1.save(cachePath).empty(), "cache saves");

        HashCache c2;
        check(c2.load(cachePath), "cache loads");
        check(c2.size() == 2, "cache holds both entries");
        check(c2.lookup(Zstr("DCIM/IMG_0001.JPG"), 5000, 1700000000, HashAlgo::xxh64) ==
              std::optional<std::string>("abcdef0123456789"), "cache hit returns the stored hash");
        check(!c2.lookup(Zstr("DCIM/IMG_0001.JPG"), 5001, 1700000000, HashAlgo::xxh64),
              "a changed size invalidates the entry");
        check(!c2.lookup(Zstr("DCIM/IMG_0001.JPG"), 5000, 1700000001, HashAlgo::xxh64),
              "a changed mtime invalidates the entry");
        check(!c2.lookup(Zstr("DCIM/IMG_0001.JPG"), 5000, 1700000000, HashAlgo::sha1),
              "a different algorithm invalidates the entry");
        check(!c2.lookup(Zstr("DCIM/IMG_9999.JPG"), 5000, 1700000000, HashAlgo::xxh64),
              "an unknown path misses");

        //a wrong-version cache is ignored wholesale, not trusted
        writeText(cachePath, "ffsoffload-cache 99\nDCIM/IMG_0001.JPG\t5000\t1700000000\txxh64\tabcdef0123456789\n");
        HashCache c3;
        check(c3.load(cachePath), "a wrong-version cache still loads");
        check(c3.empty(), "a wrong-version cache is ignored wholesale");
        check(!c3.lookup(Zstr("DCIM/IMG_0001.JPG"), 5000, 1700000000, HashAlgo::xxh64),
              "a wrong-version entry is never trusted");

        //garbage is ignored, and one bad line never poisons the rest
        writeText(cachePath, "not a cache at all\n");
        HashCache c4;
        check(c4.load(cachePath), "garbage loads as an empty cache");
        check(c4.empty(), "garbage is ignored");
        writeText(cachePath, "ffsoffload-cache 1\nthis line has no tabs\nclip.mov\t250000\t1700000001\txxh64\t0123456789abcdef\n");
        HashCache c5;
        check(c5.load(cachePath), "a cache with one bad line still loads");
        check(c5.size() == 1, "the bad line is skipped, the good entry survives");
    }

    //--- engine level: first run populates, later runs confirm from the cache
    {
        Scratch s(Zstr("cache"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);

        const JobReport first = runJob(makeOptions(s, { destA }));
        check(allVerified(first), "cache first run verifies");
        check(first.dests[0].cacheHits == 0 && first.dests[0].cacheMisses == 0,
              "first run has no cache to consult");
        const Zstring cachePath = appendSeparator(destA) + Zstr(".ffsoffload-cache");
        check(itemExists(cachePath), "first run writes the cache file");
        const std::string cacheText = readText(cachePath);
        check(cacheText.rfind("ffsoffload-cache 1\n", 0) == 0, "cache file carries the version line");
        check(cacheText.find("clip.mov") != std::string::npos, "cache file lists the copied files");

        const JobReport second = runJob(makeOptions(s, { destA }));
        check(allVerified(second), "second run verifies");
        check(second.dests[0].skipped == 4, "second run skips all four");
        check(second.dests[0].cacheHits == 4, "second run confirms all four from the cache (got " +
              std::to_string(second.dests[0].cacheHits) + ")");
        check(second.dests[0].bytesCopied == 0, "second run writes zero bytes");
        bool allConfirmed = true;
        for (const FileRecord& f : second.dests[0].files)
            allConfirmed = allConfirmed && f.hashConfirmed && !f.hash.empty();
        check(allConfirmed, "every skipped file is hash-confirmed with its digest");

        //re-verify hashes every destination file fresh: the cache is never
        //consulted there, so a content change that keeps size+date is caught
        Options o = makeOptions(s, { destA });
        o.reverifyOnly = true;
        const JobReport reverify = runJob(o);
        check(allVerified(reverify), "re-verify verifies");
        check(reverify.dests[0].verified == 4, "re-verify verifies all four");
        check(reverify.dests[0].cacheHits == 0, "re-verify takes no destination hash from the cache (got " +
              std::to_string(reverify.dests[0].cacheHits) + ")");
        check(reverify.dests[0].cacheMisses == 4, "re-verify re-hashes all four (got " +
              std::to_string(reverify.dests[0].cacheMisses) + ")");
    }

    //--- invalidation by mtime: one moved file re-copies, the rest still hit
    {
        Scratch s(Zstr("cache-mtime"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);
        runJob(makeOptions(s, { destA }));

        const Zstring victim = appendSeparator(destA) + Zstr("DCIM/100CANON/IMG_0001.JPG");
        const auto then = std::filesystem::last_write_time(
            std::filesystem::path(zen::utfTo<std::string>(victim)));
        std::filesystem::last_write_time(
            std::filesystem::path(zen::utfTo<std::string>(victim)), then + std::chrono::hours(1));

        const JobReport job = runJob(makeOptions(s, { destA }));
        check(allVerified(job), "mtime run verifies");
        check(job.dests[0].skipped == 3, "three files still skip after one mtime moved (got " +
              std::to_string(job.dests[0].skipped) + ")");
        check(job.dests[0].cacheHits == 3, "three cache hits after one mtime moved (got " +
              std::to_string(job.dests[0].cacheHits) + ")");
        check(job.dests[0].filesWritten == 1, "the moved file is re-copied");
        bool found = false;
        for (const FileRecord& f : job.dests[0].files)
            if (zen::utfTo<std::string>(f.relPath).find("IMG_0001-1.JPG") != std::string::npos)
                found = true;
        check(found, "the moved file lands as IMG_0001-1.JPG, the original is never overwritten");
    }

    //--- invalidation by size: one grown file re-copies, the rest still hit
    {
        Scratch s(Zstr("cache-size"));
        s.buildSource();
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);
        runJob(makeOptions(s, { destA }));

        writeText(appendSeparator(s.source) + Zstr("DCIM/100CANON/IMG_0002.JPG"), validJpeg(9999));

        const JobReport job = runJob(makeOptions(s, { destA }));
        check(allVerified(job), "size run verifies");
        check(job.dests[0].skipped == 3, "three files still skip after one size changed");
        check(job.dests[0].cacheHits == 3, "three cache hits after one size changed");
        check(job.dests[0].filesWritten == 1, "the grown file is re-copied");
    }

    //--- the cache file itself is never copied, on either side
    {
        Scratch s(Zstr("cache-excluded"));
        s.buildSource();
        writeText(appendSeparator(s.source) + Zstr(".ffsoffload-cache"), "junk planted on the card");
        const Zstring destA = s.dest("DEST_A");
        createDirectoryIfMissingRecursion(destA);
        writeText(appendSeparator(destA) + Zstr(".ffsoffload-cache"), "garbage from a crashed run");

        const JobReport job = runJob(makeOptions(s, { destA }));
        check(allVerified(job), "cache-exclusion run verifies");
        check(job.dests[0].files.size() == 4, "the card-side cache file is not enumerated (got " +
              std::to_string(job.dests[0].files.size()) + ")");
        check(job.dests[0].verified == 4, "all four real files verify");
        const std::string cacheText = readText(appendSeparator(destA) + Zstr(".ffsoffload-cache"));
        check(cacheText.rfind("ffsoffload-cache 1\n", 0) == 0,
              "the garbage destination cache is rebuilt as a real cache");
        check(cacheText.find("junk planted") == std::string::npos,
              "the card-side cache content is not copied into the destination cache");
    }
}


//A destination that stops early leaves a prefix of records. A prefix with no
//failures in it must not read as a completed job: that is how a half-copied card
//gets reformatted.
void testTruncatedReport()
{
    std::cout << "truncated report\n";

    FileRecord rec;
    rec.relPath = Zstr("DCIM/IMG_0001.JPG");
    rec.size = 4096;
    rec.state = FileState::verified;

    JobReport job;
    DestReport d;
    d.destRoot = Zstr("/tmp/does-not-matter");
    d.expectedFiles = 3;
    d.files.push_back(rec);
    tally(d);
    job.dests.push_back(d);

    check(d.failed == 0 && d.mismatched == 0, "the short report has no failures in it");
    check(!allVerified(job), "one record of three does not verify");

    //every record present, nothing failed: now it verifies
    d.files.push_back(rec);
    d.files.push_back(rec);
    tally(d);
    job.dests[0] = d;
    check(allVerified(job), "a complete report verifies");

    //a broken file blocks the claim even though nothing failed
    d.files[0].broken = true;
    tally(d);
    job.dests[0] = d;
    check(!allVerified(job), "a broken file blocks a complete report");

    //a failed source scan is never a success, whatever the destinations hold
    d.files[0].broken = false;
    tally(d);
    job.dests[0] = d;
    job.error = "cannot read the source folder";
    check(!allVerified(job), "a job-level error blocks the claim");
}


//--------------------------------------------------------------- the Skip field
//Skip is the one control that can silently lose a wedding: a pattern that matches
//too much leaves the RAW behind while the run still says it verified. So every
//check here is either "the fit check and the copy agree" or "nothing is lost".
void testExcludeFilter()
{
    std::cout << "exclude filter\n";
    Scratch s(Zstr("filter"));
    s.buildSource();
    writeText(appendSeparator(s.source) + Zstr("DCIM/100CANON/IMG_0001.LRV"), pattern(4000));
    createDirectoryIfMissingRecursion(appendSeparator(s.source) + Zstr("MISC/thumbs"));
    writeText(appendSeparator(s.source) + Zstr("MISC/notes.txt"), "notes");
    writeText(appendSeparator(s.source) + Zstr("MISC/thumbs/pick.jpg"), validJpeg(3000));

    const Zstring destAll  = s.dest("DEST_ALL");
    const Zstring destSome = s.dest("DEST_SOME");
    createDirectoryIfMissingRecursion(destAll);
    createDirectoryIfMissingRecursion(destSome);

    //nothing filtered: proxies and MISC come along, as they do today
    const JobReport allJob = runJob(makeOptions(s, { destAll }));
    check(allVerified(allJob), "an unfiltered run verifies");
    check(allJob.dests[0].files.size() == 7,
          "the unfiltered run carries the proxy and MISC (got " +
          std::to_string(allJob.dests[0].files.size()) + ")");

    //filtered: the proxy and the whole MISC subtree go, the CR3 beside the proxy stays
    Options some = makeOptions(s, { destSome });
    some.exclude = "*.LRV | MISC/";
    const JobReport someJob = runJob(some);
    check(allVerified(someJob), "a filtered run verifies what it did copy");
    check(someJob.dests[0].files.size() == 4,
          "a filtered run carries only the four real clips (got " +
          std::to_string(someJob.dests[0].files.size()) + ")");
    check(someJob.exclude == "*.LRV | MISC/",
          "the report records the patterns it applied (got \"" + someJob.exclude + "\")");

    bool sawProxy = false;
    bool sawMisc  = false;
    bool sawCr3   = false;
    for (const FileRecord& f : someJob.dests[0].files)
    {
        const std::string rel = zen::utfTo<std::string>(f.relPath);
        if (rel.find(".LRV") != std::string::npos) sawProxy = true;
        if (rel.rfind("MISC/", 0) == 0)              sawMisc  = true;
        if (rel == "DCIM/100CANON/IMG_0003.CR3")     sawCr3   = true;
    }
    check(!sawProxy, "the .LRV proxy is not in the filtered report");
    check(!sawMisc, "nothing under MISC/ is in the filtered report");
    check(sawCr3, "the CR3 beside the excluded proxy is still copied");
    check(!itemExists(appendSeparator(destSome) + Zstr("DCIM/100CANON/IMG_0001.LRV")),
          "no excluded file is written to the destination");
    check(!itemExists(appendSeparator(destSome) + Zstr("MISC")),
          "an excluded folder is never created on the destination");

    //the fit check must count what the copy carries, not what the card holds
    uint64_t filteredBytes = 0;
    size_t   filteredFiles = 0;
    std::string filterErr;
    check(measureSource(s.source, "*.LRV | MISC/", filteredBytes, filteredFiles, filterErr),
          "measureSource accepts a filter");

    uint64_t copiedBytes = 0;
    size_t   copiedFiles = 0;
    for (const FileRecord& f : someJob.dests[0].files)
        if (f.state == FileState::verified)
        {
            copiedBytes += f.size;
            ++copiedFiles;
        }
    check(filteredFiles == copiedFiles, "the fit check counts the filtered file count (" +
          std::to_string(filteredFiles) + " vs " + std::to_string(copiedFiles) + ")");
    check(filteredBytes == copiedBytes, "the fit check counts the filtered bytes (" +
          std::to_string(filteredBytes) + " vs " + std::to_string(copiedBytes) + ")");

    uint64_t allBytes = 0;
    size_t   allFiles = 0;
    std::string allErr;
    check(measureSource(s.source, "", allBytes, allFiles, allErr), "the unfiltered measurement succeeds");
    check(allFiles > filteredFiles, "the filter really lowers the fit check's count (" +
          std::to_string(filteredFiles) + " vs " + std::to_string(allFiles) + ")");

    //a stray `*` excludes the entire card: that must be an error, never a
    //successful run of nothing, or the card gets formatted on the strength of it
    const Zstring destNone = s.dest("DEST_NONE");
    createDirectoryIfMissingRecursion(destNone);
    Options none = makeOptions(s, { destNone });
    none.exclude = "*";
    const JobReport noneJob = runJob(none);
    check(!noneJob.error.empty(), "a filter that excludes everything is an error");
    check(!allVerified(noneJob), "an all-excluded card does not verify");
    check(noneJob.dests.empty() || noneJob.dests[0].files.empty(),
          "an all-excluded card writes no file records");
}


int main()
{
    std::cout << "ffsoffload tests\n\n";
    testHashing();
    testCopyAndVerify();
    testDuplicateDetect();
    testCorruptionDetected();
    testNameClash();
    testOneDestinationFails();
    testCardJunkSkipped();
    testReports();
    testDryRun();
    testPauseAndCancel();
    testHashAlgorithmReported();
    testMeasureSource();
    testScanFailure();
    testTempPublish();
    testStaleTemps();
    testBrokenHeaderUnits();
    testBrokenFiles();
    testHashCache();
    testTruncatedReport();
    testExcludeFilter();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    if (failures > 0)
        std::cout << failures << " FAILED\n";
    return failures == 0 ? 0 : 1;
}
