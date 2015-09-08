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
**  Module:  s-mold.c
**  Summary: value to string conversion
**  Section: strings
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

//#define	INCLUDE_TYPE_NAMES		// include the value names table
#include "sys-core.h"
#include "sys-scan.h"
#include <float.h>

#define	MAX_QUOTED_STR	50	// max length of "string" before going to { }

//typedef REBSER *(*MOLD_FUNC)(REBVAL *, REBSER *, REBCNT);
typedef void (*MOLD_FUNC)(REBVAL *, REB_MOLD *);

//const REBYTE New_Line[4] = {LF, 0};

const REBYTE Punctuation[] = ".,-/";
enum REB_Punct {
	PUNCT_DOT = 0, // Must be 0
	PUNCT_COMMA,   // Must be 1
	PUNCT_DASH,
	PUNCT_SLASH,
	PUNCT_MAX
};

REBYTE *Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR && Char_Escapes[c])

REBYTE *URL_Escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_FILE))

enum {
	ESC_URL = 1,
	ESC_FILE = 2,
	ESC_EMAIL = 4
};

/***********************************************************************
************************************************************************
**
**	SECTION: Global Mold Utilities
**
************************************************************************
***********************************************************************/

//
//  Emit: C
//
REBSER *Emit(REB_MOLD *mold, const char *fmt, ...)
{
	va_list args;
	REBYTE ender = 0;
	REBSER *series = mold->series;

	assert(SERIES_WIDE(series) == 2);

	va_start(args, fmt);

	for (; *fmt; fmt++) {
		switch (*fmt) {
		case 'W':	// Word symbol
			Append_UTF8(series, Get_Word_Name(va_arg(args, REBVAL*)), -1);
			break;
		case 'V':	// Value
			Mold_Value(mold, va_arg(args, REBVAL*), TRUE);
			break;
		case 'S':	// String of bytes
			Append_Unencoded(series, va_arg(args, const char *));
			break;
		case 'C':	// Char
			Append_Codepoint_Raw(series, va_arg(args, REBCNT));
			break;
		case 'E':	// Series (byte or uni)
			{
				REBSER *src = va_arg(args, REBSER*);
				Insert_String(series, SERIES_TAIL(series), src, 0, SERIES_TAIL(src), 0);
			}
			break;
		case 'I':	// Integer
			Append_Int(series, va_arg(args, REBINT));
			break;
		case 'i':
			Append_Int_Pad(series, va_arg(args, REBINT), -9);
			Trim_Tail(mold->series, '0');
			break;
		case '2':	// 2 digit int (for time)
			Append_Int_Pad(series, va_arg(args, REBINT), 2);
			break;
		case 'T':	// Type name
			Append_UTF8(series, Get_Type_Name(va_arg(args, REBVAL*)), -1);
			break;
		case 'N':	// Symbol name
			Append_UTF8(series, Get_Sym_Name(va_arg(args, REBCNT)), -1);
			break;
		case '+':	// Add #[ if mold/all
			if (GET_MOPT(mold, MOPT_MOLD_ALL)) {
				Append_Unencoded(series, "#[");
				ender = ']';
			}
			break;
		case 'D':	// Datatype symbol: #[type
			if (ender) {
				Append_UTF8(series, Get_Sym_Name(va_arg(args, REBCNT)), -1);
				Append_Codepoint_Raw(series, ' ');
			} else va_arg(args, REBCNT); // ignore it
			break;
		case 'B':	// Boot string
			Append_Boot_Str(series, va_arg(args, REBINT));
			break;
		default:
			Append_Codepoint_Raw(series, *fmt);
		}
	}
	va_end(args);

	if (ender) Append_Codepoint_Raw(series, ender);

	return series;
}


//
//  Prep_String: C
//  
//      Helper function for the string related Mold functions below.
//      Creates or expands the series and provides the location to
//      copy text into.
//
REBSER *Prep_String(REBSER *series, REBYTE **str, REBCNT len)
{
	REBCNT tail;

	if (!series) {
		series = Make_Binary(len);
		series->tail = len;
		*str = STR_HEAD(series);
	}
	else {
		tail = SERIES_TAIL(series);
		EXPAND_SERIES_TAIL(series, len);
		*str = STR_SKIP(series, tail);
	}
	return series;
}


//
//  Prep_Uni_Series: C
//
REBUNI *Prep_Uni_Series(REB_MOLD *mold, REBCNT len)
{
	REBCNT tail = SERIES_TAIL(mold->series);

	EXPAND_SERIES_TAIL(mold->series, len);

	return UNI_SKIP(mold->series, tail);
}


/***********************************************************************
************************************************************************
**
**	SECTION: Local MOLD Utilities
**
************************************************************************
***********************************************************************/

//
//  Pre_Mold: C
//  
//      Emit the initial datatype function, depending on /ALL option
//
void Pre_Mold(const REBVAL *value, REB_MOLD *mold)
{
	Emit(mold, GET_MOPT(mold, MOPT_MOLD_ALL) ? "#[T " : "make T ", value);
}


//
//  End_Mold: C
//  
//      Finish the mold, depending on /ALL with close block.
//
void End_Mold(REB_MOLD *mold)
{
	if (GET_MOPT(mold, MOPT_MOLD_ALL)) Append_Codepoint_Raw(mold->series, ']');
}


