#ifndef SYNTAX_H_
#define SYNTAX_H_

#include "../../se.h"
#include "../../config.h"
#include "../../extension.h"
#include <ctype.h>


///////////////////////////////////
// the user must fill this inn
// null terminated
extern const struct syntax_scheme* syntax_schemes;

static int fb_set_syntax_scheme(struct file_buffer* fb);
static int apply_syntax(struct window_split_node* wn, const int offset_start, const int offset_end, uint8_t* move_buffer, const int move_buffer_len);

static const struct extension syntax_e = {
		.fb_new_file_opened = fb_set_syntax_scheme,
		.window_written_to_screen = apply_syntax
};

#define UPPER_CASE_WORD_MIN_LEN 3

enum syntax_scheme_mode {
		// needs two strings
		COLOR_AROUND,
		// needs two strings
		COLOR_AROUND_TO_LINE,
		// needs two strings
		COLOR_INSIDE,
		// needs two strings
		COLOR_INSIDE_TO_LINE,
		// needs two strings
		COLOR_WORD_INSIDE,
		// needs one string
		COLOR_WORD,
		// needs one string
		COLOR_WORD_ENDING_WITH_STR,
		// needs one string
		COLOR_WORD_STARTING_WITH_STR,
		// needs one string
		COLOR_STR,
		// needs two strings
		// colors word if string is found after it
		COLOR_WORD_STR,
		// needs one string
		// can be combined with others if this is first
		COLOR_STR_AFTER_WORD,
		// needs one string
		// "(" would color like this "not_colored colored("
		// "[" would color like this "not_colored colored ["
		COLOR_WORD_BEFORE_STR,
		// needs one string
		// "(" would color like this "colored not_colored("
		// "=" would color like this "colored not_colored ="
		COLOR_WORD_BEFORE_STR_STR,
		// no arguments needed
		COLOR_UPPER_CASE_WORD,
};

struct syntax_scheme_entry {
		const enum syntax_scheme_mode mode;
		const struct delimiter arg;
		const struct glyph attr;
};

// TODO: INDENT_LINE_CONTAINS_STR_AND_STR
enum indent_scheme_mode {
		INDENT_LINE_ENDS_WITH_STR,
		INDENT_LINE_DOES_NOT_END_WITH_STR,

		INDENT_LINE_CONTAINS_WORD,
		INDENT_LINE_ONLY_CONTAINS_STR,
		// neds two strings
		INDENT_LINE_CONTAINS_STR_MORE_THAN_STR,
};

enum indent_scheme_type {
		INDENT_REMOVE = -1,
		INDENT_KEEP_OPENER = 0,
		INDENT_NEW = 1,

		INDENT_KEEP,
		// needs two strings, requires closer to be string 1 and opener to be string two
		INDENT_RETURN_TO_OPENER_BASE_INDENT,
};

struct indent_scheme_entry {
		const enum indent_scheme_type type;
		const enum indent_scheme_mode mode;
		const unsigned int line_offset;
		const struct delimiter arg;
};

struct syntax_scheme {
		const char* file_ending;
		const char* word_seperators;

		const struct syntax_scheme_entry* entries;
		const int entry_count;

		const struct indent_scheme_entry* indents;
		const int indent_count;
};


static int fb_auto_indent(struct file_buffer* fb, int offset);

static void do_syntax_scheme(struct file_buffer* fb, const struct syntax_scheme* cs, int offset);

static void whitespace_count_to_indent_amount(int indent_len, int count, int* indent_count, int* extra_spaces);
static int  get_line_leading_whitespace_count(const char* line);
static int  get_line_relative_offset(struct file_buffer* fb, int offset, int count);

static const struct syntax_scheme*
fb_get_syntax_scheme(struct file_buffer* fb)
{
		return fb->syntax_index < 0 ? NULL : &syntax_schemes[fb->syntax_index];
}

static int
fb_set_syntax_scheme(struct file_buffer* fb)
{
		for (int i = 0; syntax_schemes[i].file_ending; i++)
				if (is_file_type(fb->file_path, syntax_schemes[i].file_ending))
						fb->syntax_index = i;
		return 0;
}

