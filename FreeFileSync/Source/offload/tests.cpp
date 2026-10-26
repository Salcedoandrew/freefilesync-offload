// *****************************************************************************
// * ffsoffload tests.                                                          *
// *                                                                            *
// * Hash vectors are ground truth from `xxhsum -H1` (XXH64, seed 0), so a      *
// * broken hasher cannot pass silently. The filesystem tests assert the things *
// * that would actually lose a wedding: nothing is lost, corruption is caught, *
// * and the card is never written to.                                          *
// *****************************************************************************

#include "engine.h"
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
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0001.JPG"), pattern(5000));
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0002.JPG"), pattern(12345));
        writeText(appendSeparator(source) + Zstr("DCIM/100CANON/IMG_0003.CR3"), pattern(70000));
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

    //flip bytes inside the copy, leaving name, size and mtime intact - the exact
    //failure a bad cable or a dying drive produces
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
    check(measureSource(s.source, measuredBytes, measuredFiles, err), "measureSource succeeds");
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
    check(!measureSource(appendSeparator(s.source) + Zstr("no-such-folder"),
                         missingBytes, missingFiles, missingErr),
          "measuring a folder that does not exist fails");
    check(!missingErr.empty(), "failed measurement carries a reason");
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

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    if (failures > 0)
        std::cout << failures << " FAILED\n";
    return failures == 0 ? 0 : 1;
}