//
//  Post_Mold: C
//  
//      For series that has an index, add the index for mold/all.
//      Add closing block.
//
void Post_Mold(const REBVAL *value, REB_MOLD *mold)
{
	if (VAL_INDEX(value)) {
		Append_Codepoint_Raw(mold->series, ' ');
		Append_Int(mold->series, VAL_INDEX(value)+1);
	}
	if (GET_MOPT(mold, MOPT_MOLD_ALL)) Append_Codepoint_Raw(mold->series, ']');
}


//
//  New_Indented_Line: C
//  
//      Create a newline with auto-indent on next line if needed.
//
void New_Indented_Line(REB_MOLD *mold)
{
	REBINT n;
	REBUNI *cp = 0;

	// Check output string has content already but no terminator:
	if (mold->series->tail) {
		cp = UNI_LAST(mold->series);
		if (*cp == ' ' || *cp == '\t') *cp = '\n';
		else cp = 0;
	}

	// Add terminator:
	if (!cp) Append_Codepoint_Raw(mold->series, '\n');

	// Add proper indentation:
	if (!GET_MOPT(mold, MOPT_INDENT)) {
		for (n = 0; n < mold->indent; n++)
			Append_Unencoded(mold->series, "    ");
	}
}


/***********************************************************************
************************************************************************
**
**	SECTION: Char/String Datatypes
**
************************************************************************
***********************************************************************/

typedef struct REB_Str_Flags {
	REBCNT escape;		// escaped chars
	REBCNT brace_in;	// {
	REBCNT brace_out;	// }
	REBCNT newline;		// lf
	REBCNT quote;		// "
	REBCNT paren;		// (1234)
	REBCNT chr1e;
	REBCNT malign;
} REB_STRF;


static void Sniff_String(REBSER *ser, REBCNT idx, REB_STRF *sf)
{
	// Scan to find out what special chars the string contains?
	REBYTE *bp = STR_HEAD(ser);
	REBUNI *up = (REBUNI*)bp;
	REBUNI c;
	REBCNT n;

	for (n = idx; n < SERIES_TAIL(ser); n++) {
		c = (BYTE_SIZE(ser)) ? (REBUNI)(bp[n]) : up[n];
		switch (c) {
		case '{':
			sf->brace_in++;
			break;
		case '}':
			sf->brace_out++;
			if (sf->brace_out > sf->brace_in) sf->malign++;
			break;
		case '"':
			sf->quote++;
			break;
		case '\n':
			sf->newline++;
			break;
		default:
			if (c == 0x1e) sf->chr1e += 4; // special case of ^(1e)
			else if (IS_CHR_ESC(c)) sf->escape++;
			else if (c >= 0x1000) sf->paren += 6; // ^(1234)
			else if (c >= 0x100)  sf->paren += 5; // ^(123)
			else if (c >= 0x80)   sf->paren += 4; // ^(12)
		}
	}
	if (sf->brace_in != sf->brace_out) sf->malign++;
}

static REBUNI *Emit_Uni_Char(REBUNI *up, REBUNI chr, REBOOL parened)
{
	if (chr >= 0x7f || chr == 0x1e) {  // non ASCII or ^ must be (00) escaped
		if (parened || chr == 0x1e) { // do not AND with above
			*up++ = '^';
			*up++ = '(';
			up = Form_Uni_Hex(up, chr);
			*up++ = ')';
			return up;
		}
	}
	else if (IS_CHR_ESC(chr)) {
		*up++ = '^';
		*up++ = Char_Escapes[chr];
		return up;
	}

	*up++ = chr;
	return up;
}

static void Mold_Uni_Char(REBSER *dst, REBUNI chr, REBOOL molded, REBOOL parened)
{
	REBCNT tail = SERIES_TAIL(dst);
	REBUNI *up;

	if (!molded) {
		EXPAND_SERIES_TAIL(dst, 1);
		*UNI_SKIP(dst, tail) = chr;
	}
	else {
		EXPAND_SERIES_TAIL(dst, 10); // worst case: #"^(1234)"
		up = UNI_SKIP(dst, tail);
		*up++ = '#';
		*up++ = '"';
		up = Emit_Uni_Char(up, chr, parened);
		*up++ = '"';
		dst->tail = up - UNI_HEAD(dst);
	}
	UNI_TERM(dst);
}

static void Mold_String_Series(const REBVAL *value, REB_MOLD *mold)
{
	REBCNT len = VAL_LEN(value);
	REBSER *ser = VAL_SERIES(value);
	REBCNT idx = VAL_INDEX(value);
	REBYTE *bp;
	REBUNI *up;
	REBUNI *dp;
	REBOOL uni = !BYTE_SIZE(ser);
	REBCNT n;
	REBUNI c;

	REB_STRF sf;
	CLEARS(&sf);

	// Empty string:
	if (idx >= VAL_TAIL(value)) {
		Append_Unencoded(mold->series, "\"\"");  //Trap_DEAD_END(RE_PAST_END);
		return;
	}

	Sniff_String(ser, idx, &sf);
	if (!GET_MOPT(mold, MOPT_ANSI_ONLY)) sf.paren = 0;

	// Source can be 8 or 16 bits:
	if (uni) up = UNI_HEAD(ser);
	else bp = STR_HEAD(ser);

	// If it is a short quoted string, emit it as "string":
	if (len <= MAX_QUOTED_STR && sf.quote == 0 && sf.newline < 3) {

		dp = Prep_Uni_Series(mold, len + sf.newline + sf.escape + sf.paren + sf.chr1e + 2);

		*dp++ = '"';

		for (n = idx; n < VAL_TAIL(value); n++) {
			c = uni ? up[n] : cast(REBUNI, bp[n]);
			dp = Emit_Uni_Char(dp, c, (REBOOL)GET_MOPT(mold, MOPT_ANSI_ONLY)); // parened
		}

		*dp++ = '"';
		*dp = 0;
		return;
	}

	// It is a braced string, emit it as {string}:
	if (!sf.malign) sf.brace_in = sf.brace_out = 0;

	dp = Prep_Uni_Series(mold, len + sf.brace_in + sf.brace_out + sf.escape + sf.paren + sf.chr1e + 2);

	*dp++ = '{';

	for (n = idx; n < VAL_TAIL(value); n++) {

		c = uni ? up[n] : cast(REBUNI, bp[n]);
		switch (c) {
		case '{':
		case '}':
			if (sf.malign) {
				*dp++ = '^';
				*dp++ = c;
				break;
			}
		case '\n':
		case '"':
			*dp++ = c;
			break;
		default:
			dp = Emit_Uni_Char(dp, c, (REBOOL)GET_MOPT(mold, MOPT_ANSI_ONLY)); // parened
		}
	}

	*dp++ = '}';
	*dp = 0;
}


