// *****************************************************************************
// * Offload mode for FreeFileSync - the Skip field's matcher.                  *
// *                                                                            *
// * Deliberately small, because a filter that quietly copies the wrong subset  *
// * of a wedding card is worse than no filter at all. The whole syntax is:     *
// *                                                                            *
// *   *.LRV | *.THM | MISC/                                                    *
// *                                                                            *
// *   `|`          separates patterns                                          *
// *   `*`          any run of characters, never across a `/`                   *
// *   `?`          exactly one character, never `/`                            *
// *   `name`       matched against the file or folder name, at any depth        *
// *   `dir/name`   contains a `/`, so it is matched against the relative path   *
// *   `name/`      a folder and everything under it; the walk does not enter    *
// *                                                                            *
// * Matching is case-insensitive on every platform: cards mix `IMG_1.THM` and   *
// * `img_1.thm` freely, and a filter that only works on one of them is a trap.  *
// * There is no way to escape a `*` - a file literally named `a*b` cannot be     *
// * excluded - and that is stated here rather than half-supported.              *
// *                                                                            *
// * This header includes nothing from zen on purpose: the matcher is the one    *
// * piece of the engine that can be built and tested without wxWidgets, glib    *
// * or macOS, so it is built and tested on its own (see filter_test.cpp).       *
// *****************************************************************************
#pragma once

#include <string>
#include <vector>

namespace offload
{
class ExcludeFilter
{
public:
    //Parses `text`. Never throws and never fails: a pattern it does not
    //understand is a pattern that does not match, and an empty text is an empty
    //filter, which is the default state of every job.
    static ExcludeFilter parse(const std::string& text);

    bool empty() const { return pats_.empty(); }
    size_t size() const { return pats_.size(); }

    //The patterns as parsed, `|`-joined and folder slashes restored. This is what
    //the report records, so a report says what was skipped rather than echoing
    //whatever typos the user typed.
    std::string text() const;

    //`relPath` is relative to the source root and uses `/` as separator, matching
    //FileRecord::relPath. Files and folders are asked the same question: a folder
    //that matches is never descended into, so nothing under it is counted by the
    //fit check or copied by the job.
    bool matches(const std::string& relPath) const;

private:
    struct Pattern
    {
        std::string glob;       //folder patterns keep the slash stripped
        bool        folder = false;
    };

    std::vector<Pattern> pats_;

    //`*` and `?`, case-insensitive, with `/` never consumed by either wildcard.
    static bool globMatch(const std::string& glob, const std::string& subject);
};
}
