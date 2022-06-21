#include "pch.h"
#include "CppUnitTest.h"

#define Of(...) {__VA_ARGS__}
#define LONG(str) (const wchar_t*)std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(str).c_str()
#define FILES Of(                                               \
        "a", "b", "c", "d", "abc", "abd", "abe", "bb",          \
        "bcd", "ca", "cb", "dd", "de", "bdir/", "bdir/cfile"    \
    )

Glob glob("");

inline boost::optional<std::string> Matches(std::string glob, std::vector<std::string> paths, bool not = false)
{
    Glob g(glob);
    for (const std::string path : paths)
        if (not ? g.Matches(path) : !g.Matches(path)) return path;

    return boost::optional<std::string>();
}

inline boost::optional<std::string> Expands(std::string exp, std::vector<std::string> expected)
{
    std::vector<std::string> v = glob.BraceExpand(exp);
    if (v != expected)
        return "Got \n " + boost::join(v, ", ") + "\nExpected \n " + boost::join(expected, ", ");

    return boost::optional<std::string>();
}

#define MATCH(glob, paths)                  \
    if (auto path = Matches(glob, paths))   \
        Assert::Fail(LONG(glob" != " + (std::string)path->c_str()))

#define NMATCH(glob, paths)                     \
    if (auto path = Matches(glob, paths, true)) \
        Assert::Fail(LONG(glob" == " + (std::string)path->c_str()))

#define EXPAND(exp, expected)           \
    if (auto r = Expands(exp, expected)) Assert::Fail(LONG((std::string)r->c_str()))