static int
apply_syntax(struct window_split_node* wn, const int offset_start, const int offset_end, uint8_t* move_buffer, const int move_buffer_len)
{
		global_attr = default_attributes;
		struct window_buffer* wb = &wn->wb;
		struct file_buffer* fb = get_fb(wb);
		const struct syntax_scheme* cs = fb_get_syntax_scheme(fb);
		if (!cs)
				return 0;

		// clear state
		do_syntax_scheme(NULL, &(struct syntax_scheme){0}, 0);

		// search backwards to find multi-line syntax highlighting
		for (int i = 0; i < cs->entry_count; i++) {
				const struct syntax_scheme_entry cse = cs->entries[i];
				if (cse.mode == COLOR_AROUND || cse.mode == COLOR_INSIDE) {
						int offset = 0;
						int count = 0;
						int start_len = strlen(cse.arg.start);
						while((offset = fb_seek_string(fb, offset, cse.arg.start)) >= 0) {
								offset += start_len;
								if (offset >= offset_start)
										break;
								count++;
						}

						if (strcmp(cse.arg.start, cse.arg.end) != 0) {
								int end_len = strlen(cse.arg.end);
								offset = 0;
								while((offset = fb_seek_string(fb, offset, cse.arg.end)) >= 0) {
										offset += end_len;
										if (offset >= offset_start)
												break;
										count--;
								}
						}
						if (count > 0) {
								offset = fb_seek_string_backwards(fb, offset_start, cse.arg.start);
								do_syntax_scheme(fb, cs, offset);
								break;
						}
				}
		}

		int x = wn->minx + move_buffer[0], y = wn->miny;
		int move_buffer_index = 0;
		int charsize = 1;
		for(int i = offset_start; i < offset_end && y < wn->maxy
					&& move_buffer_index < move_buffer_len; i += charsize) {
				do_syntax_scheme(fb, cs, i);
				screen_set_attr(x, y)->fg = global_attr.fg;
				screen_set_attr(x, y)->bg = global_attr.bg;

				uint8_t amount = move_buffer[move_buffer_index];
				if (amount & (1<<7)) {
						x = wn->minx;
						y++;
						amount &= ~(1<<7);
				}
				x += amount;

				rune_t u;
				charsize = utf8_decode_buffer(fb->contents + i, i - offset_start, &u);
				if (charsize == 0)
						charsize = 1;
				move_buffer_index++;
		}

		do_syntax_scheme(NULL, &(struct syntax_scheme){0}, 0);
		global_attr = default_attributes;
		return 0;
}

