#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <iomanip>
#include <sstream>
#include <regex>
#include <stack>

struct Glob
{
    Glob(const std::string& pattern)
    {
        // normalise slashes
        static const std::regex slashSplit{ "/+" };
        this->pattern = std::regex_replace(boost::trim_copy(pattern), slashSplit, "/");

        ParseNegate();

        // Bash 4.3 preserves the first two bytes that start with {}
        if (this->pattern.substr(0, 2) == "{}") this->pattern = "\\{\\}" + this->pattern.substr(2);
        globSet = BraceExpand(this->pattern);

        for (std::string& p : globSet)
        {
            std::vector<std::string> parts;
            boost::split(parts, p, boost::is_any_of("/"));
            globParts.push_back(parts);
        }

        for (std::vector<std::string>& globPart : globParts)
        {
            std::vector<ParseItem*> buffer;
            for (std::string& part : globPart)
                if (auto parsed = Parse(part, false))
                    buffer.push_back(parsed->first);
                else goto failure;

            set.push_back(buffer);
            continue;
        failure:
            // something broke, ignore this glob
            continue;
        }
    }

    void ParseNegate()
    {
        auto it = pattern.begin();
        auto end = pattern.end();
        for (; it != end && *it == '!'; ++it);
        negate = std::distance(pattern.begin(), it) & 1;
        pattern = std::string(it, end);
    }

    // https://www.gnu.org/software/bash/manual/html_node/Brace-Expansion.html
    std::vector<std::string> BraceExpand(const std::string& pattern)
    {
        auto begin = pattern.begin();
        auto it = pattern.begin();
        auto end = pattern.end();

        if (pattern[0] != '{')
        {
            for (; it != end; ++it)
            {
                switch (*it)
                {
                    case('$'):
                        if (std::distance(it, end) <= 4) break;
                        if (*(it + 1) == '{')
                        {
                            for (++it; it == end && *it != '}'; ++it);
                            if (it == end) break;
                        }
                        continue;
                    case('\\'):
                        if (++it == end) break;
                        continue;
                    case('{'):
                    {
                        std::string prefix(begin, it);
                        std::string postfix(it, end);
                        std::vector<std::string> e = BraceExpand(postfix);
                        for (auto& s : e) s = prefix + s;
                        return e;
                    }
                    default:
                        continue;
                }

                break;
            }

            return { pattern };
        }

        static const std::regex numericSequence(R"(^\{(-?[[:d:]]+)\.\.(-?[[:d:]]+)(?:\.\.(-?[[:d:]]+))?\})");
        static const std::regex alphaSequence(R"(^\{([[:alpha:]])\.\.([[:alpha:]])(?:\.\.(-?[[:d:]]+))?\})");
        std::smatch seq;
        bool isNumeric = std::regex_search(pattern, seq, numericSequence);
        if (isNumeric || std::regex_search(pattern, seq, alphaSequence))
        {
            std::vector<std::string> suf = BraceExpand(seq.suffix());
            auto $1 = seq[1].str();
            auto $2 = seq[2].str();
            size_t pad = 0;
            int inc = seq[3].matched == false ? 1 : std::abs(std::stoi(seq[3].str())),
                start,
                end;

            if (isNumeric)
            {
                size_t $1_size;
                size_t $2_size;
                start = std::stoi($1, &$1_size);
                end = std::stoi($2, &$2_size);
                if ($1[$1.length() - $1_size] == '0' || $2[$2.length() - $2_size] == '0')
                    pad = std::max($1_size, $2_size);
            }
            else start = $1[0], end = $2[0];
            if (start > end) std::swap(start, end);

            std::vector<std::string> ret;
            for (; start <= end; start += inc)
            {
                std::ostringstream o;
                if (isNumeric) o << std::setfill('0') << std::setw(pad) << start;
                else o << (char)start;
                for (int i = 0; i < suf.size(); i++)
                    ret.push_back(o.str() + suf[i]);
            }

            return ret;
        }

        int depth = 1;
        std::vector<std::string> set;
        auto member = pattern.begin();

        for (++member, ++it; it != end && depth; ++it)
        {
            switch (*it)
            {
                case ('\\'):
                    if (++it == end) break;
                    continue;
                case ('{'):
                    depth++;
                    continue;
                case ('}'):
                    if (--depth == 0)
                    {
                        set.push_back(std::string(member, it));
                        std::advance(member, std::distance(member, it) + 1);
                    }
                    continue;
                case (','):
                    if (depth == 1)
                    {
                        set.push_back(std::string(member, it));
                        std::advance(member, std::distance(member, it) + 1);
                    }
                    continue;
                default: continue;
            }

            break;
        }

        if (depth != 0) return BraceExpand("\\" + pattern);

        std::vector<std::string> nestedSet;
        for (auto& s : set)
            for (auto& e : BraceExpand(s))
                nestedSet.push_back(e);

        std::string post(it, end);
        if (set.size() == 1)
        {
            static const std::regex hasOptions(R"(,.*\})");
            if (std::regex_search(post, hasOptions))
                return BraceExpand(std::string(begin, it - 1) + "\\}" + post);
            for (auto& s : nestedSet) s = "{" + s + "}";
        }

        // add suffixes
        std::vector<std::string> result;
        std::vector<std::string> r = BraceExpand(post);
        for (auto postIt = r.begin(); postIt != r.end(); ++postIt)
            for (auto setIt = nestedSet.begin(); setIt != nestedSet.end(); ++setIt)
                result.push_back(*setIt + *postIt);

        return result;
    }

