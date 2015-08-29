/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  u-parse.c
**  Summary: parse dialect interpreter
**  Section: utility
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

// Parser flags:
enum Parse_Flags {
	PF_CASE = 1 << 0,
	PF_CASED = 1 << 1 // was set as initial option
};

typedef struct reb_parse {
	REBSER *series;
	enum Reb_Kind type;
	REBCNT flags;
	REBINT result;
	REBVAL *out;
} REBPARSE;

enum parse_flags {
	PF_SET_OR_COPY, // test PF_COPY first; if false, this means PF_SET
	PF_COPY,
	PF_NOT,
	PF_NOT2,
	PF_THEN,
	PF_AND,
	PF_REMOVE,
	PF_INSERT,
	PF_CHANGE,
	PF_RETURN,
	PF_WHILE,
	PF_MAX
};

#define MAX_PARSE_DEPTH 512

// Returns SYMBOL or 0 if not a command:
#define GET_CMD(n) (((n) >= SYM_OR_BAR && (n) <= SYM_END) ? (n) : 0)
#define VAL_CMD(v) GET_CMD(VAL_WORD_CANON(v))
#define HAS_CASE(p) (p->flags & AM_FIND_CASE)
#define IS_OR_BAR(v) (IS_WORD(v) && VAL_WORD_CANON(v) == SYM_OR_BAR)
#define SKIP_TO_BAR(r) while (NOT_END(r) && !IS_SAME_WORD(r, SYM_OR_BAR)) r++;
#define IS_BLOCK_INPUT(p) (p->type >= REB_BLOCK)

static REBCNT Parse_Rules_Loop(REBPARSE *parse, REBCNT index, const REBVAL *rules, REBCNT depth);

void Print_Parse_Index(enum Reb_Kind type, const REBVAL *rules, REBSER *series, REBCNT index)
{
	REBVAL val;
	Val_Init_Series(&val, type, series);
	VAL_INDEX(&val) = index;
	Debug_Fmt("%r: %r", rules, &val);
}


/*******************************************************************************
**
**  Name: "Set_Parse_Series"
**  Summary: none
**  Details: {
**      Change the series and return the new index.}
**  Spec: none
**
*******************************************************************************/

static REBCNT Set_Parse_Series(REBPARSE *parse, const REBVAL *item)
{
	parse->series = VAL_SERIES(item);
	parse->type = VAL_TYPE(item);
	if (IS_BINARY(item) || (parse->flags & PF_CASED)) parse->flags |= PF_CASE;
	else parse->flags &= ~PF_CASE;
	return (VAL_INDEX(item) > VAL_TAIL(item)) ? VAL_TAIL(item) : VAL_INDEX(item);
}


/***********************************************************************
**
*/	static const REBVAL *Get_Parse_Value(REBVAL *safe, const REBVAL *item)
/*
**		Get the value of a word (when not a command) or path.
**		Returns all other values as-is.
**
**		!!! Because path evaluation does not necessarily wind up
**		pointing to a variable that exists in memory, a derived
**		value may be created during that process.  Previously
**		this derived value was kept on the stack, but that
**		meant every path evaluation PUSH'd without a known time
**		at which a corresponding DROP would be performed.  To
**		avoid the stack overflow, this requires you to pass in
**		a "safe" storage value location that will be good for
**		as long as the returned pointer is needed.  It *may*
**		not be used in the case of a word fetch, so pay attention
**		to the return value and not the contents of that variable.
**
**		!!! (Review if this can be done a better way.)
**
***********************************************************************/
{
	if (IS_WORD(item)) {
		// !!! Should this be getting mutable variables?  If not, how
		// does it guarantee it is honoring the protection status?
		if (!VAL_CMD(item)) item = GET_VAR(item);
	}
	else if (IS_PATH(item)) {
		const REBVAL *path = item;
		if (Do_Path(safe, &path, 0)) return item; // found a function
		item = safe;
	}
	return item;
}


/*******************************************************************************
**
**  Name: "Parse_Next_String"
**  Summary: none
**  Details: {
**      Match the next item in the string ruleset.
**  
**      If it matches, return the index just past it.
**      Otherwise return NOT_FOUND.}
**  Spec: none
**
*******************************************************************************/

