#include "handy_defines.h"

#ifdef macro_color
#define color_macro(_str)	{COLOR_WORD,{_str},  macro_color}
#endif

const struct color_scheme_entry c_color_scheme[] =  {
	// Coloring type           arguments       Color

	// strings
#ifdef string_color
	{COLOR_AROUND_TO_LINE,    {"\"", "\""},   string_color},
	{COLOR_STR,               {"''"},         normal_color},
	{COLOR_AROUND_TO_LINE,    {"'", "'"},     string_color},
	{COLOR_INSIDE_TO_LINE,    {"#include <", ">"}, string_color},
	{COLOR_INSIDE_TO_LINE,    {"#include<", ">"},  string_color},
#endif
	// comments
#ifdef comment_color
	{COLOR_AROUND,            {"/*", "*/"},   comment_color},
	{COLOR_AROUND,            {"//", "\n"},   comment_color},
#endif
	// macros
#ifdef macro_color
#ifdef constants_color
	{COLOR_STR_AFTER_WORD,    {"#ifdef"},     constants_color},
	{COLOR_STR_AFTER_WORD,    {"#ifndef"},    constants_color},
	{COLOR_STR_AFTER_WORD,    {"#define"},    constants_color},
	{COLOR_STR_AFTER_WORD,    {"#undef"},     constants_color},
#endif // constants_color
	{COLOR_WORD_STARTING_WITH_STR, {"#"},     {.fg = yellow, .mode = ATTR_BOLD}},
	color_macro("sizeof"),   color_macro("alignof"),
	color_macro("offsetof"), color_macro("va_arg"),
	color_macro("va_start"), color_macro("va_end"),
	color_macro("va_copy"),
	{COLOR_STR_AFTER_WORD,    {"defined"},       constants_color},
	color_macro("defined"),
#endif
	// operators
#ifdef operator_color
	{COLOR_STR,               {"!="},         normal_color},
	{COLOR_STR,               {"!"},          operator_color},
	{COLOR_STR,               {"~"},          operator_color},
	{COLOR_STR,               {"?"},          operator_color},
#endif
	// keywords
#ifdef keyword_color
	{COLOR_STR,               {"..."},         keyword_color},
	{COLOR_WORD_STR,          {"struct", "{"},keyword_color},
	{COLOR_WORD_STR,          {"union", "{"}, keyword_color},
	{COLOR_WORD_STR,          {"enum", "{"},  keyword_color},
	{COLOR_STR_AFTER_WORD,    {"struct"},     type_color},
	{COLOR_STR_AFTER_WORD,    {"union"},      type_color},
	{COLOR_STR_AFTER_WORD,    {"enum"},       type_color},
	{COLOR_STR_AFTER_WORD,    {"goto"},       constants_color},
	{COLOR_WORD_INSIDE,       {"}", ":"},     constants_color},
	{COLOR_WORD_INSIDE,       {"{", ":"},     constants_color},
	{COLOR_WORD_INSIDE,       {";", ":"},     constants_color},
	color_keyword("struct"),  color_keyword("enum"),
	color_keyword("union"),   color_keyword("const"),
	color_keyword("typedef"), color_keyword("extern"),
	color_keyword("static"),  color_keyword("inline"),
	color_keyword("if"),      color_keyword("else"),
	color_keyword("for"),     color_keyword("while"),
	color_keyword("case"),    color_keyword("switch"),
	color_keyword("do"),      color_keyword("return"),
	color_keyword("break"),   color_keyword("continue"),
	color_keyword("goto"),    color_keyword("restrict"),
	color_keyword("register"),
#endif
	// functions
#ifdef function_color
	{COLOR_WORD_BEFORE_STR,      {"("},       function_color},
#endif
#ifdef constants_color
	{COLOR_UPPER_CASE_WORD,   {0},            constants_color},
#endif
	// types
#ifdef type_color
	color_type("int"),     color_type("unsigned"),
	color_type("long"),    color_type("short"),
	color_type("char"),    color_type("void"),
	color_type("float"),   color_type("double"),
	color_type("complex"), color_type("bool"),
	color_type("_Bool"),   color_type("FILE"),
	color_type("va_list"),
	{COLOR_WORD_ENDING_WITH_STR, {"_t"},          type_color},
	{COLOR_WORD_ENDING_WITH_STR, {"_type"},       type_color},
	{COLOR_WORD_ENDING_WITH_STR, {"T"},           type_color},
#endif
	// numbers
#ifdef number_color
	color_number("0"), color_number("1"),
	color_number("2"), color_number("3"),
	color_number("4"), color_number("5"),
	color_number("6"), color_number("7"),
	color_number("8"), color_number("9"),
#endif
};

#define c_word_seperators default_word_seperators