/*
	http://www.blooberry.com/indexdot/html/topics/urlencoding.htm

	Only alphanumerics [0-9a-zA-Z], the special characters $-_.+!*'(),
	and reserved characters used for their reserved purposes may be used
	unencoded within a URL.
*/

static void Mold_Url(const REBVAL *value, REB_MOLD *mold)
{
	REBUNI *dp;
	REBCNT n;
	REBUNI c;
	REBCNT len = VAL_LEN(value);
	REBSER *ser = VAL_SERIES(value);

	// Compute extra space needed for hex encoded characters:
	for (n = VAL_INDEX(value); n < VAL_TAIL(value); n++) {
		c = GET_ANY_CHAR(ser, n);
		if (IS_URL_ESC(c)) len += 2;
	}

	dp = Prep_Uni_Series(mold, len);

	for (n = VAL_INDEX(value); n < VAL_TAIL(value); n++) {
		c = GET_ANY_CHAR(ser, n);
		if (IS_URL_ESC(c)) dp = Form_Hex_Esc_Uni(dp, c);  // c => %xx
		else *dp++ = c;
	}

	*dp = 0;
}

static void Mold_File(const REBVAL *value, REB_MOLD *mold)
{
	REBUNI *dp;
	REBCNT n;
	REBUNI c;
	REBCNT len = VAL_LEN(value);
	REBSER *ser = VAL_SERIES(value);

	// Compute extra space needed for hex encoded characters:
	for (n = VAL_INDEX(value); n < VAL_TAIL(value); n++) {
		c = GET_ANY_CHAR(ser, n);
		if (IS_FILE_ESC(c)) len += 2;
	}

	len++; // room for % at start

	dp = Prep_Uni_Series(mold, len);

	*dp++ = '%';

	for (n = VAL_INDEX(value); n < VAL_TAIL(value); n++) {
		c = GET_ANY_CHAR(ser, n);
		if (IS_FILE_ESC(c)) dp = Form_Hex_Esc_Uni(dp, c);  // c => %xx
		else *dp++ = c;
	}

	*dp = 0;
}

static void Mold_Tag(const REBVAL *value, REB_MOLD *mold)
{
	Append_Codepoint_Raw(mold->series, '<');
	Insert_String(mold->series, AT_TAIL, VAL_SERIES(value), VAL_INDEX(value), VAL_LEN(value), 0);
	Append_Codepoint_Raw(mold->series, '>');

}

//
//  Mold_Binary: C
//
void Mold_Binary(const REBVAL *value, REB_MOLD *mold)
{
	REBCNT len = VAL_LEN(value);
	REBSER *out;

	switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
	default:
	case 16:
		out = Encode_Base16(value, 0, len > 32);
		break;
	case 64:
		Append_Unencoded(mold->series, "64");
		out = Encode_Base64(value, 0, len > 64);
		break;
	case 2:
		Append_Codepoint_Raw(mold->series, '2');
		out = Encode_Base2(value, 0, len > 8);
		break;
	}

	Emit(mold, "#{E}", out);
	Free_Series(out);
}

static void Mold_All_String(const REBVAL *value, REB_MOLD *mold)
{
	// The string that is molded for /all option:
	REBVAL val;

	//// ???? move to above Mold_String_Series function????

	Pre_Mold(value, mold); // #[file! part
	val = *value;
	VAL_INDEX(&val) = 0;
	if (IS_BINARY(value)) Mold_Binary(&val, mold);
	else {
		VAL_SET(&val, REB_STRING);
		Mold_String_Series(&val, mold);
	}
	Post_Mold(value, mold);
}


/***********************************************************************
************************************************************************
**
**	SECTION: Block Series Datatypes
**
************************************************************************
***********************************************************************/