    class ParseItem
    {
    protected:
        std::string m_source;

    public:
        std::string source() const { return m_source; };
        virtual bool match(const std::string& input) = 0;
    };

    class LiteralItem : public ParseItem
    {
    public:
        LiteralItem(const std::string& src) { m_source = src; };
        bool match(const std::string& input) override { return input == m_source; };
    };

    class MagicItem : public ParseItem
    {
    protected:
        std::regex regex;

    public:
        MagicItem(const std::string& src)
        {
            m_source = src;
            regex = std::regex{ "^" + src + "$" };
        };
        bool match(const std::string& input) override
        {
            return std::regex_match(input, regex);
        };
    };

    struct PatternListEntry
    {
        char type;
        size_t start;
        size_t reStart;
        size_t reEnd;
    };

    boost::optional<std::pair<Glob::ParseItem*, bool>> Parse(std::string& pattern, bool isSub)
    {
        if (pattern.empty() || pattern == "**")
            return std::pair<ParseItem*, bool>{new LiteralItem{ pattern }, false};

        static const std::string qmark = "[^/]";
        static const std::string star = qmark + "*?";
        static const std::string reSpecials = "()[]{}?*+^$\\.&~# \t\n\r\v\f";

        std::string re;
        bool hasMagic = false, escaping = false, inClass = false;
        std::stack<PatternListEntry> patternListStack;
        std::vector<PatternListEntry> negativeLists;
        char stateChar = 0;

        size_t reClassStart = 0, classStart = 0;

        std::string patternStart = pattern[0] == '.' ? "" : R"((?!\.))";

        auto clearStateChar = [&]()
        {
            switch (stateChar)
            {
                case 0:
                    return;
                case '*':
                    re += star;
                    hasMagic = true;
                    break;
                case '?':
                    re += qmark;
                    hasMagic = true;
                    break;
                default:
                    re += "\\";
                    re += stateChar;
                    break;
            }

            stateChar = 0;
        };

        for (size_t i = 0; i < pattern.length(); i++)
        {
            char c = pattern[i];

            if (escaping)
            {
                if (c == '/')
                    return boost::optional<std::pair<ParseItem*, bool>>{};
                if (reSpecials.find(c) != std::string::npos)
                    re += "\\";
                re += c;
                escaping = false;
                continue;
            }

            switch (c)
            {
                case '/':
                    return boost::optional<std::pair<ParseItem*, bool>>{};

                case '\\':
                    clearStateChar();
                    escaping = true;
                    continue;

                case '?':
                case '*':
                case '+':
                case '@':
                case '!':
                    if (inClass)
                    {
                        if (c == '!' && i == classStart + 1) c = '^';
                        re += c;
                        continue;
                    }

                    clearStateChar();
                    stateChar = c;
                    continue;

                case '(':
                    if (inClass)
                    {
                        re += "(";
                        continue;
                    }

                    if (!stateChar)
                    {
                        re += "\\(";
                        continue;
                    }

                    patternListStack.push(PatternListEntry{ stateChar, i - 1, re.length() });
                    re += stateChar == '!' ? "(?:(?!(?:" : "(?:";
                    stateChar = 0;
                    continue;

                case ')':
                    if (inClass || patternListStack.empty())
                    {
                        re += "\\)";
                        continue;
                    }

                    clearStateChar();
                    hasMagic = true;
                    auto pl = patternListStack.top();
                    patternListStack.pop();

                    re += ')';
                    switch (pl.type)
                    {
                        case '!':
                            re += ")[^/]*?)";
                            pl.reEnd = re.length();
                            negativeLists.push_back(pl);
                            break;
                        case '?':
                        case '+':
                        case '*':
                            re += pl.type;
                            break;
                            // case '@':
                            //   break;
                    }

                    continue;

                case '|':
                    if (inClass || patternListStack.empty())
                    {
                        re += "\\|";
                        continue;
                    }

                    clearStateChar();
                    re += "|";
                    continue;

                case '[':
                    clearStateChar();

                    if (inClass)
                    {
                        re += "\\[";
                        continue;
                    }

                    inClass = true;
                    classStart = i;
                    reClassStart = re.length();
                    re += c;
                    continue;

                case ']':
                {
                    if (i == classStart + 1 || !inClass)
                    {
                        re += "\\]";
                        continue;
                    }

                    std::string cs = pattern.substr(classStart + 1, i - classStart - 1);
                    try
                    {
                        auto escaped = boost::replace_all_copy(cs, "\\", "");
                        auto s = std::regex_replace(escaped, std::regex{ "[\\[\\]]" }, "\\$&");
                        std::regex reClass{ "[" + s + "]" };
                    }
                    catch (const std::regex_error&)
                    {
                        std::pair<Glob::ParseItem*, bool> sp = *Parse(cs, true);
                        re = re.substr(0, reClassStart) + "\\[" + (*sp.first).source() + "\\]";
                        hasMagic = hasMagic || sp.second;
                        inClass = false;
                        continue;
                    }

                    hasMagic = true;
                    inClass = false;
                    re += c;
                    continue;
                }

                default:
                    clearStateChar();

                    if (reSpecials.find(c) != std::string::npos && !(c == '^' && inClass))
                        re += "\\";

                    re += c;
                    break;
            }
        }

        if (inClass)
        {
            std::string cs = pattern.substr(classStart + 1);
            std::pair<Glob::ParseItem*, bool> sp = *Parse(cs, true);
            re = re.substr(0, reClassStart) + "\\[" + (*sp.first).source();
            hasMagic = hasMagic || sp.second;
        }

        for (size_t p = patternListStack.size(); p-- > 0;)
        {
            auto pl = patternListStack.top();
            patternListStack.pop();

            std::string tail;
            std::string tailStr = re.substr(pl.reStart + 3);
            ptrdiff_t lastMatch = 0;
            auto lastMatchEnd = tailStr.cbegin();
            static const std::regex escapeCheck(R"(((?:\\{2}){0,64})(\\?)\|)");
            std::sregex_iterator begin(tailStr.cbegin(), tailStr.cend(), escapeCheck),
                end;

            for (; begin != end; ++begin)
            {
                const std::smatch& match = *begin;
                auto pos = match.position();
                auto start = lastMatchEnd;
                std::advance(start, pos - lastMatch);
                tail.append(lastMatchEnd, start);
                auto $1 = match[1].str();
                auto $2 = match[2].str();

                if ($2.empty()) $2 = "\\";
                tail.append($1 + $1 + $2 + "|");

                auto length = match.length();
                lastMatch = pos + length;
                lastMatchEnd = start;
                std::advance(lastMatchEnd, length);
            }

            tail.append(lastMatchEnd, tailStr.cend());

            std::string t = pl.type == '*' ? star
                : pl.type == '?' ? qmark
                : "\\" + std::string(1, pl.type);

            hasMagic = true;
            re = re.substr(0, pl.reStart) + t + "\\(" + tail;
        }

        clearStateChar();
        if (escaping) re += "\\\\";

        bool addPatternStart = false;
        switch (re[0])
        {
            case '[':
            case '.':
            case '(':
                addPatternStart = true;
                break;
        }

        for (size_t n = negativeLists.size(); n-- > 0;)
        {
            auto nl = negativeLists[n];

            auto nlBefore = re.substr(0, nl.reStart);
            auto nlFirst = re.substr(nl.reStart, nl.reEnd - nl.reStart - 8);
            auto nlAfter = re.substr(nl.reEnd);
            auto nlLast = re.substr(nl.reEnd - 8, 8) + nlAfter;

            auto openParensBefore = std::count(nlBefore.begin(), nlBefore.end(), '(');
            while (openParensBefore--)
                nlAfter = std::regex_replace(nlAfter, std::regex("\\)[+*?]?"), "");

            std::string dollar = nlAfter.empty() && !isSub ? "$" : "";
            re = nlBefore + nlFirst + nlAfter + dollar + nlLast;
        }

        if (!re.empty() && hasMagic) re = "(?=.)" + re;

        if (addPatternStart) re = patternStart + re;

        if (isSub)
            return std::pair<ParseItem*, bool>{new LiteralItem{ re }, hasMagic};

        if (!hasMagic)
        {
            static const std::regex globUnescape(R"(\\(.))");
            return std::pair<ParseItem*, bool>{new LiteralItem{ std::regex_replace(re, globUnescape, "$1") }, false};
        }

        return std::pair<ParseItem*, bool>{new MagicItem{ re }, false};
    }

