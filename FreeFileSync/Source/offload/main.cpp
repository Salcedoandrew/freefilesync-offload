// *****************************************************************************
// * ffsoffload - verified media offload for camera cards.                      *
// *                                                                            *
// *   ffsoffload --source /Volumes/CARD --dest /Volumes/A --dest /Volumes/B    *
// *                                                                            *
// * Copies to every destination in parallel, hashes with XXH64 while writing,  *
// * reads every copy back and compares, then writes an MHL per destination     *
// * plus a CSV/JSON log. The source is never modified.                         *
// *****************************************************************************

#include "engine.h"
#include "filter.h"
#include "report.h"

#include <zen/file_access.h>
#include <zen/file_path.h>
#include <zen/utf.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace offload;

namespace
{
//Workers read these; the signal handler only sets the plain flag below, because
//only `volatile sig_atomic_t` is safe to touch from a handler.
std::atomic<bool> g_pause{ false };
std::atomic<bool> g_cancel{ false };
std::atomic<bool> g_done{ false };
volatile sig_atomic_t g_pauseSignal = 0;

//SIGUSR1 toggles pause, SIGINT/SIGTERM cancel after the file in flight.
extern "C" void onSignal(int sig)
{
    if (sig == SIGUSR1)
        g_pauseSignal = !g_pauseSignal;
    else
        g_cancel.store(true);
}

void installSignalHandlers()
{
    std::signal(SIGUSR1, onSignal);
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);
}