static REBCNT Parse_Next_String(REBPARSE *parse, REBCNT index, const REBVAL *item, REBCNT depth)
{
	// !!! THIS CODE NEEDS CLEANUP AND REWRITE BASED ON OTHER CHANGES
	REBSER *series = parse->series;
	REBSER *ser;
	REBCNT flags = parse->flags | AM_FIND_MATCH | AM_FIND_TAIL;
	int rewrite_needed;
	REBVAL save;

	if (Trace_Level) {
		Trace_Value(7, item);
		Trace_String(8, STR_SKIP(series, index), series->tail - index);
	}

	if (IS_NONE(item)) return index;

	if (index >= series->tail) return NOT_FOUND;

	switch (VAL_TYPE(item)) {

	// Do we match a single character?
	case REB_CHAR:
		if (HAS_CASE(parse))
			index = (VAL_CHAR(item) == GET_ANY_CHAR(series, index)) ? index+1 : NOT_FOUND;
		else
			index = (UP_CASE(VAL_CHAR(item)) == UP_CASE(GET_ANY_CHAR(series, index))) ? index+1 : NOT_FOUND;
		break;

	case REB_EMAIL:
	case REB_STRING:
	case REB_BINARY:
		index = Find_Str_Str(series, 0, index, SERIES_TAIL(series), 1, VAL_SERIES(item), VAL_INDEX(item), VAL_LEN(item), flags);
		break;

	// Do we match to a char set?
	case REB_BITSET:
		flags = Check_Bit(VAL_SERIES(item), GET_ANY_CHAR(series, index), !HAS_CASE(parse));
		index = flags ? index + 1 : NOT_FOUND;
		break;
/*
	case REB_DATATYPE:	// Currently: integer!
		if (VAL_TYPE_KIND(item) == REB_INTEGER) {
			REBCNT begin = index;
			while (IS_LEX_NUMBER(*str)) str++, index++;
			if (begin == index) index = NOT_FOUND;
		}
		break;
*/
	case REB_TAG:
	case REB_FILE:
//	case REB_ISSUE:
		// !! Can be optimized (w/o COPY)
		ser = Copy_Form_Value(item, 0);
		index = Find_Str_Str(series, 0, index, SERIES_TAIL(series), 1, ser, 0, ser->tail, flags);
		Free_Series(ser);
		break;

	case REB_NONE:
		break;

	// Parse a sub-rule block:
	case REB_BLOCK:
		index = Parse_Rules_Loop(parse, index, VAL_BLK_DATA(item), depth);
		break;

	// Do an expression:
	case REB_PAREN:
		// might GC
		if (Do_Block_Throws(&save, VAL_SERIES(item), 0)) {
			*parse->out = save;
			Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
			DEAD_END;
		}

		item = &save;
        index = MIN(index, series->tail); // may affect tail
		break;

	default:
		Trap1_DEAD_END(RE_PARSE_RULE, item);
	}

	return index;
}


/*******************************************************************************
**
**  Name: "Parse_Next_Block"
**  Summary: none
**  Details: {
**      Used for parsing blocks to match the next item in the ruleset.
**      If it matches, return the index just past it. Otherwise, return zero.}
**  Spec: none
**
*******************************************************************************/

static REBCNT Parse_Next_Block(REBPARSE *parse, REBCNT index, const REBVAL *item, REBCNT depth)
{
	// !!! THIS CODE NEEDS CLEANUP AND REWRITE BASED ON OTHER CHANGES
	REBSER *series = parse->series;
	REBVAL *blk = BLK_SKIP(series, index);
	REBVAL save;

	if (Trace_Level) {
		Trace_Value(7, item);
		Trace_Value(8, blk);
	}

	switch (VAL_TYPE(item)) {

	// Look for specific datattype:
	case REB_DATATYPE:
		index++;
		if (VAL_TYPE(blk) == VAL_TYPE_KIND(item)) break;
		goto no_result;

	// Look for a set of datatypes:
	case REB_TYPESET:
		index++;
		if (TYPE_CHECK(item, VAL_TYPE(blk))) break;
		goto no_result;

	// 'word
	case REB_LIT_WORD:
		index++;
		if (IS_WORD(blk) && (VAL_WORD_CANON(blk) == VAL_WORD_CANON(item))) break;
		goto no_result;

	case REB_LIT_PATH:
		index++;
		if (IS_PATH(blk) && !Cmp_Block(blk, item, 0)) break;
		goto no_result;

	case REB_NONE:
		break;

	// Parse a sub-rule block:
	case REB_BLOCK:
		index = Parse_Rules_Loop(parse, index, VAL_BLK_DATA(item), depth);
		break;

	// Do an expression:
	case REB_PAREN:
		// might GC
		if (Do_Block_Throws(&save, VAL_SERIES(item), 0)) {
			*parse->out = save;
			Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
			DEAD_END;
		}
		item = &save;
		// old: if (IS_ERROR(item)) Throw_Error(VAL_ERR_OBJECT(item));
        index = MIN(index, series->tail); // may affect tail
		break;

	// Match with some other value:
	default:
		index++;
		if (Cmp_Value(blk, item, (REBOOL)HAS_CASE(parse))) goto no_result;
	}

	return index;

no_result:
	return NOT_FOUND;
}


