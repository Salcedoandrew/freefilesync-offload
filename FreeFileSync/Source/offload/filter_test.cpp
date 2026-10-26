// *****************************************************************************
// * ffsoffload filter tests.                                                   *
// *                                                                            *
// * The matcher is the one part of the engine with no zen/glib/wx dependency,  *
// * so it is built and run on its own:                                          *
// *                                                                            *
// *   make -f Makefile.macos filter-test                                       *
// *                                                                            *
// * Every case here is one that would silently cost a photographer files: an  *
// * exclusion that also eats the CR3 beside it, a folder pattern that only     *
// * works at the top level, or a case-sensitive match on a card that writes    *
// * `IMG_1.THM` next to `img_1.thm`.                                           *
// *****************************************************************************

#include "filter.h"

#include <iostream>
#include <string>

namespace
{
int checks  = 0;
int failures = 0;

void check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::cout << "  FAIL  " << what << "\n";
    }
}

bool excludes(const std::string& text, const std::string& path)
{
    return offload::ExcludeFilter::parse(text).matches(path);
}
}

int main()
{
    //-- nothing in, nothing out -------------------------------------------------
    check(!excludes("", "DCIM/IMG_0001.CR3"), "an empty filter excludes nothing");
    check(!excludes("   ", "DCIM/IMG_0001.CR3"), "whitespace is an empty filter");
    check(!excludes("|| |", "DCIM/IMG_0001.CR3"), "empty alternatives are ignored");
    check(offload::ExcludeFilter::parse("").empty(), "an empty text parses to an empty filter");

    //-- the name form, at any depth --------------------------------------------
    check(excludes("*.lrv", "DCIM/100CANON/IMG_0001.LRV"), "*.lrv finds a proxy three folders down");
    check(!excludes("*.lrv", "DCIM/100CANON/IMG_0001.CR3"), "*.lrv leaves the CR3 beside it alone");
    check(excludes("*.LRV", "DCIM/100CANON/IMG_0001.lrv"), "matching is case-insensitive (upper pattern)");
    check(excludes("*.lrv", "DCIM/100CANON/IMG_0001.LRV"), "matching is case-insensitive (lower pattern)");
    check(excludes("IMG_000?.CR3", "DCIM/IMG_0001.CR3"), "? matches exactly one character");
    check(!excludes("IMG_000?.CR3", "DCIM/IMG_00010.CR3"), "? does not match two characters");
    check(excludes("*.LRV | *.THM", "DCIM/A.THM"), "the second alternative applies");
    check(excludes("*.LRV | *.THM", "DCIM/A.LRV"), "the first alternative applies");
    check(!excludes("*.LRV | *.THM", "DCIM/A.CR3"), "neither alternative applies to a CR3");

    //-- the path form ----------------------------------------------------------
    check(excludes("DCIM/private", "DCIM/private"), "a path pattern matches that path");
    check(!excludes("DCIM/private", "OTHER/DCIM/private"), "a path pattern is anchored at the source root");
    check(excludes("2024/*.JPG", "2024/a.JPG"), "a path pattern with * matches one level");
    check(!excludes("2024/*.JPG", "2024/sub/a.JPG"), "a wildcard never crosses a /");
    check(!excludes("2024/*.JPG", "2023/a.JPG"), "a path pattern does not match another year");

    //-- the folder form --------------------------------------------------------
    check(excludes("MISC/", "MISC"), "a folder pattern prunes the folder itself");
    check(excludes("MISC/", "MISC/notes.txt"), "a folder pattern covers a file directly inside");
    check(excludes("MISC/", "MISC/deep/nested/file.mov"), "a folder pattern covers a nested file");
    check(!excludes("MISC/", "MISC2/notes.txt"), "a folder pattern does not match a longer name");
    check(excludes("/DCIM/CACHE/", "DCIM/CACHE/thumb.db"), "a leading slash means the same as none");
    check(excludes("DCIM/100CANON/", "DCIM/100CANON"), "a multi-level folder pattern prunes the folder");
    check(!excludes("DCIM/100CANON/", "DCIM/101CANON"), "a multi-level folder pattern is exact");

    //-- what it says it did ----------------------------------------------------
    check(offload::ExcludeFilter::parse("  /DCIM/ | *.THM ").text() == "DCIM/ | *.THM",
          "text() normalises the patterns for the report");
    check(offload::ExcludeFilter::parse("a|b|c").size() == 3, "three patterns are three patterns");

    //-- nothing pathological ---------------------------------------------------
    check(!excludes("*a*a*a*a*a*a*a*a*b", std::string(400, 'a')),
          "a star-heavy pattern terminates and does not match");
    check(excludes("*a*a*b", std::string(200, 'a') + "b"), "a star-heavy pattern still matches when it should");

    std::cout << checks - failures << "/" << checks << " filter checks passed\n";
    return failures == 0 ? 0 : 1;
}