    bool Matches(const std::string& file)
    {
        std::vector<std::string> parts;
        boost::split(parts, file, boost::is_any_of("/"));

        for (auto& member : set)
            if (this->MatchOne(parts, member, false)) return !negate;

        return negate;
    }

    bool MatchOne(const std::vector<std::string>& file,
        const std::vector<ParseItem*>& pattern, bool partial)
    {
        size_t fi = 0, pi = 0;
        auto fl = file.size();
        auto pl = pattern.size();

        for (; fi < fl && pi < pl; fi++, pi++)
        {
            auto part = file[fi];
            auto item = pattern[pi];

            if (item == nullptr) return false;

            if (item->source() == "**")
            {
                size_t fr = fi, pr = pi + 1;

                if (pr == pl)
                {
                    for (; fi < fl; fi++)
                        if (file[fi][0] == '.')
                            return false;

                    return true;
                }

                while (fr < fl)
                {
                    std::vector<std::string> frSlice(file.begin() + fr, file.end());
                    std::vector<ParseItem*> prSlice(pattern.begin() + pr, pattern.end());
                    if (MatchOne(frSlice, prSlice, partial))
                        return true;
                    if (file[fr][0] == '.')
                        break;
                    fr++;
                }

                if (partial && fr == file.size())
                    return true;

                return false;
            }

            if (!item->match(part)) return false;
        }

        if (fi == file.size() && pi == pattern.size())
            return true;
        else if (fi == file.size())
            return partial;
        else if (pi == pattern.size())
            return fi == file.size() - 1 && file[fi] == "";

        throw std::runtime_error("Glob matching passthrough.");
    }

private:
    bool negate = false;
    std::vector<std::string> globSet;
    std::vector<std::vector<std::string>> globParts;
    std::string pattern;
    std::vector<std::vector<ParseItem*>> set;
};