/*******************************************************************************
**
**  Name: "To_Thru"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static REBCNT To_Thru(REBPARSE *parse, REBCNT index, const REBVAL *block, REBFLG is_thru)
{
	REBSER *series = parse->series;
	REBCNT type = parse->type;
	REBVAL *blk;
	const REBVAL *item;
	REBCNT cmd;
	REBCNT i;
	REBCNT len;
	REBVAL save;

	for (; index <= series->tail; index++) {

		for (blk = VAL_BLK_HEAD(block); NOT_END(blk); blk++) {

			item = blk;

			// Deal with words and commands
			if (IS_WORD(item)) {
				if ((cmd = VAL_CMD(item))) {
					if (cmd == SYM_END) {
						if (index >= series->tail) {
							index = series->tail;
							goto found;
						}
						goto next;
					}
					else if (cmd == SYM_QUOTE) {
						item = ++blk; // next item is the quoted value
						if (IS_END(item)) goto bad_target;
						if (IS_PAREN(item)) {
							// might GC
							if (Do_Block_Throws(&save, VAL_SERIES(item), 0)) {
								*parse->out = save;
								Trap(RE_PARSE_LONGJMP_HACK);
								DEAD_END;
							}
							item = &save;
						}

					}
					else goto bad_target;
				}
				else {
					// !!! Should mutability be enforced?  It might have to
					// be if set/copy are used...
					item = GET_MUTABLE_VAR(item);
				}
			}
			else if (IS_PATH(item)) {
				item = Get_Parse_Value(&save, item);
			}

			// Try to match it:
			if (type >= REB_BLOCK) {
				if (ANY_BLOCK(item)) goto bad_target;
				i = Parse_Next_Block(parse, index, item, 0);
				if (i != NOT_FOUND) {
					if (!is_thru) i--;
					index = i;
					goto found;
				}
			}
			else if (type == REB_BINARY) {
				REBYTE ch1 = *BIN_SKIP(series, index);

				// Handle special string types:
				if (IS_CHAR(item)) {
					if (VAL_CHAR(item) > 0xff) goto bad_target;
					if (ch1 == VAL_CHAR(item)) goto found1;
				}
				else if (IS_BINARY(item)) {
					if (ch1 == *VAL_BIN_DATA(item)) {
						len = VAL_LEN(item);
						if (len == 1) goto found1;
						if (0 == Compare_Bytes(BIN_SKIP(series, index), VAL_BIN_DATA(item), len, 0)) {
							if (is_thru) index += len;
							goto found;
						}
					}
				}
				else if (IS_INTEGER(item)) {
					if (VAL_INT64(item) > 0xff) goto bad_target;
					if (ch1 == VAL_INT32(item)) goto found1;
				}
				else goto bad_target;
			}
			else { // String
				REBCNT ch1 = GET_ANY_CHAR(series, index);
				REBCNT ch2;

				if (!HAS_CASE(parse)) ch1 = UP_CASE(ch1);

				// Handle special string types:
				if (IS_CHAR(item)) {
					ch2 = VAL_CHAR(item);
					if (!HAS_CASE(parse)) ch2 = UP_CASE(ch2);
					if (ch1 == ch2) goto found1;
				}
				else if (ANY_STR(item)) {
					ch2 = VAL_ANY_CHAR(item);
					if (!HAS_CASE(parse)) ch2 = UP_CASE(ch2);
					if (ch1 == ch2) {
						len = VAL_LEN(item);
						if (len == 1) goto found1;
						i = Find_Str_Str(series, 0, index, SERIES_TAIL(series), 1, VAL_SERIES(item), VAL_INDEX(item), len, AM_FIND_MATCH | parse->flags);
						if (i != NOT_FOUND) {
							if (is_thru) i += len;
							index = i;
							goto found;
						}
					}
				}
				else if (IS_INTEGER(item)) {
					ch1 = GET_ANY_CHAR(series, index);  // No casing!
					if (ch1 == (REBCNT)VAL_INT32(item)) goto found1;
				}
				else goto bad_target;
			}

next:		// Check for | (required if not end)
			blk++;
			if (IS_PAREN(blk)) blk++;
			if (IS_END(blk)) break;
			if (!IS_OR_BAR(blk)) {
				item = blk;
				goto bad_target;
			}
		}
	}
	return NOT_FOUND;

found:
	if (IS_PAREN(blk + 1)) {
		REBVAL evaluated;
		if (Do_Block_Throws(&evaluated, VAL_SERIES(blk + 1), 0)) {
			*parse->out = evaluated;
			Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
			DEAD_END;
		}
		// !!! ignore evaluated if it's not THROWN?
	}
	return index;

found1:
	if (IS_PAREN(blk + 1)) {
		REBVAL evaluated;
		if (Do_Block_Throws(&evaluated, VAL_SERIES(blk + 1), 0)) {
			*parse->out = save;
			Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
			DEAD_END;
		}
		// !!! ignore evaluated if it's not THROWN?
	}
	return index + (is_thru ? 1 : 0);

bad_target:
	Trap1_DEAD_END(RE_PARSE_RULE, item);
	return 0;
}


/*******************************************************************************
**
**  Name: "Parse_To"
**  Summary: none
**  Details: {
**      Parse TO a specific:
**          1. integer - index position
**          2. END - end of input
**          3. value - according to datatype
**          4. block of values - the first one we hit}
**  Spec: none
**
*******************************************************************************/

