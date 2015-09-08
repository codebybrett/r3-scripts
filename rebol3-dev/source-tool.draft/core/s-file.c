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
**  Module:  s-file.c
**  Summary: file and path string handling
**  Section: strings
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#define FN_PAD 2	// pad file name len for adding /, /*, and /?


//
//  To_REBOL_Path: C
//  
//  Convert local filename to a REBOL filename.
//  
//  Allocate and return a new series with the converted path.
//  Return 0 on error.
//  
//  Reduces width when possible.
//  Adds extra space at end for appending a dir /(star)
//      (Note: don't put actual star, as "/" "*" ends this comment)
//  
//  REBDIFF: No longer appends current dir to volume when no
//  root slash is provided (that odd MSDOS c:file case).
//
REBSER *To_REBOL_Path(const void *p, REBCNT len, REBINT uni, REBFLG dir)
{
	REBOOL colon = 0;  // have we hit a ':' yet?
	REBOOL slash = 0; // have we hit a '/' yet?
	REBUNI c;
	REBSER *dst;
	REBCNT n;
	REBCNT i;
	const REBYTE *bp = uni ? NULL : cast(const REBYTE *, p);
	const REBUNI *up = uni ? cast(const REBUNI *, p) : NULL;

	if (len == 0)
		len = uni ? Strlen_Uni(up) : LEN_BYTES(bp);

	n = 0;
	dst = ((uni == -1) || (uni && Is_Wide(up, len)))
		? Make_Unicode(len+FN_PAD) : Make_Binary(len+FN_PAD);

	for (i = 0; i < len;) {
		c = uni ? up[i] : bp[i];
		i++;
#ifdef TO_WINDOWS
		if (c == ':') {
			// Handle the vol:dir/file format:
			if (colon || slash) return 0; // no prior : or / allowed
			colon = 1;
			if (i < len) {
				c = uni ? up[i] : bp[i];
				if (c == '\\' || c == '/') i++; // skip / in foo:/file
			}
			c = '/'; // replace : with a /
		}
		else if (c == '\\' || c== '/') {
			if (slash > 0) continue;
			c = '/';
			slash = 1;
		}
		else slash = 0;
#endif
		SET_ANY_CHAR(dst, n++, c);
	}
	if (dir && c != '/') {  // watch for %/c/ case
		SET_ANY_CHAR(dst, n++, '/');
	}
	SERIES_TAIL(dst) = n;
	TERM_SERIES(dst);

#ifdef TO_WINDOWS
	// Change C:/ to /C/ (and C:X to /C/X):
	if (colon) Insert_Char(dst, 0, '/');
#endif

	return dst;
}


//
//  Value_To_REBOL_Path: C
//  
//  Helper to above function.
//
REBSER *Value_To_REBOL_Path(REBVAL *val, REBOOL dir)
{
	assert(ANY_BINSTR(val));
	return To_REBOL_Path(VAL_DATA(val), VAL_LEN(val), (REBOOL)!VAL_BYTE_SIZE(val), dir);
}