static void Mold_Block_Series(REB_MOLD *mold, REBSER *series, REBCNT index, const char *sep)
{
	REBSER *out = mold->series;
	REBOOL line_flag = FALSE; // newline was part of block
	REBOOL had_lines = FALSE;
	REBVAL *value = BLK_SKIP(series, index);

	if (!sep) sep = "[]";

	if (IS_END(value)) {
		Append_Unencoded(out, sep);
		return;
	}

	// Recursion check: (variation of: Find_Same_Block(MOLD_LOOP, value))
	for (value = BLK_HEAD(MOLD_LOOP); NOT_END(value); value++) {
		if (VAL_SERIES(value) == series) {
			Emit(mold, "C...C", sep[0], sep[1]);
			return;
		}
	}
	value = Alloc_Tail_Array(MOLD_LOOP);
	Val_Init_Block(value, series);

	if (sep[1]) {
		Append_Codepoint_Raw(out, sep[0]);
		mold->indent++;
	}
//	else out->tail--;  // why?????

	value = BLK_SKIP(series, index);
	while (NOT_END(value)) {
		if (VAL_GET_OPT(value, OPT_VALUE_LINE)) {
			if (sep[1] || line_flag) New_Indented_Line(mold);
			had_lines = TRUE;
		}
		line_flag = TRUE;
		Mold_Value(mold, value, TRUE);
		value++;
		if (NOT_END(value))
			Append_Codepoint_Raw(out, (sep[0] == '/') ? '/' : ' ');
	}

	if (sep[1]) {
		mold->indent--;
		if (VAL_GET_OPT(value, OPT_VALUE_LINE) || had_lines)
			New_Indented_Line(mold);
		Append_Codepoint_Raw(out, sep[1]);
	}

	Remove_Last(MOLD_LOOP);
}

static void Mold_Block(const REBVAL *value, REB_MOLD *mold)
{
	const char *sep;
	REBOOL all = GET_MOPT(mold, MOPT_MOLD_ALL);
	REBSER *series = mold->series;
	REBFLG over = FALSE;

	if (SERIES_WIDE(VAL_SERIES(value)) == 0) {
	#if !defined(NDEBUG)
		Debug_Fmt("** Mold_Block() zero series wide, t=%d", VAL_TYPE(value));
	#endif

		Panic_Series(VAL_SERIES(value));
	}

	// Optimize when no index needed:
	if (VAL_INDEX(value) == 0 && !IS_MAP(value)) // && (VAL_TYPE(value) <= REB_LIT_PATH))
		all = FALSE;

	// If out of range, do not cause error to avoid error looping.
	if (VAL_INDEX(value) >= VAL_TAIL(value)) over = TRUE; // Force it into []

	if (all || (over && !IS_BLOCK(value) && !IS_PAREN(value))) {
		SET_FLAG(mold->opts, MOPT_MOLD_ALL);
		Pre_Mold(value, mold); // #[block! part
		//if (over) Append_Unencoded(mold->series, "[]");
		//else
		Mold_Block_Series(mold, VAL_SERIES(value), 0, 0);
		Post_Mold(value, mold);
	}
	else
	{
		switch(VAL_TYPE(value)) {

		case REB_MAP:
			Pre_Mold(value, mold);
			sep = 0;

		case REB_BLOCK:
			if (GET_MOPT(mold, MOPT_ONLY)) {
				CLR_FLAG(mold->opts, MOPT_ONLY); // only top level
				sep = "\000\000";
			}
			else sep = 0;
			break;

		case REB_PAREN:
			sep = "()";
			break;

		case REB_GET_PATH:
			series = Append_Codepoint_Raw(series, ':');
			sep = "/";
			break;

		case REB_LIT_PATH:
			series = Append_Codepoint_Raw(series, '\'');
			/* fall through */
		case REB_PATH:
		case REB_SET_PATH:
			sep = "/";
			break;
		default:
			sep = NULL;
		}

		if (over) Append_Unencoded(mold->series, sep ? sep : "[]");
		else Mold_Block_Series(mold, VAL_SERIES(value), VAL_INDEX(value), sep);

		if (VAL_TYPE(value) == REB_SET_PATH)
			Append_Codepoint_Raw(series, ':');
	}
}

static void Mold_Simple_Block(REB_MOLD *mold, REBVAL *block, REBCNT len)
{
	// Simple molder for error locations. Series must be valid.
	// Max length in chars must be provided.
	REBCNT start = SERIES_TAIL(mold->series);

	while (NOT_END(block)) {
		if ((SERIES_TAIL(mold->series) - start) > len) break;
		Mold_Value(mold, block, TRUE);
		block++;
		if (NOT_END(block)) Append_Codepoint_Raw(mold->series, ' ');
	}

	// If it's too large, truncate it:
	if ((SERIES_TAIL(mold->series) - start) > len) {
		SERIES_TAIL(mold->series) = start + len;
		Append_Unencoded(mold->series, "...");
	}
}

static void Form_Block_Series(REBSER *blk, REBCNT index, REB_MOLD *mold, REBSER *frame)
{
	// Form a series (part_mold means mold non-string values):
	REBINT n;
	REBINT len = SERIES_TAIL(blk) - index;
	REBVAL *val;
	REBVAL *wval;

	if (len < 0) len = 0;

	for (n = 0; n < len;) {
		val = BLK_SKIP(blk, index+n);
		wval = 0;
		if (frame && (IS_WORD(val) || IS_GET_WORD(val))) {
			wval = Find_Word_Value(frame, VAL_WORD_SYM(val));
			if (wval) val = wval;
		}
		Mold_Value(mold, val, wval != 0);
		n++;
		if (GET_MOPT(mold, MOPT_LINES)) {
			Append_Codepoint_Raw(mold->series, LF);
		}
		else {
			// Add a space if needed:
			if (n < len && mold->series->tail
				&& *UNI_LAST(mold->series) != LF
				&& !GET_MOPT(mold, MOPT_TIGHT)
			)
				Append_Codepoint_Raw(mold->series, ' ');
		}
	}
}


/***********************************************************************
************************************************************************
**
**	SECTION: Special Datatypes
**
************************************************************************
***********************************************************************/