static REBCNT Parse_To(REBPARSE *parse, REBCNT index, const REBVAL *item, REBFLG is_thru)
{
	REBSER *series = parse->series;
	REBCNT i;
	REBSER *ser;

	// TO a specific index position.
	if (IS_INTEGER(item)) {
		i = (REBCNT)Int32(item) - (is_thru ? 0 : 1);
		if (i > series->tail) i = series->tail;
	}
	// END
	else if (IS_WORD(item) && VAL_WORD_CANON(item) == SYM_END) {
		i = series->tail;
	}
	else if (IS_BLOCK(item)) {
		i = To_Thru(parse, index, item, is_thru);
	}
	else {
		if (IS_BLOCK_INPUT(parse)) {
			REBVAL word; /// !!!Temp, but where can we put it?
			if (IS_LIT_WORD(item)) {  // patch to search for word, not lit.
				word = *item;
				VAL_SET(&word, REB_WORD);
				item = &word;
			}
			///i = Find_Value(series, index, tail-index, item, 1, (REBOOL)(PF_CASE & flags), FALSE, 1);
			i = Find_Block(series, index, series->tail, item, 1, HAS_CASE(parse)?AM_FIND_CASE:0, 1);
			if (i != NOT_FOUND && is_thru) i++;
		}
		else {
			// "str"
			if (ANY_BINSTR(item)) {
				if (!IS_STRING(item) && !IS_BINARY(item)) {
					// !!! Can this be optimized not to use COPY?
					ser = Copy_Form_Value(item, 0);
					i = Find_Str_Str(series, 0, index, series->tail, 1, ser, 0, ser->tail, HAS_CASE(parse));
					if (i != NOT_FOUND && is_thru) i += ser->tail;
					Free_Series(ser);
				}
				else {
					i = Find_Str_Str(series, 0, index, series->tail, 1, VAL_SERIES(item), VAL_INDEX(item), VAL_LEN(item), HAS_CASE(parse));
					if (i != NOT_FOUND && is_thru) i += VAL_LEN(item);
				}
			}
			// #"A"
			else if (IS_CHAR(item)) {
				i = Find_Str_Char(series, 0, index, series->tail, 1, VAL_CHAR(item), HAS_CASE(parse));
				if (i != NOT_FOUND && is_thru) i++;
			}
			// bitset
			else if (IS_BITSET(item)) {
				i = Find_Str_Bitset(series, 0, index, series->tail, 1, VAL_BITSET(item), HAS_CASE(parse));
				if (i != NOT_FOUND && is_thru) i++;
			}
			else {
				assert(FALSE);
				DEAD_END;
			}
		}
	}

	return i;
}


/*******************************************************************************
**
**  Name: "Do_Eval_Rule"
**  Summary: none
**  Details: {
**      Evaluate the input as a code block. Advance input if
**      rule succeeds. Return new index or failure.
**  
**      Examples:
**          do skip
**          do end
**          do "abc"
**          do 'abc
**          do [...]
**          do variable
**          do datatype!
**          do quote 123
**          do into [...]
**  
**      Problem: cannot write:  set var do datatype!}
**  Spec: none
**
*******************************************************************************/

static REBCNT Do_Eval_Rule(REBPARSE *parse, REBCNT index, const REBVAL **rule)
{
	REBVAL value;
	const REBVAL *item = *rule;
	REBCNT n;
	REBPARSE newparse;
	REBVAL save; // REVIEW: Could this just reuse value?

	// First, check for end of input:
	if (index >= parse->series->tail) {
		if (IS_WORD(item) && VAL_CMD(item) == SYM_END) return index;
		else return NOT_FOUND;
	}

	// Evaluate next N input values:
	index = Do_Next_May_Throw(&value, parse->series, index);

	if (index == THROWN_FLAG) {
		// Value is a THROW, RETURN, BREAK, etc...we have to stop processing
		*parse->out = value;
		Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
		DEAD_END;
	}

	// Get variable or command:
	if (IS_WORD(item)) {

		n = VAL_CMD(item);

		if (n == SYM_SKIP)
			return (IS_SET(&value)) ? index : NOT_FOUND;

		if (n == SYM_QUOTE) {
			item = item + 1;
			(*rule)++;
			if (IS_END(item)) Trap1_DEAD_END(RE_PARSE_END, item-2);
			if (IS_PAREN(item)) {
				// might GC
				if (Do_Block_Throws(&save, VAL_SERIES(item), 0)) {
					*parse->out = save;
					Trap(RE_PARSE_LONGJMP_HACK);
					DEAD_END;
				}
				item = &save;
			}
		}
		else if (n == SYM_INTO) {
			REBPARSE sub_parse;

			item = item + 1;
			(*rule)++;
			if (IS_END(item)) Trap1_DEAD_END(RE_PARSE_END, item-2);
			item = Get_Parse_Value(&save, item); // sub-rules
			if (!IS_BLOCK(item)) Trap1_DEAD_END(RE_PARSE_RULE, item-2);
			if (!ANY_BINSTR(&value) && !ANY_BLOCK(&value)) return NOT_FOUND;

			sub_parse.series = VAL_SERIES(&value);
			sub_parse.type = VAL_TYPE(&value);
			sub_parse.flags = parse->flags;
			sub_parse.result = 0;
			sub_parse.out = parse->out;

			if (
				VAL_TAIL(&value) == Parse_Rules_Loop(
					&sub_parse, VAL_INDEX(&value), VAL_BLK_DATA(item), 0
				)
			) {
				return index;
			}

			return NOT_FOUND;
		}
		else if (n > 0)
			Trap1_DEAD_END(RE_PARSE_RULE, item);
		else
			item = Get_Parse_Value(&save, item); // variable
	}
	else if (IS_PATH(item)) {
		item = Get_Parse_Value(&save, item); // variable
	}
	else if (IS_SET_WORD(item) || IS_GET_WORD(item) || IS_SET_PATH(item) || IS_GET_PATH(item))
		Trap1_DEAD_END(RE_PARSE_RULE, item);

	if (IS_NONE(item)) {
		return (VAL_TYPE(&value) > REB_NONE) ? NOT_FOUND : index;
	}

	// Copy the value into its own block:
	newparse.series = Make_Array(1);
	SAVE_SERIES(newparse.series);
	Append_Value(newparse.series, &value);
	newparse.type = REB_BLOCK;
	newparse.flags = parse->flags;
	newparse.result = 0;
	newparse.out = parse->out;

	n = (Parse_Next_Block(&newparse, 0, item, 0) != NOT_FOUND) ? index : NOT_FOUND;
	UNSAVE_SERIES(newparse.series);
	return n;
}