void
do_syntax_scheme(struct file_buffer* fb, const struct syntax_scheme* cs, int offset)
{
		static int end_at_whitespace = 0;
		static const char* end_condition;
		static int end_condition_len;
		static struct glyph next_word_attr;
		static int color_next_word = 0;
		static int around = 0;

		if (!fb || !cs) {
				// reset
				end_at_whitespace = 0;
				end_condition_len = 0;
				around = 0;
				color_next_word = 0;
				end_condition = NULL;
				global_attr = default_attributes;
				return;
		}

		char* buf = fb->contents;
		int buflen = fb->len;

		if (end_condition && !color_next_word) {
				if (buflen - offset <= end_condition_len)
						return;
				if (end_at_whitespace && buf[offset] == '\n') {
						// *_TO_LINE reached end of line
						end_condition_len = 0;
						end_condition = NULL;
						end_at_whitespace = 0;
						global_attr = default_attributes;
				} else if (fb_offset_starts_with(fb, offset, end_condition)) {
						if (isspace(end_condition[end_condition_len-1])) {
								end_condition_len--;
								if (end_condition_len <= 0)
										global_attr = default_attributes;
						}
						// if it's around not inside, don't reset color until later
						if (around)
								around = 0;
						else
								global_attr = default_attributes;

						end_condition = NULL;
						end_at_whitespace = 0;
				}
				return;
		} else if (end_at_whitespace) {
				if (!fb_is_on_a_word(fb, offset, cs->word_seperators)) {
						end_at_whitespace = 0;
						global_attr = default_attributes;
				} else {
						return;
				}
		} else if (color_next_word) {
				// check if new word encountered
				if (!fb_is_on_a_word(fb, offset, cs->word_seperators))
						return;
				global_attr = next_word_attr;
				color_next_word = 0;
				end_at_whitespace = 1;
				return;
		} else if (end_condition_len > 0) {
				// wait for the word/sequence to finish
				// NOTE: does not work with utf8 chars
				// TODO: ???
				if (--end_condition_len <= 0)
						global_attr = default_attributes;
				else
						return;
		}

		for (int i = 0; i < cs->entry_count; i++) {
				struct syntax_scheme_entry entry = cs->entries[i];
				enum syntax_scheme_mode mode = entry.mode;

				if (mode == COLOR_UPPER_CASE_WORD) {
						if (!fb_is_start_of_a_word(fb, offset, cs->word_seperators))
								continue;

						int end_len = 0;
						while (offset + end_len < fb->len && !str_contains_char(cs->word_seperators, buf[offset + end_len])) {
								if (!isupper(buf[offset + end_len]) && buf[offset + end_len] != '_'
									&& (!end_len || (buf[offset + end_len] < '0' || buf[offset + end_len] > '9')))
										goto not_upper_case;
								end_len++;
						}
						// upper case words must be longer than UPPER_CASE_WORD_MIN_LEN chars
						if (end_len < UPPER_CASE_WORD_MIN_LEN)
								continue;

						global_attr = entry.attr;
						end_condition_len = end_len;
						return;

				not_upper_case:
						continue;
				}

				int len = strlen(entry.arg.start);

				if (mode == COLOR_WORD_BEFORE_STR || mode == COLOR_WORD_BEFORE_STR_STR || mode == COLOR_WORD_ENDING_WITH_STR) {
						// check if this is a new word
						if (str_contains_char(cs->word_seperators, buf[offset])) continue;

						int offset_tmp = offset;
						// find new word twice if it's BEFORE_STR_STR
						int times = mode == COLOR_WORD_BEFORE_STR_STR ? 2 : 1;
						int first_word_len = 0;
						int first_time = 1;
						while (times--) {
								// seek end of word
								offset_tmp = fb_seek_word_end(fb, offset_tmp, cs->word_seperators);
								if (offset_tmp == offset && mode == COLOR_WORD_BEFORE_STR_STR)
										goto exit_word_before_str_str;
								if (first_time)
										first_word_len = offset_tmp - offset;

								if (mode != COLOR_WORD_ENDING_WITH_STR)
										offset_tmp = fb_seek_not_whitespace(fb, offset_tmp);

								first_time = 0;
						}

						if (mode == COLOR_WORD_ENDING_WITH_STR) {
								offset_tmp -= len;
								if (offset_tmp < 0)
										continue;
						}
						if (fb_offset_starts_with(fb, offset_tmp, entry.arg.start)) {
								global_attr = entry.attr;
								end_condition_len = first_word_len;
								return;
						}
				exit_word_before_str_str:
						continue;
				}

				if (mode == COLOR_INSIDE || mode == COLOR_INSIDE_TO_LINE || mode == COLOR_WORD_INSIDE) {
						if (offset - len < 0)
								continue;
						// check the if what's behind the cursor is the first string
						if (fb_offset_starts_with(fb, offset - len, entry.arg.start)) {
								if (offset < fb->len && fb_offset_starts_with(fb, offset, entry.arg.end))
										continue;

								if (mode == COLOR_WORD_INSIDE) {
										// verify that only one word exists inside
										int offset_tmp = offset;
										offset_tmp = fb_seek_not_whitespace(fb, offset_tmp);
										offset_tmp = fb_seek_whitespace(fb, offset_tmp);
										int offset_tmp1 = offset_tmp - strlen(entry.arg.end);
										offset_tmp = fb_seek_not_whitespace(fb, offset_tmp);

										if ((!fb_offset_starts_with(fb, offset_tmp, entry.arg.end)
											 && !fb_offset_starts_with(fb, offset_tmp1, entry.arg.end))
											|| offset_tmp1 - offset <= 1 || offset_tmp - offset <= 1)
												continue;
								} else if (mode == COLOR_INSIDE_TO_LINE) {
										if (fb_seek_char(fb, offset, '\n') < fb_seek_string(fb, offset, entry.arg.end))
												continue;
								}


								end_condition = entry.arg.end;
								end_condition_len = strlen(entry.arg.end);
								global_attr = entry.attr;
								around = 0;
								return;
						}
						continue;
				}

				if ((mode == COLOR_AROUND || mode == COLOR_AROUND_TO_LINE) &&
					fb_offset_starts_with(fb, offset, entry.arg.start)) {
						end_condition = entry.arg.end;
						end_condition_len = strlen(entry.arg.end);
						around = 1;
						if (entry.mode == COLOR_AROUND_TO_LINE)
								end_at_whitespace = 1;
						global_attr = entry.attr;
						return;
				}
				if (mode == COLOR_WORD || mode == COLOR_STR_AFTER_WORD ||
					mode == COLOR_WORD_STR || mode == COLOR_WORD_STARTING_WITH_STR) {

						// check if this is the start of a new word that matches word exactly(except for WORD_STARTING_WITH_STR)
						if(!fb_offset_starts_with(fb, offset, entry.arg.start) ||
						   !fb_is_start_of_a_word(fb, offset, cs->word_seperators) ||
						   (fb_is_on_a_word(fb, offset + len, cs->word_seperators) && mode != COLOR_WORD_STARTING_WITH_STR))
								continue;

						if (mode == COLOR_WORD_STR) {
								int offset_str = fb_seek_not_whitespace(fb, offset + len);

								if (!fb_offset_starts_with(fb, offset_str, entry.arg.end))
										continue;
								end_condition_len = strlen(entry.arg.start);
						} else {
								end_at_whitespace = 1;
						}
						if (mode == COLOR_STR_AFTER_WORD) {
								next_word_attr = entry.attr;
								color_next_word = 1;
								continue;
						}
						global_attr = entry.attr;
						return;
				}
				if (mode == COLOR_STR) {
						if (!fb_offset_starts_with(fb, offset, entry.arg.start))
								continue;
						end_condition_len = len;
						global_attr = entry.attr;
						return;
				}
		}
}