static void Mold_Typeset(const REBVAL *value, REB_MOLD *mold, REBFLG molded)
{
	REBINT n;

	if (molded) {
		Pre_Mold(value, mold);	// #[typeset! or make typeset!
		Append_Codepoint_Raw(mold->series, '[');
	}

	// Convert bits to types (we can make this more efficient !!)
	for (n = 0; n < REB_MAX; n++) {
		if (TYPE_CHECK(value, n)) {
			Emit(mold, "+DN ", SYM_DATATYPE_TYPE, n + 1);
		}
	}
	Trim_Tail(mold->series, ' ');

	if (molded) {
		//Form_Typeset(value, mold & ~(1<<MOPT_MOLD_ALL));
		Append_Codepoint_Raw(mold->series, ']');
		End_Mold(mold);
	}
}

static void Mold_Function(const REBVAL *value, REB_MOLD *mold)
{
	Pre_Mold(value, mold);

	Append_Codepoint_Raw(mold->series, '[');

	Mold_Block_Series(mold, VAL_FUNC_SPEC(value), 0, 0); //// & ~(1<<MOPT_MOLD_ALL)); // Never literalize it (/all).

	if (IS_FUNCTION(value) || IS_CLOSURE(value))
		Mold_Block_Series(mold, VAL_FUNC_BODY(value), 0, 0);

	Append_Codepoint_Raw(mold->series, ']');
	End_Mold(mold);
}

static void Mold_Map(const REBVAL *value, REB_MOLD *mold, REBFLG molded)
{
	REBSER *mapser = VAL_SERIES(value);
	REBVAL *val;

	// Prevent endless mold loop:
	if (Find_Same_Block(MOLD_LOOP, value) > 0) {
		Append_Unencoded(mold->series, "...]");
		return;
	}
	Append_Value(MOLD_LOOP, value);

	if (molded) {
		Pre_Mold(value, mold);
		Append_Codepoint_Raw(mold->series, '[');
	}

	// Mold all non-none entries
	mold->indent++;
	for (val = BLK_HEAD(mapser); NOT_END(val) && NOT_END(val+1); val += 2) {
		if (!IS_NONE(val+1)) {
			if (molded) New_Indented_Line(mold);
			Emit(mold, "V V", val, val+1);
			if (!molded) Append_Codepoint_Raw(mold->series, '\n');
		}
	}
	mold->indent--;

	if (molded) {
		New_Indented_Line(mold);
		Append_Codepoint_Raw(mold->series, ']');
	}

	End_Mold(mold);
	Remove_Last(MOLD_LOOP);
}

static void Form_Object(const REBVAL *value, REB_MOLD *mold)
{
	REBSER *wser = VAL_OBJ_WORDS(value);
	REBVAL *words = BLK_HEAD(wser);
	REBVAL *vals  = VAL_OBJ_VALUES(value); // first value is context
	REBCNT n;

	// Prevent endless mold loop:
	if (Find_Same_Block(MOLD_LOOP, value) > 0) {
		Append_Unencoded(mold->series, "...]");
		return;
	}
	Append_Value(MOLD_LOOP, value);

	// Mold all words and their values:
	for (n = 1; n < SERIES_TAIL(wser); n++) {
		if (!VAL_GET_EXT(words + n, EXT_WORD_HIDE))
			Emit(mold, "N: V\n", VAL_WORD_SYM(words+n), vals+n);
	}
	Remove_Last(mold->series);
	Remove_Last(MOLD_LOOP);
}

static void Mold_Object(const REBVAL *value, REB_MOLD *mold)
{
	REBSER *wser;
	REBVAL *words;
	REBVAL *vals; // first value is context
	REBCNT n;

	assert(VAL_OBJ_FRAME(value));

	wser = VAL_OBJ_WORDS(value);
//	if (wser < 1000)
//		Dump_Block_Raw(VAL_OBJ_FRAME(value), 0, 1);
	words = BLK_HEAD(wser);

	vals  = VAL_OBJ_VALUES(value); // first value is context

	Pre_Mold(value, mold);

	Append_Codepoint_Raw(mold->series, '[');

	// Prevent infinite looping:
	if (Find_Same_Block(MOLD_LOOP, value) > 0) {
		Append_Unencoded(mold->series, "...]");
		return;
	}
	Append_Value(MOLD_LOOP, value);

	mold->indent++;
	for (n = 1; n < SERIES_TAIL(wser); n++) {
		if (
			!VAL_GET_EXT(words + n, EXT_WORD_HIDE) &&
			((VAL_TYPE(vals+n) > REB_NONE) || !GET_MOPT(mold, MOPT_NO_NONE))
		){
			New_Indented_Line(mold);
			Append_UTF8(mold->series, Get_Sym_Name(VAL_WORD_SYM(words+n)), -1);
			//Print("Slot: %s", Get_Sym_Name(VAL_WORD_SYM(words+n)));
			Append_Unencoded(mold->series, ": ");
			if (IS_WORD(vals+n) && !GET_MOPT(mold, MOPT_MOLD_ALL))
				Append_Codepoint_Raw(mold->series, '\'');
			Mold_Value(mold, vals+n, TRUE);
		}
	}
	mold->indent--;
	New_Indented_Line(mold);
	Append_Codepoint_Raw(mold->series, ']');

	End_Mold(mold);
	Remove_Last(MOLD_LOOP);
}