/*******************************************************************************
**
**  Name: "Parse_Rules_Loop"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static REBCNT Parse_Rules_Loop(REBPARSE *parse, REBCNT index, const REBVAL *rules, REBCNT depth)
{
	REBSER *series = parse->series;
	const REBVAL *item;		// current rule item
	const REBVAL *word;		// active word to be set
	REBCNT start;		// recovery restart point
	REBCNT i;			// temp index point
	REBCNT begin;		// point at beginning of match
	REBINT count;		// iterated pattern counter
	REBINT mincount;	// min pattern count
	REBINT maxcount;	// max pattern count
	const REBVAL *item_hold;
	REBVAL *val;		// spare
	REBCNT rulen;
	REBFLG flags;
	REBCNT cmd;
	const REBVAL *rule_head = rules;
	REBVAL save;

	CHECK_C_STACK_OVERFLOW(&flags);
	//if (depth > MAX_PARSE_DEPTH) vTrap_Word(RE_LIMIT_HIT, SYM_PARSE, 0);
	flags = 0;
	word = 0;
	mincount = maxcount = 1;
	start = begin = index;

	// For each rule in the rule block:
	while (NOT_END(rules)) {

		//Print_Parse_Index(parse->type, rules, series, index);

		if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

		//--------------------------------------------------------------------
		// Pre-Rule Processing Section
		//
		// For non-iterated rules, including setup for iterated rules.
		// The input index is not advanced here, but may be changed by
		// a GET-WORD variable.
		//--------------------------------------------------------------------

		item = rules++;

		// If word, set-word, or get-word, process it:
		if (VAL_TYPE(item) >= REB_WORD && VAL_TYPE(item) <= REB_GET_WORD) {

			// Is it a command word?
			if ((cmd = VAL_CMD(item))) {

				if (!IS_WORD(item)) Trap1_DEAD_END(RE_PARSE_COMMAND, item); // SET or GET not allowed

				if (cmd <= SYM_BREAK) { // optimization

					switch (cmd) {

					case SYM_OR_BAR:
						return index;	// reached it successfully

					// Note: mincount = maxcount = 1 on entry
					case SYM_WHILE:
						SET_FLAG(flags, PF_WHILE);
					case SYM_ANY:
						mincount = 0;
					case SYM_SOME:
						maxcount = MAX_I32;
						continue;

					case SYM_OPT:
						mincount = 0;
						continue;

					case SYM_COPY:
						SET_FLAG(flags, PF_COPY);
					case SYM_SET:
						SET_FLAG(flags, PF_SET_OR_COPY);
						item = rules++;
						if (!(IS_WORD(item) || IS_SET_WORD(item))) Trap1_DEAD_END(RE_PARSE_VARIABLE, item);
						if (VAL_CMD(item)) Trap1_DEAD_END(RE_PARSE_COMMAND, item);
						word = item;
						continue;

					case SYM_NOT:
						SET_FLAG(flags, PF_NOT);
						flags ^= (1<<PF_NOT2);
						continue;

					case SYM_AND:
						SET_FLAG(flags, PF_AND);
						continue;

					case SYM_THEN:
						SET_FLAG(flags, PF_THEN);
						continue;

					case SYM_REMOVE:
						SET_FLAG(flags, PF_REMOVE);
						continue;

					case SYM_INSERT:
						SET_FLAG(flags, PF_INSERT);
						goto post;

					case SYM_CHANGE:
						SET_FLAG(flags, PF_CHANGE);
						continue;

					// There are two RETURNs: one is a matching form, so with
					// 'parse data [return "abc"]' you are not asking to
					// return the literal string "abc" independent of input.
					// it will only return if "abc" matches.  This works for
					// a rule reference as well, such as 'return rule'.
					//
					// The second option is if you put the value in parens,
					// in which case it will just return whatever that value
					// happens to be, e.g. 'parse data [return ("abc")]'

					case SYM_RETURN:
						if (IS_PAREN(rules)) {
							if (Do_Block_Throws(
								parse->out, VAL_SERIES(rules), 0
							)) {
								// If the paren evaluation result gives a
								// THROW, BREAK, CONTINUE, etc then we'll
								// return that (but we were returning anyway,
								// so just fall through)
							}

							// Implicitly returns whatever's in parse->out
							// !!! Should return gracefully...!
							Trap(RE_PARSE_LONGJMP_HACK);
							DEAD_END;
						}
						SET_FLAG(flags, PF_RETURN);
						continue;

					case SYM_ACCEPT:
					case SYM_BREAK:
						parse->result = 1;
						return index;

					case SYM_REJECT:
						parse->result = -1;
						return index;

					case SYM_FAIL:
						index = NOT_FOUND;
						goto post;

					case SYM_IF:
						item = rules++;
						if (IS_END(item)) goto bad_end;
						if (!IS_PAREN(item)) Trap1_DEAD_END(RE_PARSE_RULE, item);

						// might GC
						if (Do_Block_Throws(&save, VAL_SERIES(item), 0)) {
							*parse->out = save;
							Trap(RE_PARSE_LONGJMP_HACK);
							DEAD_END;
						}

						item = &save;
						if (IS_CONDITIONAL_TRUE(item)) continue;
						else {
							index = NOT_FOUND;
							goto post;
						}

					case SYM_LIMIT:
						Trap_DEAD_END(RE_NOT_DONE);
						//val = Get_Parse_Value(&save, rules++);
					//	if (IS_INTEGER(val)) limit = index + Int32(val);
					//	else if (ANY_SERIES(val)) limit = VAL_INDEX(val);
					//	else goto
						//goto bad_rule;
					//	goto post;

					case SYM_QQ:
						Print_Parse_Index(parse->type, rules, series, index);
						continue;
					}
				}
				// Any other cmd must be a match command, so proceed...

			} else { // It's not a PARSE command, get or set it:

				// word: - set a variable to the series at current index
				if (IS_SET_WORD(item)) {
					REBVAL temp;
					Val_Init_Series_Index(&temp, parse->type, series, index);

					Set_Var(item, &temp);

					continue;
				}

				// :word - change the index for the series to a new position
				if (IS_GET_WORD(item)) {
					// !!! Should mutability be enforced?
					item = GET_MUTABLE_VAR(item);
					// CureCode #1263 change
					//if (parse->type != VAL_TYPE(item) || VAL_SERIES(item) != series)
					//	Trap1_DEAD_END(RE_PARSE_SERIES, rules-1);
					if (!ANY_SERIES(item)) Trap1_DEAD_END(RE_PARSE_SERIES, rules-1);
					index = Set_Parse_Series(parse, item);
					series = parse->series;
					continue;
				}

				// word - some other variable
				if (IS_WORD(item)) {
					// !!! Should mutability be enforced?
					item = GET_MUTABLE_VAR(item);
				}

				// item can still be 'word or /word
			}
		}
		else if (ANY_PATH(item)) {
			const REBVAL *path = item;

			if (IS_PATH(item)) {
				if (Do_Path(&save, &path, 0)) {
					// !!! "found a function" ?
				}
				else
					item = &save;
			}
			else if (IS_SET_PATH(item)) {
				REBVAL tmp;

				Val_Init_Series(&tmp, parse->type, parse->series);
				VAL_INDEX(&tmp) = index;
				if (Do_Path(&save, &path, &tmp)) {
					// found a function
				}
				else
					item = &save;
			}
			else if (IS_GET_PATH(item)) {
				if (Do_Path(&save, &path, 0)) {
					// found a function
				}
				else {
					item = &save;
					// CureCode #1263 change
					//		if (parse->type != VAL_TYPE(item) || VAL_SERIES(item) != parse->series)
					if (!ANY_SERIES(item)) Trap1_DEAD_END(RE_PARSE_SERIES, path);
					index = Set_Parse_Series(parse, item);
					item = NULL;
				}
			}

			if (index > series->tail) index = series->tail;
			if (!item) continue; // for SET and GET cases
		}

		if (IS_PAREN(item)) {
			REBVAL evaluated;

			// might GC
			if (Do_Block_Throws(&evaluated, VAL_SERIES(item), 0)) {
				*parse->out = evaluated;
				Trap(RE_PARSE_LONGJMP_HACK); // !!! should return gracefully!
				DEAD_END;
			}
			// ignore evaluated if it's not THROWN?

			if (index > series->tail) index = series->tail;
			continue;
		}

		// Counter? 123
		if (IS_INTEGER(item)) {	// Specify count or range count
			SET_FLAG(flags, PF_WHILE);
			mincount = maxcount = Int32s(item, 0);
			item = Get_Parse_Value(&save, rules++);
			if (IS_END(item)) Trap1_DEAD_END(RE_PARSE_END, rules-2);
			if (IS_INTEGER(item)) {
				maxcount = Int32s(item, 0);
				item = Get_Parse_Value(&save, rules++);
				if (IS_END(item)) Trap1_DEAD_END(RE_PARSE_END, rules-2);
			}
		}
		// else fall through on other values and words

		//--------------------------------------------------------------------
		// Iterated Rule Matching Section:
		//
		// Repeats the same rule N times or until the rule fails.
		// The index is advanced and stored in a temp variable i until
		// the entire rule has been satisfied.
		//--------------------------------------------------------------------

		item_hold = item;	// a command or literal match value
		if (VAL_TYPE(item) <= REB_UNSET || VAL_TYPE(item) >= REB_NATIVE) goto bad_rule;
		begin = index;		// input at beginning of match section
		rulen = 0;			// rules consumed (do not use rule++ below)
		i = index;

		//note: rules var already advanced

		for (count = 0; count < maxcount;) {

			item = item_hold;

			if (IS_WORD(item)) {

				switch (cmd = VAL_WORD_CANON(item)) {

				case SYM_SKIP:
					i = (index < series->tail) ? index+1 : NOT_FOUND;
					break;

				case SYM_END:
					i = (index < series->tail) ? NOT_FOUND : series->tail;
					break;

				case SYM_TO:
				case SYM_THRU:
					if (IS_END(rules)) goto bad_end;
					item = Get_Parse_Value(&save, rules);
					rulen = 1;
					i = Parse_To(parse, index, item, cmd == SYM_THRU);
					break;

				case SYM_QUOTE:
					if (IS_END(rules)) goto bad_end;
					rulen = 1;
					if (IS_PAREN(rules)) {
						// might GC
						if (Do_Block_Throws(&save, VAL_SERIES(rules), 0)) {
							*parse->out = save;
							Trap(RE_PARSE_LONGJMP_HACK);
							DEAD_END;
						}
						item = &save;
					}
					else item = rules;
					i = (0 == Cmp_Value(BLK_SKIP(series, index), item, parse->flags & AM_FIND_CASE)) ? index+1 : NOT_FOUND;
					break;

				case SYM_INTO: {
					REBPARSE sub_parse;

					if (IS_END(rules)) goto bad_end;
					rulen = 1;
					item = Get_Parse_Value(&save, rules); // sub-rules
					if (!IS_BLOCK(item)) goto bad_rule;
					val = BLK_SKIP(series, index);

					if (!ANY_BINSTR(val) && !ANY_BLOCK(val)) {
						i = NOT_FOUND;
						break;
					}

					sub_parse.series = VAL_SERIES(val);
					sub_parse.type = VAL_TYPE(val);
					sub_parse.flags = parse->flags;
					sub_parse.result = 0;
					sub_parse.out = parse->out;

					if (
						VAL_TAIL(val) != Parse_Rules_Loop(
							&sub_parse,
							VAL_INDEX(val),
							VAL_BLK_DATA(item),
							depth + 1
						)
					) {
						i = NOT_FOUND;
					}

					i = index + 1;
					break;
				}

				case SYM_DO:
					if (!IS_BLOCK_INPUT(parse)) goto bad_rule;
					i = Do_Eval_Rule(parse, index, &rules);
					rulen = 1;
					break;

				default:
					goto bad_rule;
				}
			}
			else if (IS_BLOCK(item)) {
				item = VAL_BLK_DATA(item);
				//if (IS_END(rules) && item == rule_head) {
				//	rules = item;
				//	goto top;
				//}
				i = Parse_Rules_Loop(parse, index, item, depth+1);
				if (parse->result) {
					index = (parse->result > 0) ? i : NOT_FOUND;
					parse->result = 0;
					break;
				}
			}
			// Parse according to datatype:
			else {
				if (IS_BLOCK_INPUT(parse))
					i = Parse_Next_Block(parse, index, item, depth+1);
				else
					i = Parse_Next_String(parse, index, item, depth+1);
			}

			// Necessary for special cases like: some [to end]
			// i: indicates new index or failure of the match, but
			// that does not mean failure of the rule, because optional
			// matches can still succeed, if if the last match failed.
			if (i != NOT_FOUND) {
				count++; // may overflow to negative
				if (count < 0) count = MAX_I32; // the forever case
				// If input did not advance:
				if (i == index && !GET_FLAG(flags, PF_WHILE)) {
					if (count < mincount) index = NOT_FOUND; // was not enough
					break;
				}
			}
			//if (i >= series->tail) {     // OLD check: no more input
			else {
				if (count < mincount) index = NOT_FOUND; // was not enough
				else if (i != NOT_FOUND) index = i;
				// else keep index as is.
				break;
			}
			index = i;

			// A BREAK word stopped us:
			//if (parse->result) {parse->result = 0; break;}
		}

		rules += rulen;

		//if (index > series->tail && index != NOT_FOUND) index = series->tail;
		if (index > series->tail) index = NOT_FOUND;

		//--------------------------------------------------------------------
		// Post Match Processing:
		//--------------------------------------------------------------------
post:
		// Process special flags:
		if (flags) {
			// NOT before all others:
			if (GET_FLAG(flags, PF_NOT)) {
				if (GET_FLAG(flags, PF_NOT2) && index != NOT_FOUND) index = NOT_FOUND;
				else index = begin;
			}
			if (index == NOT_FOUND) { // Failure actions:
				// !!! if word isn't NULL should we set its var to NONE! ...?
				if (GET_FLAG(flags, PF_THEN)) {
					SKIP_TO_BAR(rules);
					if (!IS_END(rules)) rules++;
				}
			}
			else {  // Success actions:
				count = (begin > index) ? 0 : index - begin; // how much we advanced the input
				if (GET_FLAG(flags, PF_COPY)) {
					REBVAL temp;
					Val_Init_Series(
						&temp,
						parse->type,
						IS_BLOCK_INPUT(parse)
							? Copy_Array_At_Max_Shallow(series, begin, count)
							: Copy_String(series, begin, count) // condenses;
					);
					Set_Var(word, &temp);
				}
				else if (GET_FLAG(flags, PF_SET_OR_COPY)) {
					REBVAL *var = GET_MUTABLE_VAR(word); // traps if protected

					if (IS_BLOCK_INPUT(parse)) {
						if (count == 0) SET_NONE(var);
						else *var = *BLK_SKIP(series, begin);
					}
					else {
						if (count == 0) SET_NONE(var);
						else {
							i = GET_ANY_CHAR(series, begin);
							if (parse->type == REB_BINARY) {
								SET_INTEGER(var, i);
							} else {
								SET_CHAR(var, i);
							}
						}
					}

					// !!! Used to reuse item, so item was set to the var at
					// the end, but was that actually needed?
					item = var;
				}
				if (GET_FLAG(flags, PF_RETURN)) {
					// See notes on PARSE's return in handling of SYM_RETURN
					Val_Init_Series(
						parse->out,
						parse->type,
						IS_BLOCK_INPUT(parse)
							? Copy_Array_At_Max_Shallow(series, begin, count)
							: Copy_String(series, begin, count) // condenses
					);

					Trap(RE_PARSE_LONGJMP_HACK);
					DEAD_END;
				}
				if (GET_FLAG(flags, PF_REMOVE)) {
					if (count) Remove_Series(series, begin, count);
					index = begin;
				}
				if (flags & (1<<PF_INSERT | 1<<PF_CHANGE)) {
					count = GET_FLAG(flags, PF_INSERT) ? 0 : count;
					cmd = GET_FLAG(flags, PF_INSERT) ? 0 : (1<<AN_PART);
					item = rules++;
					if (IS_END(item)) goto bad_end;
					// Check for ONLY flag:
					if (IS_WORD(item) && (cmd = VAL_CMD(item))) {
						if (cmd != SYM_ONLY) goto bad_rule;
						cmd |= (1<<AN_ONLY);
						item = rules++;
					}
					// CHECK FOR QUOTE!!
					item = Get_Parse_Value(&save, item); // new value
					if (IS_UNSET(item)) Trap1_DEAD_END(RE_NO_VALUE, rules-1);
					if (IS_END(item)) goto bad_end;
					if (IS_BLOCK_INPUT(parse)) {
						index = Modify_Array(GET_FLAG(flags, PF_CHANGE) ? A_CHANGE : A_INSERT,
								series, begin, item, cmd, count, 1);
						if (IS_LIT_WORD(item)) SET_TYPE(BLK_SKIP(series, index-1), REB_WORD);
					}
					else {
						if (parse->type == REB_BINARY) cmd |= (1<<AN_SERIES); // special flag
						index = Modify_String(GET_FLAG(flags, PF_CHANGE) ? A_CHANGE : A_INSERT,
								series, begin, item, cmd, count, 1);
					}
				}
				if (GET_FLAG(flags, PF_AND)) index = begin;
			}

			flags = 0;
			word = 0;
		}

		// Goto alternate rule and reset input:
		if (index == NOT_FOUND) {
			SKIP_TO_BAR(rules);
			if (IS_END(rules)) break;
			rules++;
			index = begin = start;
		}

		begin = index;
		mincount = maxcount = 1;

	}
	return index;

bad_rule:
	Trap1_DEAD_END(RE_PARSE_RULE, rules-1);
bad_end:
	Trap1_DEAD_END(RE_PARSE_END, rules-1);
	return 0;
}


/*******************************************************************************
**
**  Name: "parse"
**  Summary: {Parses a string or block series according to grammar rules.}
**  Details: none
**  Spec: [
**      <1> input
**      <2> rules
**      <3> /case
**      <4> /all
**  ]
**
*******************************************************************************/