////////////////////////
// Auto indent
//
//

int
fb_auto_indent(struct file_buffer* fb, int offset)
{
		const struct syntax_scheme* cs = fb_get_syntax_scheme(fb);
		LIMIT(offset, 0, fb->len-1);

		int indent_diff = 0;
		int indent_keep_x = -1;
		int keep_pos = 0;

		int get_line_offset;
		char* get_line = NULL;

		for (int i = 0; i < cs->indent_count; i++) {
				const struct indent_scheme_entry indent = cs->indents[i];

				get_line_offset = get_line_relative_offset(fb, offset, indent.line_offset);
				if (get_line)
						free(get_line);
				get_line = fb_get_line_at_offset(fb, get_line_offset);

				switch(indent.mode) {
						int temp_offset, len;
						char* res;
				case INDENT_LINE_CONTAINS_WORD:
				case INDENT_LINE_ONLY_CONTAINS_STR:
				case INDENT_LINE_CONTAINS_STR_MORE_THAN_STR:
						res = strstr(get_line, indent.arg.start);
						if (!res)
								continue;
						if (INDENT_LINE_CONTAINS_WORD) {
								if (res > get_line && !str_contains_char(cs->word_seperators, *(res-1)))
										continue;
								res += strlen(indent.arg.start);
								if (*res && !str_contains_char(cs->word_seperators, *res))
										continue;
						} else if (INDENT_LINE_CONTAINS_STR_MORE_THAN_STR == indent.mode) {
								char* start_last = get_line;
								char* end_last = get_line;
								for (int count = 0; count >= 0; ) {
										if (start_last && (start_last = strstr(start_last, indent.arg.start))) {
												start_last++;
												count--;
										} else {
												goto indent_for_loop_continue;
										}
										if (end_last && (end_last = strstr(end_last, indent.arg.end))) {
												end_last++;
												count++;
										}
								}
						} else if (indent.mode == INDENT_LINE_ONLY_CONTAINS_STR) {
								int str_start = fb_seek_string_backwards(fb, get_line_offset, indent.arg.start);
								int str_end = str_start + strlen(indent.arg.start);
								int line_end = MIN(get_line_offset + 1, fb->len);
								int line_start = MAX(fb_seek_char_backwards(fb, get_line_offset, '\n'), 0);
								if (fb_seek_not_whitespace_backwards(fb, str_start-1) >= line_start ||
									fb_seek_not_whitespace(fb, str_end+1) <= line_end)
										continue;
						}
						if (indent.type == INDENT_KEEP_OPENER || indent.type == INDENT_RETURN_TO_OPENER_BASE_INDENT)
								keep_pos = fb_seek_string_backwards(fb, get_line_offset, indent.arg.start);
						goto set_indent_type;

				case INDENT_LINE_ENDS_WITH_STR:
				case INDENT_LINE_DOES_NOT_END_WITH_STR:
						len = strlen(indent.arg.start);
						temp_offset = fb_seek_not_whitespace_backwards(fb, get_line_offset) - len;
						if (temp_offset < 0 ||
							temp_offset < fb_seek_char_backwards(fb, get_line_offset, '\n'))
								continue;
						if (memcmp(fb->contents + get_line_offset, indent.arg.start, strlen(indent.arg.start)) == 0) {
								if (indent.mode == INDENT_LINE_DOES_NOT_END_WITH_STR)
										continue;
						} else {
								if (indent.mode == INDENT_LINE_ENDS_WITH_STR)
										continue;
						}
						keep_pos = temp_offset;
						goto set_indent_type;

				set_indent_type:
						if (indent.type == INDENT_KEEP_OPENER) {
								if (indent_keep_x >= 0)
										continue;
								int tmp;
								fb_offset_to_xy(fb, keep_pos, 0, 0, &indent_keep_x, &tmp, &tmp);
						} else if (indent.type == INDENT_RETURN_TO_OPENER_BASE_INDENT) {
								if (indent_keep_x >= 0)
										continue;
								int opener, closer;
								if (!fb_get_delimiter(fb, keep_pos, indent.arg, NULL, &opener, &closer)) {
										indent_keep_x = -1;
										goto indent_for_loop_continue;
								}
								keep_pos = fb_seek_not_whitespace(fb, fb_seek_char_backwards(fb, opener, '\n'));
								int tmp;
								fb_offset_to_xy(fb, keep_pos, 0, 0, &indent_keep_x, &tmp, &tmp);
								//TODO: why does this miss by one?
								if (indent_keep_x > 0)
										indent_keep_x--;
						} else {
								if (indent_diff)
										continue;
								indent_diff += indent.type;
						}
				}
		indent_for_loop_continue:
				continue;
		}

		if (get_line)
				free(get_line);

		int indents = 0, extra_spaces = 0;

		int prev_line_offset = fb_seek_char_backwards(fb, offset, '\n') - 1;
		if (prev_line_offset < 0)
				return 0;
		char* prev_line = fb_get_line_at_offset(fb, prev_line_offset);

		if (indent_keep_x >= 0) {
				whitespace_count_to_indent_amount(fb->indent_len, indent_keep_x, &indents, &extra_spaces);
		} else {
				whitespace_count_to_indent_amount(fb->indent_len, get_line_leading_whitespace_count(prev_line), &indents, &extra_spaces);
		}
		if (indent_diff != INDENT_KEEP) {
				indents += indent_diff;
				indents = MAX(indents, 0);
		}

		// remove the lines existing indent
		int removed = 0;
		int line_code_start = MIN(fb_seek_not_whitespace(fb, prev_line_offset + 1), fb_seek_char(fb, prev_line_offset + 1, '\n'));
		if (line_code_start - (prev_line_offset) >= 1) {
				removed = line_code_start - (prev_line_offset+1);
				fb_remove(fb, prev_line_offset + 1, removed, 1, 1);
		}

		if (indents + extra_spaces <= 0) {
				free(prev_line);
				return -removed;
		}

		unsigned int indent_str_len = 0;
		while(indents--)
				indent_str_len += fb->indent_len ? fb->indent_len : 1;
		char indent_str[indent_str_len + extra_spaces];

		char space_tab = fb->indent_len > 0 ? ' ' : '\t';
		int i = 0;
		if (!fb->indent_len)
				for (/* i = 0 */; i < indent_str_len; i++)
						indent_str[i] = space_tab;
		for (/* i = 0 or indent_str_len */ ; i < indent_str_len + extra_spaces; i++)
				indent_str[i] = ' ';

		fb_insert(fb, indent_str, indent_str_len + extra_spaces, prev_line_offset + 1, 0);

		free(prev_line);
		return indent_str_len + extra_spaces - removed;
}