static void Mold_Error(const REBVAL *value, REB_MOLD *mold, REBFLG molded)
{
	ERROR_OBJ *err;
	REBVAL *msg;  // Error message block
	REBSER *frame;

	// Protect against recursion. !!!!

	if (molded) {
		if (VAL_OBJ_FRAME(value) && VAL_ERR_NUM(value) >= RE_NOTE && VAL_ERR_OBJECT(value))
			Mold_Object(value, mold);
		else {
			// Happens if throw or return is molded.
			// make error! 0-3
			Pre_Mold(value, mold);
			Append_Int(mold->series, VAL_ERR_NUM(value));
			End_Mold(mold);
		}
		return;
	}

	frame = VAL_ERR_OBJECT(value);
	err = VAL_ERR_VALUES(value);

	// Form: ** <type> Error:
	Emit(mold, "** WB", &err->type, RS_ERRS+0);

	// Append: error message ARG1, ARG2, etc.
	msg = Find_Error_Info(err, 0);
	if (msg) {
		if (!IS_BLOCK(msg)) Mold_Value(mold, msg, 0);
		else {
			//start = DSP + 1;
			//Reduce_In_Frame(frame, VAL_BLK_DATA(msg));
			//SERIES_TAIL(DS_Series) = DSP + 1;
			//Form_Block_Series(DS_Series, start, mold, 0);
			Form_Block_Series(VAL_SERIES(msg), 0, mold, frame);
		}
	} else
		Append_Boot_Str(mold->series, RS_ERRS+1);

	Append_Codepoint_Raw(mold->series, '\n');

	// Form: ** Where: function
	value = &err->where;
	if (VAL_TYPE(value) > REB_NONE) {
		Append_Boot_Str(mold->series, RS_ERRS+2);
		Mold_Value(mold, value, 0);
		Append_Codepoint_Raw(mold->series, '\n');
	}

	// Form: ** Near: location
	value = &err->nearest;
	if (VAL_TYPE(value) > REB_NONE) {
		Append_Boot_Str(mold->series, RS_ERRS+3);
		if (IS_STRING(value)) // special case: source file line number
			Append_String(mold->series, VAL_SERIES(value), 0, VAL_TAIL(value));
		else if (IS_BLOCK(value))
			Mold_Simple_Block(mold, VAL_BLK_DATA(value), 60);
		Append_Codepoint_Raw(mold->series, '\n');
	}
}


/***********************************************************************
************************************************************************
**
**	SECTION: Global Mold Functions
**
************************************************************************
***********************************************************************/

//
//  Mold_Value: C
//  
//      Mold or form any value to string series tail.
//
void Mold_Value(REB_MOLD *mold, const REBVAL *value, REBFLG molded)
{
	REBYTE buf[60];
	REBINT len;
	REBSER *ser = mold->series;

	CHECK_C_STACK_OVERFLOW(&len);

	assert(SERIES_WIDE(mold->series) == sizeof(REBUNI));
	assert(ser);

	if (THROWN(value)) {
		// !!! You do not want to see THROWN values leak into user awareness,
		// as they are an implementation detail.  So unless this is debug
		// output, it should be an assert.  Thus REB_MOLD probably needs a
		// "for debug output purposes" switch.
		Emit(mold, "S", "!!! THROWN() -> ");
	}

	// Special handling of string series: {
	if (ANY_STR(value) && !IS_TAG(value)) {

		// Forming a string:
		if (!molded) {
			Insert_String(ser, -1, VAL_SERIES(value), VAL_INDEX(value), VAL_LEN(value), 0);
			return;
		}

		// Special format for ALL string series when not at head:
		if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
			Mold_All_String(value, mold);
			return;
		}
	}

	switch (VAL_TYPE(value)) {
	case REB_NONE:
		Emit(mold, "+N", SYM_NONE);
		break;

	case REB_LOGIC:
//		if (!molded || !VAL_LOGIC_WORDS(value) || !GET_MOPT(mold, MOPT_MOLD_ALL))
			Emit(mold, "+N", VAL_LOGIC(value) ? SYM_TRUE : SYM_FALSE);
//		else
//			Mold_Logic(mold, value);
		break;

	case REB_INTEGER:
		len = Emit_Integer(buf, VAL_INT64(value));
		goto append;

	case REB_DECIMAL:
	case REB_PERCENT:
		len = Emit_Decimal(buf, VAL_DECIMAL(value), IS_PERCENT(value)?DEC_MOLD_PERCENT:0,
			Punctuation[GET_MOPT(mold, MOPT_COMMA_PT) ? PUNCT_COMMA : PUNCT_DOT], mold->digits);
		goto append;

	case REB_MONEY:
		len = Emit_Money(value, buf, mold->opts);
		goto append;

	case REB_CHAR:
		Mold_Uni_Char(ser, VAL_CHAR(value), (REBOOL)molded, (REBOOL)GET_MOPT(mold, MOPT_MOLD_ALL));
		break;

	case REB_PAIR:
		len = Emit_Decimal(buf, VAL_PAIR_X(value), DEC_MOLD_MINIMAL, Punctuation[PUNCT_DOT], mold->digits/2);
		Append_Unencoded_Len(ser, s_cast(buf), len);
		Append_Codepoint_Raw(ser, 'x');
		len = Emit_Decimal(buf, VAL_PAIR_Y(value), DEC_MOLD_MINIMAL, Punctuation[PUNCT_DOT], mold->digits/2);
		Append_Unencoded_Len(ser, s_cast(buf), len);
		//Emit(mold, "IxI", VAL_PAIR_X(value), VAL_PAIR_Y(value));
		break;

	case REB_TUPLE:
		len = Emit_Tuple(value, buf);
		goto append;

	case REB_TIME:
		//len = Emit_Time(value, buf, Punctuation[GET_MOPT(mold, MOPT_COMMA_PT) ? PUNCT_COMMA : PUNCT_DOT]);
		Emit_Time(mold, value);
		break;

	case REB_DATE:
		Emit_Date(mold, value);
		break;

	case REB_STRING:
		// FORM happens in top section.
		Mold_String_Series(value, mold);
		break;

	case REB_BINARY:
		if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
			Mold_All_String(value, mold);
			return;
		}
		Mold_Binary(value, mold);
		break;

	case REB_FILE:
		if (VAL_LEN(value) == 0) {
			Append_Unencoded(ser, "%\"\"");
			break;
		}
		Mold_File(value, mold);
		break;

	case REB_EMAIL:
	case REB_URL:
		Mold_Url(value, mold);
		break;

	case REB_TAG:
		if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
			Mold_All_String(value, mold);
			return;
		}
		Mold_Tag(value, mold);
		break;