REBNATIVE(parse)
{
	REBVAL *input = D_ARG(1);
	REBVAL *rules = D_ARG(2);
	const REBOOL cased = D_REF(3);

	REBCNT opts = 0;
	REBCNT index;

	REBPARSE parse;

	REBOL_STATE state;
	const REBVAL *error;

	if (cased) opts |= PF_CASE;

	// We always want "case-sensitivity" on binary bytes, vs. treating as
	// case-insensitive bytes for ASCII characters
	if (IS_BINARY(input)) opts |= PF_CASE;

	assert(IS_TRASH(D_OUT));

	parse.series = VAL_SERIES(input);
	parse.type = VAL_TYPE(input);
	parse.flags = cased ? AM_FIND_CASE : 0;
	parse.result = 0;
	parse.out = D_OUT;

	PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Trap()s can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		// !!! Rather than return normally up the C call stack when it
		// hits a place it needs to give back a THROWN() or do a RETURN,
		// PARSE re-uses the setjmp/longjmp mechanism from error handling
		// to leap out of the stack it is in and to this enclosing code.
		// This is an unnecessary hack and should be removed.
		//
		// For instance: the code would jump here if it were to hit either
		// a RETURN parse rule -or- have a paren code that did a RETURN.
		// Note the distinction in meaning in that particular case:
		//
		//	   foo: func [] [parse "1020" [(return true)]
		//	   bar: func [] [parse "0304" [return (false)]]
		//
		// (To end a PARSE from within a PAREN!, it is being considered
		// that EXIT/WITH might exit the parentheses and then the /WITH would
		// be treated as the next rule to run.  In that case, you could end
		// a parse with EXIT/WITH [return].)
		//
		if (VAL_ERR_NUM(error) == RE_PARSE_LONGJMP_HACK) {
			// If the longjmp hack is being done, it is required that the
			// jumper has filled our output value.  It knows where to write
			// it because we pass D_OUT in the REBPARSE as parse->out
			//
			assert(!IS_TRASH(D_OUT));
			return R_OUT;
		}

		// All other errors we don't interfere with, and just re-trap
		Do_Error(error);
	}

	index = Parse_Rules_Loop(&parse, VAL_INDEX(input), VAL_BLK_DATA(rules), 0);

	// !!! Currently the only way Parse_Rules_Loop should write to the out
	// value we gave it is if it did a "longjmp hack".  When the hack is gone
	// and the C unwinds back to us without a longjmp, we would read the
	// parse->out value here (instead of in RE_PARSE_LONGJMP_HACK handling).
	assert(IS_TRASH(D_OUT));

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	// Parse can fail if the match rule state can't process pending input
	if (index == NOT_FOUND)
		return R_FALSE; // !!! Would R_NONE be better?

	// If the match rules all completed, but the parse position didn't end
	// at (or beyond) the tail of the input series, the parse also failed
	if (index < VAL_TAIL(input))
		return R_FALSE; // !!! Would R_NONE be better?

	// The parse succeeded...
	return R_TRUE; // !!! Would 'input' be a more useful "true"?  See CC#2165
}