//Turns the signal-flag into the atomic the engine polls, once per 50 ms.
std::thread startSignalBridge()
{
    return std::thread([]()
    {
        while (!g_done.load())
        {
            static sig_atomic_t seen = 0;
            if (g_pauseSignal != seen)
            {
                seen = g_pauseSignal;
                g_pause.store(seen != 0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void printUsage()
{
    std::cout <<
        "ffsoffload - verified offload of a camera card to one or more destinations\n"
        "\n"
        "  ffsoffload --source DIR --dest DIR [--dest DIR ...] [options]\n"
        "\n"
        "Options:\n"
        "  --source DIR        folder or card to read from (never modified)\n"
        "  --dest DIR          destination root (repeat for every copy)\n"
        "  --verify MODE       'hash' (default, read back and compare hashes)\n"
        "                      or 'size' (compare sizes only, much weaker)\n"
        "  --hash ALGO         xxh64 (default) | md5 | sha1 - used for verification\n"
        "                      and reported in the CSV/JSON/MHL\n"
        "  --exclude PATTERNS  skip matching files and folders, '|'-separated:\n"
        "                      \"*.LRV | *.THM | MISC/\" (* and ? wildcards, a\n"
        "                      trailing / prunes a folder; case-insensitive)\n"
        "  --reverify          do not copy; hash what is already on the destinations\n"
        "                      and report mismatches\n"
        "  --dry-run           plan and report, write nothing\n"
        "  --report DIR        also write report.csv and report.json here\n"
        "  --mhl               write <destination>/.ffsoffload.mhl at each destination root\n"
        "  --quiet             only print the summary and failures\n"
        "  --json              print the summary as JSON\n"
        "\n"
        "Signals: SIGUSR1 pauses and resumes, SIGINT/SIGTERM cancel after the\n"
        "         file in flight (already-published files stay verified)\n"
        "\n"
        "Exit codes: 0 all copies verified, 1 mismatch or failure, 2 bad usage\n";
}

struct Args
{
    Zstring source;
    std::vector<Zstring> dests;
    VerifyMode verify = VerifyMode::sourceAndDest;
    bool reverify = false;
    bool dryRun   = false;
    bool writeMhl = false;
    bool quiet    = false;
    bool json     = false;
    HashAlgo hash = HashAlgo::xxh64;
    std::string exclude;
    Zstring reportDir;
};

bool parseArgs(int argc, char** argv, Args& out, std::string& err)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&](Zstring& target) -> bool
        {
            if (i + 1 >= argc) { err = "missing value for " + a; return false; }
            target = argv[++i];
            return true;
        };

        if (a == "--source")        { if (!next(out.source)) return false; }
        else if (a == "--dest")     { Zstring d; if (!next(d)) return false; out.dests.push_back(d); }
        else if (a == "--report")   { if (!next(out.reportDir)) return false; }
        else if (a == "--verify")
        {
            Zstring mode;
            if (!next(mode)) return false;
            if (mode == Zstr("hash"))      out.verify = VerifyMode::sourceAndDest;
            else if (mode == Zstr("size")) out.verify = VerifyMode::sizeOnly;
            else { err = "unknown --verify mode: " + zen::utfTo<std::string>(mode); return false; }
        }
        else if (a == "--hash")
        {
            Zstring algo;
            if (!next(algo)) return false;
            if (!parseHashAlgo(zen::utfTo<std::string>(algo), out.hash))
            {
                err = "unknown --hash algorithm: " + zen::utfTo<std::string>(algo) + " (xxh64|md5|sha1)";
                return false;
            }
        }
        else if (a == "--reverify") out.reverify = true;
        else if (a == "--dry-run")  out.dryRun   = true;
        else if (a == "--exclude")
        {
            Zstring patterns;
            if (!next(patterns)) return false;
            out.exclude = zen::utfTo<std::string>(patterns);
        }
        else if (a == "--mhl")      out.writeMhl = true;
        else if (a == "--quiet")    out.quiet    = true;
        else if (a == "--json")     out.json     = true;
        else if (a == "-h" || a == "--help") { printUsage(); std::exit(0); }
        else { err = "unknown option: " + a; return false; }
    }
    if (out.source.empty())      { err = "--source is required";  return false; }
    if (out.dests.empty())       { err = "at least one --dest is required"; return false; }
    if (out.reverify && out.dryRun) { err = "--reverify and --dry-run contradict each other"; return false; }
    return true;
}

std::string destLabel(const Zstring& path)
{
    std::string s = zen::utfTo<std::string>(path);
    while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    const size_t sep = s.find_last_of("/\\");
    return sep == std::string::npos ? s : s.substr(sep + 1);
}
} //namespace


int main(int argc, char** argv)
{
    Args args;
    std::string err;
    if (!parseArgs(argc, argv, args, err))
    {
        std::cerr << "ffsoffload: " << err << "\n\n";
        printUsage();
        return 2;
    }

    //fail early on an unusable source instead of after a scan
    try
    {
        if (!zen::itemExists(args.source))
        {
            std::cerr << "ffsoffload: source does not exist: " << zen::utfTo<std::string>(args.source) << "\n";
            return 2;
        }
    }
    catch (const zen::FileError& e)
    {
        std::cerr << "ffsoffload: " << zen::utfTo<std::string>(e.toString()) << "\n";
        return 2;
    }

    Options opts;
    opts.sourceRoot = args.source;
    opts.destRoots  = args.dests;
    opts.verify     = args.verify;
    opts.hashAlgo   = args.hash;
    opts.dryRun     = args.dryRun;
    opts.exclude    = args.exclude;
    opts.reverifyOnly = args.reverify;
    opts.cancel     = &g_cancel;
    opts.pause      = &g_pause;

    installSignalHandlers();
    std::thread signalBridge = startSignalBridge();

    std::thread pauseMonitor;
    if (!args.quiet)
        pauseMonitor = std::thread([]() //printing lives on its own thread
    {
            bool announced = false;
            for (;;)
            {
                if (g_done.load())
                    return;
                const bool paused = g_pause.load();
                if (paused != announced)
                {
                    announced = paused;
                    std::cout << (paused ? "\n-- paused (SIGUSR1 to resume) --\n"
                                         : "\n-- resumed --\n") << std::flush;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

    const auto start = std::chrono::steady_clock::now();

    if (!args.quiet)
    {
        std::cout << "source  " << zen::utfTo<std::string>(args.source) << "\n";
        for (const Zstring& d : args.dests)
            std::cout << "dest    " << zen::utfTo<std::string>(d) << "\n";
        std::cout << "verify  " << getVerifyModeName(args.verify)
                  << (args.reverify ? " (re-verify only)" : "") << "\n";
        std::cout << "hash    " << getAlgoName(args.hash) << "\n";
        if (!args.exclude.empty())
            std::cout << "skip    " << ExcludeFilter::parse(args.exclude).text() << "\n";
        std::cout << "\n";
    }

    size_t filesSeen = 0;
    opts.onFileDone = [&](const Zstring& destRoot, const FileRecord& f)
    {
        ++filesSeen;
        if (args.quiet)
            return;
        std::cout << "  " << destLabel(destRoot) << "  " << getStateName(f.state)
                  << "  " << humanBytes(f.size) << "  " << zen::utfTo<std::string>(f.relPath);
        if (f.broken)
            std::cout << "  [BROKEN: " << f.brokenReason << " - copied anyway]";
        if (!f.error.empty())
            std::cout << "  [" << f.error << "]";
        std::cout << "\n";
    };

    const JobReport job = runJob(opts);
    g_done.store(true);
    if (pauseMonitor.joinable())
        pauseMonitor.join();
    if (signalBridge.joinable())
        signalBridge.join();

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (args.json)
    {
        //One line a script can read without a parser for prose: the totals it would
        //otherwise have to add up itself, the bytes that actually moved, where the
        //report went, and the skip rules that shaped all of it.
        uint64_t bytesCopied = 0;
        size_t verified = 0;
        size_t written = 0;
        size_t skipped = 0;
        size_t mismatched = 0;
        size_t failed = 0;
        size_t broken = 0;
        for (const DestReport& d : job.dests)
        {
            bytesCopied += d.bytesCopied;
            verified    += d.verified;
            written     += d.filesWritten;
            skipped     += d.skipped;
            mismatched  += d.mismatched;
            failed      += d.failed;
            broken      += d.broken;
        }

        std::cout << "{\"ok\": " << (allVerified(job) ? "true" : "false")
                  << ", \"source\": \"" << jsonEscape(zen::utfTo<std::string>(job.sourceRoot)) << "\""
                  << ", \"files\": " << totalFiles(job)
                  << ", \"verified\": " << verified
                  << ", \"written\": " << written
                  << ", \"skipped\": " << skipped
                  << ", \"mismatch\": " << mismatched
                  << ", \"failed\": " << failed
                  << ", \"broken\": " << broken
                  << ", \"bytes\": " << bytesCopied
                  << ", \"seconds\": " << elapsed
                  << ", \"verify\": \"" << jsonEscape(job.verifyMode) << "\""
                  << ", \"hash\": \"" << getAlgoName(job.hashAlgo) << "\""
                  << ", \"exclude\": \"" << jsonEscape(job.exclude) << "\""
                  << ", \"dry_run\": " << (job.dryRun ? "true" : "false")
                  << ", \"report\": \"" << jsonEscape(args.reportDir.empty()
                                                     ? std::string()
                                                     : zen::utfTo<std::string>(args.reportDir)) << "\""
                  << ", \"error\": \"" << jsonEscape(job.error) << "\"";
        for (const DestReport& d : job.dests)
            std::cout << ", \"" << jsonEscape(destLabel(d.destRoot)) << "\": {\"verified\": " << d.verified
                      << ", \"written\": " << d.filesWritten << ", \"skipped\": " << d.skipped
                      << ", \"mismatch\": " << d.mismatched << ", \"failed\": " << d.failed
                      << ", \"broken\": " << d.broken
                      << ", \"cache_hits\": " << d.cacheHits << "}";
        std::cout << "}\n";
    }
    else
    {
        std::cout << "\n";
        for (const DestReport& d : job.dests)
        {
            std::cout << destLabel(d.destRoot) << ": "
                      << d.verified << " verified, "
                      << d.filesWritten << " written, "
                      << d.skipped << " already present, "
                      << d.mismatched << " mismatch, "
                      << d.failed << " failed, "
                      << d.broken << " broken  ("
                      << humanBytes(d.bytesCopied) << " written, "
                      << d.cacheHits << " cache hits)\n";

            for (const FileRecord& f : d.files)
                if (isFailure(f.state))
                    std::cout << "  FAIL " << zen::utfTo<std::string>(f.relPath)
                              << "  [" << getStateName(f.state) << "] " << f.error << "\n";
            //Broken is a warning, not a failure: the file was still copied
            //faithfully, so the job can pass with broken files in it.
            for (const FileRecord& f : d.files)
                if (f.broken)
                    std::cout << "  WARNING " << zen::utfTo<std::string>(f.relPath)
                              << " is broken: " << f.brokenReason << " (copied anyway)\n";
            if (!d.cacheError.empty())
                std::cout << "  WARNING hash cache could not be saved: " << d.cacheError << "\n";
        }
        //A failed source scan runs no destination, so there are no FAIL lines
        //above at all: print the reason here instead of a bare "FAILED".
        if (!job.error.empty())
            std::cout << "  ERROR: " << job.error << "\n";

        //Say which kind of failure this was. "See failures above" is a lie when the
        //only problem is a damaged source file (the file was copied, so no FAIL line
        //exists) or a destination that stopped early (there is no line at all).
        size_t failedTotal = 0;
        size_t brokenTotal = 0;
        for (const DestReport& d : job.dests)
        {
            failedTotal += d.failed + d.mismatched;
            brokenTotal += d.broken;
        }

        if (allVerified(job))
            std::cout << "OK: every file verified\n";
        else if (failedTotal > 0)
            std::cout << "FAILED: see failures above\n";
        else if (brokenTotal > 0)
            std::cout << "FAILED: " << brokenTotal
                      << " damaged source file(s) were copied but never counted as verified\n";
        else
            std::cout << "FAILED: a destination stopped early, so the report is incomplete\n";
        std::cout << "elapsed " << elapsed << " s for " << totalFiles(job) << " file copies\n";
    }

    //artefacts
    std::vector<std::string> reportErrors;
    if (args.reportDir.empty())
    {
        if (!args.dryRun)
            for (const DestReport& d : job.dests)
                if (args.writeMhl)
                    if (const std::string e = writeMhl(job, d, zen::appendSeparator(d.destRoot) + Zstr(".ffsoffload.mhl")); !e.empty())
                        reportErrors.push_back(e);
    }
    else
    {
        try { zen::createDirectoryIfMissingRecursion(args.reportDir); } catch (const zen::FileError&) {}
        const std::string e1 = writeCsv (job, zen::appendSeparator(args.reportDir) + Zstr("report.csv"));
        const std::string e2 = writeJson(job, zen::appendSeparator(args.reportDir) + Zstr("report.json"));
        if (!e1.empty()) reportErrors.push_back(e1);
        if (!e2.empty()) reportErrors.push_back(e2);
        if (args.writeMhl)
            for (const DestReport& d : job.dests)
                if (const std::string e = writeMhl(job, d, zen::appendSeparator(d.destRoot) + Zstr(".ffsoffload.mhl")); !e.empty())
                    reportErrors.push_back(e);
    }

    for (const std::string& e : reportErrors)
        std::cerr << "warning: could not write report: " << e << "\n";

    return allVerified(job) ? 0 : 1;
}
