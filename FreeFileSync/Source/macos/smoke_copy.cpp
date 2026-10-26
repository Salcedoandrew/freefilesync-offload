// *****************************************************************************
// * macOS port smoke test: does the AFS engine actually copy and verify on     *
// * arm64 Darwin? Reads a source file, writes a destination through the same   *
// * AbstractFileSystem the Offload mode will use, then byte-compares the two   *
// * with fff::filesHaveSameContent() - the exact verification path FreeFileSync *
// * already uses for "verify copied files".                                    *
// *****************************************************************************

#include <iostream>
#include <chrono>

#include "afs/native.h"
#include "afs/abstract.h"
#include "base/binary.h"

using namespace fff;
using AFS = AbstractFileSystem;

static const zen::IoCallback noProgress = [](int64_t) {};

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        std::cerr << "usage: smoke_copy <source> <destination>      copy then verify\n"
                     "       smoke_copy --verify <a> <b>            verify only\n";
        return 2;
    }
    const bool verifyOnly = (argc == 4 && std::string(argv[1]) == "--verify");
    const Zstring srcPath = argv[verifyOnly ? 2 : 1];
    const Zstring dstPath = argv[verifyOnly ? 3 : 2];

    try
    {
        const AbstractPath src = createItemPathNative(srcPath);
        const AbstractPath dst = createItemPathNative(dstPath);

        const auto t0 = std::chrono::steady_clock::now();

        uint64_t bytes = 0;
        if (!verifyOnly)
        {
            const auto in  = AFS::getInputStream(src);                                  //throw FileError
            const auto out = AFS::getOutputStream(dst, std::nullopt, std::nullopt);      //throw FileError

            const size_t blockSize = in->getBlockSize();
            std::unique_ptr<char[]> buf(new char[blockSize]);

            for (;;)
            {
                const size_t n = in->tryRead(buf.get(), blockSize, noProgress); //0 == EOF
                if (n == 0)
                    break;
                size_t written = 0;
                while (written < n)
                    written += out->tryWrite(buf.get() + written, n - written, noProgress);
                bytes += n;
            }
            out->finalize(noProgress); //publishes via .ffs_tmp + rename
        }

        const bool same = filesHaveSameContent(src, dst, noProgress);
        const auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        std::cout << "copied  " << bytes << " bytes in " << dt << " s ("
                  << (dt > 0 ? static_cast<double>(bytes) / dt / (1024 * 1024) : 0.0) << " MiB/s)\n";
        std::cout << "verify  " << (same ? "IDENTICAL" : "MISMATCH") << "\n";
        return same ? 0 : 1;
    }
    catch (const zen::FileError& e)
    {
        std::cerr << "FileError: " << zen::utfTo<std::string>(e.toString()) << "\n";
        return 3;
    }
    catch (const std::exception& e)
    {
        std::cerr << "exception: " << e.what() << "\n";
        return 4;
    }
}