#define ISGLOB(glob, expected)              \
    if (Glob(glob).IsGlob() != expected)    \
        expected ? Assert::Fail(LONG("\""glob"\".IsGlob != true"))   \
                 : Assert::Fail(LONG("\""glob"\".IsGlob != false"))

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Match
{
    TEST_CLASS(glob)
    {
    public:
        TEST_METHOD(basic)
        {
            MATCH("a*", Of("a", "abc", "abd", "abe"));
            MATCH("X*", Of("X*"));

            NMATCH("X*", Of(""));

            MATCH(R"(\\*)", Of(R"(\\*)"));
            MATCH(R"(\\**)", Of(R"(\\**)"));
            MATCH(R"(\\*\\*)", Of(R"(\\*\\*)"));

            MATCH("b*/", Of("bdir/"));
            MATCH("c*", Of("c", "ca", "cb"));
            MATCH("**", FILES);

            NMATCH("\\.\\./*/", Of("\\.\\./*/"));
            NMATCH("s/\\..*//", Of("s/\\..*//"));

            NMATCH("/^root:/{s/^[^:]*:[^:]*:([^:]*).*$/\\1/", Of("/^root:/{s/^[^:]*:[^:]*:([^:]*).*$/\\1/"));
            NMATCH("/^root:/{s/^[^:]*:[^:]*:([^:]*).*$/\u0001/", Of("/^root:/{s/^[^:]*:[^:]*:([^:]*).*$/\u0001/"));

            MATCH("[a-c]b*", Of("abc", "abd", "abe", "bb", "cb"));
            MATCH("[a-y]*[^c]", Of("abd", "abe", "bb", "bcd", "bdir/", "ca", "cb", "dd", "de"));
            MATCH("a*[^c]", Of("abd", "abe"));
            MATCH("a[X-]b", Of("a-b", "aXb"));
            MATCH("[^a-c]*", Of("d", "dd", "de"));
            MATCH("a\\*b/*", Of("a*b/ooo"));
            MATCH("a\\*?/*", Of("a*b/ooo"));
            NMATCH("*\\\\!*", Of("echo !7"));
            MATCH("*\\!*", Of("echo !7"));
            MATCH("*.\\*", Of("r.*"));
            MATCH("a[b]c", Of("abc"));
            MATCH("a[\\b]c", Of("abc"));
            MATCH("a?c", Of("abc"));
            NMATCH("a\\*c", Of("abc"));
            MATCH("", Of(""));

            MATCH("*/man*/bash.*", Of("man/man1/bash.1"));
            MATCH("man/man1/bash.1", Of("man/man1/bash.1"));
            MATCH("a***c", Of("abc"));
            MATCH("a*****?c", Of("abc"));
            MATCH("?*****??", Of("abc"));
            MATCH("*****??", Of("abc"));
            MATCH("?*****?c", Of("abc"));
            MATCH("?***?****c", Of("abc"));
            MATCH("?***?****?", Of("abc"));
            MATCH("?***?****", Of("abc"));
            MATCH("*******c", Of("abc"));
            MATCH("*******?", Of("abc"));
            MATCH("a*cd**?**??k", Of("abcdecdhjk"));
            MATCH("a**?**cd**?**??k", Of("abcdecdhjk"));
            MATCH("a**?**cd**?**??k***", Of("abcdecdhjk"));
            MATCH("a**?**cd**?**??***k", Of("abcdecdhjk"));
            MATCH("a**?**cd**?**??***k**", Of("abcdecdhjk"));
            MATCH("a****c**?**??*****", Of("abcdecdhjk"));
            MATCH("[-abc]", Of("-"));
            MATCH("[abc-]", Of("-"));
            MATCH("\\", Of("\\"));
            MATCH("[\\\\]", Of("\\"));
            MATCH("[[]", Of("["));
            MATCH("[", Of("["));
            MATCH("[*", Of("[abc"));


            MATCH("[]]", Of("]"));
            MATCH("[]-]", Of("]"));
            MATCH("[a-z]", Of("p"));
            NMATCH("??**********?****?", Of("abc"));
            NMATCH("??**********?****c", Of("abc"));
            NMATCH("?************c****?****", Of("abc"));
            NMATCH("*c*?**", Of("abc"));
            NMATCH("a*****c*?**", Of("abc"));
            NMATCH("a********???*******", Of("abc"));
            NMATCH("[]", Of("a"));
            NMATCH("[abc", Of("["));

            NMATCH("{/*,*}", Of("/asdf/asdf/asdf"));
            MATCH("{/?,*}", Of("/a", "bb"));
            MATCH("**", Of("a/b"));
            NMATCH("**", Of("a/.d", ".a/.d"));

            MATCH("a/*/b", Of("a/c/b"));
            NMATCH("a/*/b", Of("a/.d/b"));
            MATCH("a/.*/b", Of("a/./b", "a/../b", "a/.d/b"));


            MATCH("*(a/b)", Of("*(a/b)"));
            NMATCH("*(a/b)", Of("a/b"));

            MATCH("*(a|{b),c)}", Of("a", "ab", "ac"));
            NMATCH("*(a|{b),c)}", Of("ad"));

            MATCH("[!a*", Of("[!ab"));
            NMATCH("[!a*", Of("[ab"));

            MATCH("+(a|*\\|c\\\\|d\\\\\\|e\\\\\\\\|f\\\\\\\\\\|g", Of("+(a|b\\|c\\\\|d\\\\|e\\\\\\\\|f\\\\\\\\|g"));
            NMATCH("+(a|*\\|c\\\\|d\\\\\\|e\\\\\\\\|f\\\\\\\\\\|g", Of("a", "b\\c"));

            MATCH("*(a|{b,c})", Of("a", "b", "c", "ab", "ac"));
            MATCH("{a,*(b|c,d)}", Of("a", "(b|c", "*(b|c", "d)"));
            MATCH("{a,*(b|{c,d})}", Of("a", "b", "bc", "cb", "c", "d"));
            MATCH("*(a|{b|c,c})", Of("a", "b", "c", "ab", "ac", "bc", "cb"));

            MATCH("!a*", Of("\\!a", "d", "e", "!ab", "!abc"));
            MATCH("!!a*", Of("a!b"));
            MATCH("!\\!a*", Of("a!b", "d", "e", "\\!a"));

            MATCH("*.!(js)", Of("foo.bar", "foo.", "boo.js.boo", "foo.js.js"));

            MATCH("**/.x/**", Of(".x/", ".x/a", ".x/a/b", "a/.x/b", "a/b/.x/", "a/b/.x/c", "a/b/.x/c/d", "a/b/.x/c/d/e"));

            MATCH("[z-a]", Of());
            MATCH("a/[2015-03-10T00:23:08.647Z]/z", Of());
        }

        TEST_METHOD(brace_expand)
        {
            MATCH("a{b,c{d,e},{f,g}h}x{y,z}", Of("abxy", "abxz", "acdxy", "acdxz", "acexy", "acexz", "afhxy", "afhxz", "aghxy", "aghxz"));
            MATCH("a{1..5}b", Of("a1b", "a2b", "a3b", "a4b", "a5b"));
            MATCH("a{b}c", Of("a{b}c"));
            MATCH("{a},b}", Of("a}", "b"));
            MATCH("a{00..05}b", Of("a00b", "a01b", "a02b", "a03b", "a04b", "a05b"));
            MATCH("z{a,b},c}d", Of("za,c}d", "zb,c}d"));
            MATCH("z{a,b{,c}d", Of("z{a,bd", "z{a,bcd"));
            MATCH("a{b{c{d,e}f}g}h", Of("a{b{cdf}g}h", "a{b{cef}g}h"));
            MATCH("a{b{c{d,e}f{x,y}}g}h", Of("a{b{cdfx}g}h", "a{b{cdfy}g}h", "a{b{cefx}g}h", "a{b{cefy}g}h"));
            MATCH("a{b{c{d,e}f{x,y{}g}h", Of("a{b{cdfxh", "a{b{cdfy{}gh", "a{b{cefxh", "a{b{cefy{}gh"));
            MATCH("{a,b}${c}${d}", Of("a${c}${d}", "b${c}${d}"));
            MATCH("${a}${b}{c,d}", Of("${a}${b}c", "${a}${b}d"));
            EXPAND("x{{a,b}}y", Of("x{a}y", "x{b}y"));
        }

        TEST_METHOD(is_glob)
        {
            ISGLOB("a/b/c", false);
            ISGLOB("dir/src/abc.js", false);
            ISGLOB("dir/src/abc.*", true);
        }
    };

    TEST_CLASS(brace_expansion)
    {
    public:
        TEST_METHOD(numeric_sequence)
        {
            EXPAND("a{1..2}b{2..3}c", Of("a1b2c", "a1b3c", "a2b2c", "a2b3c"));
            EXPAND("{1..2}{2..3}", Of("12", "13", "22", "23"));
        }

        TEST_METHOD(numeric_inc_sequence)
        {
            EXPAND("{0..8..2}", Of("0", "2", "4", "6", "8"));
            EXPAND("{1..8..2}", Of("1", "3", "5", "7"));
        }

        TEST_METHOD(numeric_neg_sequence)
        {
            //EXPAND("{3..-2}", Of("3", "2", "1", "0", "-1", "-2"));
            // this behaviour is not Bash 4.3, however, this does not affect anything
            EXPAND("{3..-2}", Of("-2", "-1", "0", "1", "2", "3"));
        }

        TEST_METHOD(alpha_sequence)
        {
            EXPAND("1{a..b}2{b..c}3", Of("1a2b3", "1a2c3", "1b2b3", "1b2c3"));
            EXPAND("{a..b}{b..c}", Of("ab", "ac", "bb", "bc"));
        }

        TEST_METHOD(alpha_inc_sequence)
        {
            EXPAND("{a..k..2}", Of("a", "c", "e", "g", "i", "k"));
            EXPAND("{b..k..2}", Of("b", "d", "f", "h", "j"));
        }

        TEST_METHOD(dollar)
        {
            EXPAND("${1..3}", Of("${1..3}"));
            EXPAND("${a,b}${c,d}", Of("${a,b}${c,d}"));
            EXPAND("${a,b}${c,d}{e,f}", Of("${a,b}${c,d}e", "${a,b}${c,d}f"));
            EXPAND("{a,b}${c,d}${e,f}", Of("a${c,d}${e,f}", "b${c,d}${e,f}"));
            EXPAND("${a,b}${c,d}{1..3}", Of("${a,b}${c,d}1", "${a,b}${c,d}2", "${a,b}${c,d}3"));
            EXPAND("x${a,b}x${c,d}x", Of("x${a,b}x${c,d}x"));
            EXPAND("$", Of("$"));
        }

        TEST_METHOD(empty_option)
        {
            EXPAND("-v{,,,,}", Of("-v", "-v", "-v", "-v", "-v"));
        }

        TEST_METHOD(negative_increment)
        {
            //EXPAND("{3..1}", Of("3", "2", "1"));
            EXPAND("{3..1}", Of("1", "2", "3"));
            //EXPAND("{10..8}", Of("10", "9", "8"));
            EXPAND("{10..8}", Of("8", "9", "10"));
            //EXPAND("{10..08}", Of("10", "09", "08"));
            EXPAND("{10..08}", Of("08", "09", "10"));
            //EXPAND("{c..a}", Of("c", "b", "a"));
            EXPAND("{c..a}", Of("a", "b", "c"));

            //EXPAND("{4..0..2}", Of("4", "2", "0"));
            EXPAND("{4..0..2}", Of("0", "2", "4"));
            //EXPAND("{4..0..-2}", Of("4", "2", "0"));
            EXPAND("{4..0..-2}", Of("0", "2", "4"));
            //EXPAND("{e..a..2}", Of("e", "c", "a"));
            EXPAND("{e..a..2}", Of("a", "c", "e"));
        }

        TEST_METHOD(pad)
        {
            EXPAND("{9..11}", Of("9", "10", "11"));
            EXPAND("{09..11}", Of("09", "10", "11"));
        }

        TEST_METHOD(nested)
        {
            EXPAND("{a,b{1..3},c}", Of("a", "b1", "b2", "b3", "c"));
            EXPAND("{{A..Z},{a..z}}", Of(
                "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
                "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
            ));
            EXPAND("ppp{,config,oe{,conf}}", Of("ppp", "pppconfig", "pppoe", "pppoeconf"));
        }

        TEST_METHOD(same_type)
        {
            EXPAND("{a..9}", Of("{a..9}"));
        }
    };
};