//		Mold_Issue(value, mold);
//		break;

	case REB_BITSET:
		Pre_Mold(value, mold); // #[bitset! or make bitset!
		Mold_Bitset(value, mold);
		End_Mold(mold);
		break;

	case REB_IMAGE:
		Pre_Mold(value, mold);
		if (!GET_MOPT(mold, MOPT_MOLD_ALL)) {
			Append_Codepoint_Raw(ser, '[');
			Mold_Image_Data(value, mold);
			Append_Codepoint_Raw(ser, ']');
			End_Mold(mold);
		}
		else {
			REBVAL val = *value;
			VAL_INDEX(&val) = 0; // mold all of it
			Mold_Image_Data(&val, mold);
			Post_Mold(value, mold);
		}
		break;

	case REB_BLOCK:
	case REB_PAREN:
		if (!molded)
			Form_Block_Series(VAL_SERIES(value), VAL_INDEX(value), mold, 0);
		else
			Mold_Block(value, mold);
		break;

	case REB_PATH:
	case REB_SET_PATH:
	case REB_GET_PATH:
	case REB_LIT_PATH:
		Mold_Block(value, mold);
		break;

	case REB_VECTOR:
		Mold_Vector(value, mold, molded);
		break;

	case REB_DATATYPE:
		if (!molded)
			Emit(mold, "N", VAL_TYPE_KIND(value) + 1);
		else
			Emit(mold, "+DN", SYM_DATATYPE_TYPE, VAL_TYPE_KIND(value) + 1);
		break;

	case REB_TYPESET:
		Mold_Typeset(value, mold, molded);
		break;

	case REB_WORD:
		// This is a high frequency function, so it is optimized.
		Append_UTF8(ser, Get_Sym_Name(VAL_WORD_SYM(value)), -1);
		break;

	case REB_SET_WORD:
		Emit(mold, "W:", value);
		break;

	case REB_GET_WORD:
		Emit(mold, ":W", value);
		break;

	case REB_LIT_WORD:
		Emit(mold, "\'W", value);
		break;

	case REB_REFINEMENT:
		Emit(mold, "/W", value);
		break;

	case REB_ISSUE:
		Emit(mold, "#W", value);
		break;

	case REB_CLOSURE:
	case REB_FUNCTION:
	case REB_NATIVE:
	case REB_ACTION:
	case REB_COMMAND:
		Mold_Function(value, mold);
		break;

	case REB_OBJECT:
	case REB_MODULE:
	case REB_PORT:
		if (!molded) Form_Object(value, mold);
		else Mold_Object(value, mold);
		break;

	case REB_TASK:
		Mold_Object(value, mold); //// | (1<<MOPT_NO_NONE));
		break;

	case REB_ERROR:
		Mold_Error(value, mold, molded);
		break;

	case REB_MAP:
		Mold_Map(value, mold, molded);
		break;

	case REB_GOB:
	{
		REBSER *blk;
		Pre_Mold(value, mold);
		blk = Gob_To_Block(VAL_GOB(value));
		Mold_Block_Series(mold, blk, 0, 0);
		End_Mold(mold);
	}
		break;


	case REB_EVENT:
		Mold_Event(value, mold);
		break;

	case REB_STRUCT:
	{
		REBSER *blk;
		Pre_Mold(value, mold);
		blk = Struct_To_Block(&VAL_STRUCT(value));
		Mold_Block_Series(mold, blk, 0, 0);
		End_Mold(mold);
	}
		break;

	case REB_ROUTINE:
		Pre_Mold(value, mold);
		Mold_Block_Series(mold, VAL_ROUTINE_SPEC(value), 0, NULL);
		End_Mold(mold);
		break;
	case REB_LIBRARY:
		Pre_Mold(value, mold);

		DS_PUSH_NONE;
		*DS_TOP = *(REBVAL*)SERIES_DATA(VAL_LIB_SPEC(value));
		Mold_File(DS_TOP, mold);
		DS_DROP;

		End_Mold(mold);
		break;
	case REB_CALLBACK:
		Pre_Mold(value, mold);
		Mold_Block_Series(mold, VAL_ROUTINE_SPEC(value), 0, NULL);
		End_Mold(mold);
		break;
	case REB_REBCODE:
	case REB_FRAME:
	case REB_HANDLE:
	case REB_UTYPE:
		// Value has no printable form, so just print its name.
		if (!molded) Emit(mold, "?T?", value);
		else Emit(mold, "+T", value);
		break;

	case REB_END:
	case REB_UNSET:
		if (molded) Emit(mold, "+T", value);
		break;

	default:
		assert(FALSE);
		Panic_Core(RP_DATATYPE+5, VAL_TYPE(value));
	}
	return;

