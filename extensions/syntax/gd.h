#include "handy_defines.h"

const struct syntax_scheme_entry gd_syntax[] =  {
        // Coloring type           arguments       Color

        // strings
#ifdef string_color
        {COLOR_AROUND_TO_LINE,    {"\"", "\""},   string_color},
        {COLOR_AROUND,    {"\"\"\"", "\"\"\""},   string_color},
        {COLOR_AROUND_TO_LINE,    {"'", "'"},     string_color},
        {COLOR_INSIDE_TO_LINE,    {"$", "."},     string_color},
        {COLOR_INSIDE_TO_LINE,    {"$", " "},     string_color},
        {COLOR_INSIDE_TO_LINE,    {"$", "("},     string_color},
        {COLOR_INSIDE_TO_LINE,    {"$", "["},     string_color},
        {COLOR_INSIDE_TO_LINE,    {"$", "\n"},    string_color},
        {COLOR_STR,               {"$"},          string_color},
#endif
        // comments
#ifdef comment_color
        {COLOR_AROUND,            {"#", "\n"},   comment_color},
#endif
        // operators
#ifdef operator_color
        {COLOR_STR,               {"!="},         normal_color},
        {COLOR_STR,               {"!"},          operator_color},
#endif
        // constants
#ifdef constants_color
        {COLOR_UPPER_CASE_WORD,   {0},          constants_color},
        {COLOR_WORD,              {"PI"},       constants_color},
        {COLOR_WORD,              {"TAU"},      constants_color},
        {COLOR_WORD,              {"INF"},      constants_color},
        {COLOR_WORD,              {"NAN"},      constants_color},
        {COLOR_WORD,              {"null"},     constants_color},
        {COLOR_WORD,              {"true"},     constants_color},
        {COLOR_WORD,              {"false"},    constants_color},
        {COLOR_STR_AFTER_WORD,    {"const"},    constants_color},
#endif
        // keywords
#ifdef keyword_color
        color_keyword("extends"), color_keyword("class_name"),
        color_keyword("var"), color_keyword("const"),
        color_keyword("enum"), color_keyword("func"),
        color_keyword("if"), color_keyword("else"),
        color_keyword("elif"), color_keyword("for"),
        color_keyword("while"), color_keyword("in"),
        color_keyword("return"), color_keyword("class"),
        color_keyword("pass"), color_keyword("continue"),
        color_keyword("break"), color_keyword("is"),
        color_keyword("as"), color_keyword("self"),
        color_keyword("tool"), color_keyword("signal"),
        color_keyword("static"), color_keyword("onready"),
        color_keyword("export"), color_keyword("setget"),
        color_keyword("breakpoint"), color_keyword("yield"),
        color_keyword("assert"), color_keyword("preload"),
        color_keyword("remote"), color_keyword("master"),
        color_keyword("puppet"), color_keyword("remotesync"),
        color_keyword("mastersync"), color_keyword("puppetsync"),
        color_keyword("and"), color_keyword("or"),
#endif
        // functions
#ifdef function_color
        {COLOR_WORD_INSIDE,      {"func ", "("},  macro_color},
        {COLOR_WORD_INSIDE,      {"class ", ":"},  macro_color},
        {COLOR_WORD_BEFORE_STR,  {"("},           function_color},
#endif
        // types
#ifdef type_color
        color_type("bool"), color_type("int"),
        color_type("float"), color_type("String"),
        color_type("Vector2"), color_type("Rect2"),
        color_type("Vector3"), color_type("Transform2D"),
        color_type("Plane"), color_type("Quat"),
        color_type("AABB"), color_type("Basis"),
        color_type("Transform"), color_type("Color"),
        color_type("NodePath"), color_type("RID"),
        color_type("Object"), color_type("Array"),
        color_type("PoolByteArray"), color_type("PoolIntArray"),
        color_type("PoolRealArray"), color_type("PoolStringArray"),
        color_type("PoolVector2Array"), color_type("PoolVector3Array"),
        color_type("PoolColorArray"), color_type("Dictionary"),
        color_type("Dictionary"), color_type("Node"),
        color_type("void"),
#endif
        // numbers
#ifdef number_color
        color_numbers(),
#endif
};

#define gd_word_seperators default_word_seperators

const struct indent_scheme_entry gd_indent[] = {
        {INDENT_REMOVE,      INDENT_LINE_CONTAINS_WORD, -1, {"return"}},
        {INDENT_REMOVE,      INDENT_LINE_CONTAINS_WORD, -1, {"break"}},
        {INDENT_REMOVE,      INDENT_LINE_CONTAINS_WORD, -1, {"continue"}},
        {INDENT_REMOVE,      INDENT_LINE_CONTAINS_WORD, -1, {"pass"}},
        {INDENT_NEW,         INDENT_LINE_ENDS_WITH_STR, -1, {":"}},
};