int get_line_leading_whitespace_count(const char* line)
{
		int count = 0;
		while(*line) {
				if (*line == ' ')
						count++;
				else if (*line == '\t')
						count += tabspaces - (count % tabspaces);
				else
						break;
				line++;
		}
		return count;
}

void
whitespace_count_to_indent_amount(int indent_len, int count, int* indent_count, int* extra_spaces)
{
		*indent_count = 0, *extra_spaces = 0;
		int space_count = indent_len > 0 ? indent_len : tabspaces;
		while(count >= space_count) {
				*indent_count += 1;
				count -= space_count;
		}
		*extra_spaces = count;
}

int
get_line_relative_offset(struct file_buffer* fb, int offset, int count)
{
		offset = fb_seek_char(fb, offset, '\n');
		if (offset < 0)
				offset = fb->len;
		if (count > 0) {
				while(count-- && offset >= 0)
						offset = fb_seek_char(fb, offset+1, '\n');
				if (offset < 0)
						offset = fb->len;
		} else if (count < 0) {
				offset = fb_seek_char_backwards(fb, offset, '\n');
				while(count++ && offset >= 0)
						offset = fb_seek_char_backwards(fb, offset-1, '\n');
				if (offset < 0)
						offset = 0;
		}
		offset = fb_seek_char(fb, offset, '\n');
		if (offset > 0 && fb->contents[offset-1] != '\n')
				offset--;
		if (offset < 0)
				offset = fb->len;
		return offset;
}


#endif // SYNTAX_H_