append:
	Append_Unencoded_Len(ser, s_cast(buf), len);

}


//
//  Copy_Form_Value: C
//  
//      Form a value based on the mold opts provided.
//
REBSER *Copy_Form_Value(const REBVAL *value, REBCNT opts)
{
	REB_MOLD mo;
	CLEARS(&mo);
	mo.opts = opts;
	Reset_Mold(&mo);

	Mold_Value(&mo, value, 0);
	return Copy_String(mo.series, 0, -1);
}


//
//  Copy_Mold_Value: C
//  
//      Form a value based on the mold opts provided.
//
REBSER *Copy_Mold_Value(const REBVAL *value, REBCNT opts)
{
	REB_MOLD mo;
	CLEARS(&mo);
	mo.opts = opts;
	Reset_Mold(&mo);

	Mold_Value(&mo, value, TRUE);
	return Copy_String(mo.series, 0, -1);
}


//
//  Form_Reduce: C
//  
//      Reduce a block and then form each value into a string. Return the
//      string or NULL if an unwind triggered while reducing.
//
REBSER *Form_Reduce(REBSER *block, REBCNT index)
{
	REBINT start = DSP + 1;
	REBINT n;
	REBSER *result = NULL;

	REB_MOLD mo;

	while (index < BLK_LEN(block)) {
		REBVAL out;
		index = Do_Next_May_Throw(&out, block, index);
		if (index == THROWN_FLAG) goto return_balanced;
		DS_PUSH(&out);
	}

	CLEARS(&mo);
	Reset_Mold(&mo);

	for (n = start; n <= DSP; n++)
		Mold_Value(&mo, DS_AT(n), 0);

	result = Copy_String(mo.series, 0, -1);

return_balanced:
	DS_DROP_TO(start);
	return result;
}


//
//  Form_Tight_Block: C
//
REBSER *Form_Tight_Block(const REBVAL *blk)
{
	REBVAL *val;

	REB_MOLD mo;
	CLEARS(&mo);
	Reset_Mold(&mo);

	for (val = VAL_BLK_DATA(blk); NOT_END(val); val++)
		Mold_Value(&mo, val, 0);
	return Copy_String(mo.series, 0, -1);
}


//
//  Reset_Mold: C
//
void Reset_Mold(REB_MOLD *mold)
{
	REBSER *buf = BUF_MOLD;
	REBINT len;

	if (!buf) Panic(RP_NO_BUFFER);

	if (SERIES_REST(buf) > MAX_COMMON)
		Remake_Series(buf, MIN_COMMON, SERIES_WIDE(buf), MKS_NONE);

	BLK_RESET(MOLD_LOOP);
	RESET_SERIES(buf);
	mold->series = buf;

	// This is not needed every time, but w/o a functional way to set the option,
	// it must be done like this and each time.
	if (GET_MOPT(mold, MOPT_MOLD_ALL)) len = MAX_DIGITS;
	else {
		// !!! It may be necessary to mold out values before the options
		// block is loaded, and this 'Get_System_Int' is a bottleneck which
		// crashes that in early debugging.  BOOT_ERRORS is sufficient.
		if (PG_Boot_Phase >= BOOT_ERRORS)
			len = Get_System_Int(SYS_OPTIONS, OPTIONS_DECIMAL_DIGITS, MAX_DIGITS);
		else
			len = MAX_DIGITS;

		if (len > MAX_DIGITS) len = MAX_DIGITS;
		else if (len < 0) len = 0;
	}
	mold->digits = len;
}


//
//  Mold_Print_Value: C
//  
//      Basis function for print.  Can do a form or a mold based
//      on the mold flag setting.  Can limit string output to a
//      specified size to prevent long console garbage output.
//
REBSER *Mold_Print_Value(const REBVAL *value, REBCNT limit, REBFLG mold)
{
	REB_MOLD mo;
	CLEARS(&mo);
	Reset_Mold(&mo);

	Mold_Value(&mo, value, mold);

	if (limit != 0 && STR_LEN(mo.series) > limit) {
		SERIES_TAIL(mo.series) = limit;
		Append_Unencoded(mo.series, "..."); // adds a null at the tail
	}

	return mo.series;
}


//
//  Init_Mold: C
//
void Init_Mold(REBCNT size)
{
	REBYTE *cp;
	REBYTE c;
	const REBYTE *dc;

	Set_Root_Series(TASK_MOLD_LOOP, Make_Array(size/10), "mold loop");
	Set_Root_Series(TASK_BUF_MOLD, Make_Unicode(size), "mold buffer");

	// Create quoted char escape table:
	Char_Escapes = cp = ALLOC_ARRAY_ZEROFILL(REBYTE, MAX_ESC_CHAR + 1);
	for (c = '@'; c <= '_'; c++) *cp++ = c;
	Char_Escapes[cast(REBYTE, TAB)] = '-';
	Char_Escapes[cast(REBYTE, LF)] = '/';
	Char_Escapes[cast(REBYTE, '"')] = '"';
	Char_Escapes[cast(REBYTE, '^')] = '^';

	URL_Escapes = cp = ALLOC_ARRAY_ZEROFILL(REBYTE, MAX_URL_CHAR + 1);
	//for (c = 0; c <= MAX_URL_CHAR; c++) if (IS_LEX_DELIMIT(c)) cp[c] = ESC_URL;
	for (c = 0; c <= ' '; c++) cp[c] = ESC_URL | ESC_FILE;
	dc = cb_cast(";%\"()[]{}<>");
	for (c = LEN_BYTES(dc); c > 0; c--) URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}
