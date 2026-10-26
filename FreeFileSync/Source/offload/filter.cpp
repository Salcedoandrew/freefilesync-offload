// *****************************************************************************
// * Offload mode for FreeFileSync - the Skip field's matcher. See filter.h.    *
// *****************************************************************************

#include "filter.h"

namespace offload
{
namespace
{
char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string baseName(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string trimmed(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
        --e;
    return s.substr(b, e - b);
}
}


//Classic wildcard match with backtracking, so `*a*b` cannot blow up on a long
//name. Two rules matter more than the algorithm: a wildcard never eats a `/`
//(otherwise `*.LRV` would match `DCIM/x.LRV` as a whole path and the "name" form
//would mean something different from the "path" form), and comparison is
//case-insensitive.
bool ExcludeFilter::globMatch(const std::string& glob, const std::string& subject)
{
    size_t gi = 0;
    size_t si = 0;
    size_t star = std::string::npos;
    size_t starSi = 0;

    while (si < subject.size())
    {
        if (gi < glob.size() && glob[gi] == '*')
        {
            star   = gi++;
            starSi = si;
        }
        else if (gi < glob.size() && glob[gi] != '*' &&
                 (glob[gi] == '?' ? subject[si] != '/'
                                  : lowerAscii(glob[gi]) == lowerAscii(subject[si])))
        {
            ++gi;
            ++si;
        }
        else if (star != std::string::npos && subject[starSi] != '/')
        {
            gi = star + 1;
            si = ++starSi;
        }
        else
            return false;
    }

    while (gi < glob.size() && glob[gi] == '*')
        ++gi;
    return gi == glob.size();
}


ExcludeFilter ExcludeFilter::parse(const std::string& text)
{
    ExcludeFilter f;

    size_t start = 0;
    for (;;)
    {
        const size_t bar = text.find('|', start);
        std::string part = trimmed(text.substr(start, bar == std::string::npos
                                                      ? std::string::npos
                                                      : bar - start));
        if (!part.empty())
        {
            Pattern p;
            if (part.back() == '/')
            {
                p.folder = true;
                while (!part.empty() && part.back() == '/')
                    part.pop_back();
            }
            //`/DCIM/` and `DCIM/` mean the same thing: both are relative to the
            //source root, so leading slashes carry no information.
            while (!part.empty() && part.front() == '/')
                part.erase(part.begin());

            if (!part.empty())
            {
                p.glob = part;
                f.pats_.push_back(p);
            }
        }
        if (bar == std::string::npos)
            break;
        start = bar + 1;
    }
    return f;
}


std::string ExcludeFilter::text() const
{
    std::string out;
    for (const Pattern& p : pats_)
    {
        if (!out.empty())
            out += " | ";
        out += p.glob;
        if (p.folder)
            out += '/';
    }
    return out;
}


bool ExcludeFilter::matches(const std::string& relPath) const
{
    if (pats_.empty() || relPath.empty())
        return false;

    const std::string name = baseName(relPath);

    for (const Pattern& p : pats_)
    {
        //A pattern with a slash in it is about the path; one without is about the
        //name. That is the whole reason `*.LRV` finds a file three folders down
        //while `DCIM/2024` does not.
        const bool fullPath = p.glob.find('/') != std::string::npos;
        if (globMatch(p.glob, fullPath ? relPath : name))
            return true;

        //A folder pattern also covers what is inside it, so a walk that starts
        //below the folder (a re-measure of a subfolder, a second run into a
        //nested root) still excludes the same files the first run did.
        if (!p.folder)
            continue;
        for (size_t i = 1; i < relPath.size(); ++i)
        {
            if (relPath[i] != '/')
                continue;
            const std::string ancestor = relPath.substr(0, i);
            if (globMatch(p.glob, fullPath ? ancestor : baseName(ancestor)))
                return true;
        }
    }
    return false;
}
}
