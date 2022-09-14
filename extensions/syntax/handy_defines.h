#ifndef HANDY_DEFINES_H_
#define HANDY_DEFINES_H_

#define default_word_seperators "., '\n\t*+-/%!~<>=(){}[]\"^&|\\`´?:;"

#ifdef keyword_color
#define color_keyword(_str) {COLOR_WORD,{_str},  keyword_color}
#endif

#ifdef type_color
#define color_type(_str)	{COLOR_WORD,{_str},  type_color}
#endif

#ifdef number_color
#define color_number(_num)									\
	{COLOR_WORD_STARTING_WITH_STR, {_num}, number_color},	\
	{COLOR_WORD_ENDING_WITH_STR, {_num".f"},number_color}
#endif

#define color_numbers()							\
	color_number("0"),							\
	color_number("1"),    						\
	color_number("2"),    						\
	color_number("3"),    						\
	color_number("4"),    						\
	color_number("5"),    						\
	color_number("6"),    						\
	color_number("7"),    						\
	color_number("8"),    						\
	color_number("9")

#endif // HANDY_DEFINES_H_