//
//  To_Local_Path: C
//  
//  Convert REBOL filename to a local filename.
//  
//  Allocate and return a new series with the converted path.
//  Return 0 on error.
//  
//  Adds extra space at end for appending a dir /(star)
//      (Note: don't put actual star, as "/" "*" ends this comment)
//  
//  Expands width for OS's that require it.
//
REBSER *To_Local_Path(const void *p, REBCNT len, REBOOL uni, REBFLG full)
{
	REBUNI c, d;
	REBSER *dst;
	REBCNT i = 0;
	REBCNT n = 0;
	REBUNI *out;
	REBCHR *lpath;
	REBCNT l = 0;
	const REBYTE *bp = uni ? NULL : cast(const REBYTE *, p);
	const REBUNI *up = uni ? cast(const REBUNI *, p) : NULL;

	if (len == 0)
		len = uni ? Strlen_Uni(up) : LEN_BYTES(bp);

	// Prescan for: /c/dir = c:/dir, /vol/dir = //vol/dir, //dir = ??
	c = uni ? up[i] : bp[i];
	if (c == '/') {			// %/
		dst = Make_Unicode(len+FN_PAD);
		out = UNI_HEAD(dst);
#ifdef TO_WINDOWS
		i++;
		if (i < len) {
			c = uni ? up[i] : bp[i];
			i++;
		}
		if (c != '/') {		// %/c or %/c/ but not %/ %// %//c
			// peek ahead for a '/':
			d = '/';
			if (i < len) d = uni ? up[i] : bp[i];
			if (d == '/') {	// %/c/ => "c:/"
				i++;
				out[n++] = c;
				out[n++] = ':';
			}
			else {
				out[n++] = OS_DIR_SEP;  // %/cc %//cc => "//cc"
				i--;
			}
		}
#endif
		out[n++] = OS_DIR_SEP;
	}
	else {
		if (full) l = OS_GET_CURRENT_DIR(&lpath);
		dst = Make_Unicode(l + len + FN_PAD); // may be longer (if lpath is encoded)
		if (full) {
#ifdef TO_WINDOWS
			assert(sizeof(REBCHR) == sizeof(REBUNI));
			Append_Uni_Uni(dst, cast(const REBUNI*, lpath), l);
#else
			REBINT clen = Decode_UTF8(
				UNI_HEAD(dst), cast(const REBYTE*, lpath), l, FALSE
			);
			dst->tail = abs(clen);
			//Append_Unencoded(dst, lpath);
#endif
			Append_Codepoint_Raw(dst, OS_DIR_SEP);
			OS_FREE(lpath);
		}
		out = UNI_HEAD(dst);
		n = SERIES_TAIL(dst);
	}

	// Prescan each file segment for: . .. directory names:
	// (Note the top of this loop always follows / or start)
	while (i < len) {
		if (full) {
			// Peek for: . ..
			c = uni ? up[i] : bp[i];
			if (c == '.') {		// .
				i++;
				c = uni ? up[i] : bp[i];
				if (c == '.') {	// ..
					c = uni ? up[i + 1] : bp[i + 1];
					if (c == 0 || c == '/') { // ../ or ..
						i++;
						// backup a dir
						n -= (n > 2) ? 2 : n;
						for (; n > 0 && out[n] != OS_DIR_SEP; n--);
						c = c ? 0 : OS_DIR_SEP; // add / if necessary
					}
					// fall through on invalid ..x combination:
				}
				else {	// .a or . or ./
					if (c == '/') {
						i++;
						c = 0; // ignore it
					}
					else if (c) c = '.'; // for store below
				}
				if (c) out[n++] = c;
			}
		}
		for (; i < len; i++) {
			c = uni ? up[i] : bp[i];
			if (c == '/') {
				if (n == 0 || out[n-1] != OS_DIR_SEP) out[n++] = OS_DIR_SEP;
				i++;
				break;
			}
			out[n++] = c;
		}
	}
	out[n] = 0;
	SERIES_TAIL(dst) = n;
//	TERM_SERIES(dst);
//	Debug_Uni(dst);

	return dst;
}


//
//  Value_To_Local_Path: C
//  
//  Helper to above function.
//
REBSER *Value_To_Local_Path(REBVAL *val, REBFLG full)
{
	assert(ANY_BINSTR(val));
	return To_Local_Path(VAL_DATA(val), VAL_LEN(val), (REBOOL)!VAL_BYTE_SIZE(val), full);
}


//
//  Value_To_OS_Path: C
//  
//  Helper to above function.
//
REBSER *Value_To_OS_Path(REBVAL *val, REBFLG full)
{
	REBSER *ser; // will be unicode size
#ifndef TO_WINDOWS
	REBSER *bin;
	REBCNT n;
#endif

	assert(ANY_BINSTR(val));

	ser = To_Local_Path(VAL_DATA(val), VAL_LEN(val), (REBOOL)!VAL_BYTE_SIZE(val), full);

#ifndef TO_WINDOWS
	// Posix needs UTF8 conversion:
	n = Length_As_UTF8(UNI_HEAD(ser), SERIES_TAIL(ser), TRUE, OS_CRLF);
	bin = Make_Binary(n + FN_PAD);
	Encode_UTF8(BIN_HEAD(bin), n+FN_PAD, UNI_HEAD(ser), &n, TRUE, OS_CRLF);
	SERIES_TAIL(bin) = n;
	TERM_SERIES(bin);
	Free_Series(ser);
	ser = bin;
#endif

	return ser;
}
