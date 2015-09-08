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
**  Module:  l-types.c
**  Summary: special lexical type converters
**  Section: lexical
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-scan.h"
#include "sys-deci-funcs.h"
#include "sys-dec-to-char.h"
#include <errno.h>

typedef REBFLG (*MAKE_FUNC)(REBVAL *, REBVAL *, REBCNT);
#include "tmp-maketypes.h"


//
//  Scan_Hex: C
//  
//      Scans hex while it is valid and does not exceed the maxlen.
//      If the hex string is longer than maxlen - it's an error.
//      If a bad char is found less than the minlen - it's an error.
//      String must not include # - ~ or other invalid chars.
//      If minlen is zero, and no string, that's a valid zero value.
//  
//      Note, this function relies on LEX_WORD lex values having a LEX_VALUE
//      field of zero, except for hex values.
//
const REBYTE *Scan_Hex(const REBYTE *cp, REBI64 *num, REBCNT minlen, REBCNT maxlen)
{
	REBYTE lex;
	REBYTE v;
	REBI64 n = 0;
	REBCNT cnt = 0;

	if (maxlen > MAX_HEX_LEN) return 0;
	while ((lex = Lex_Map[*cp]) > LEX_WORD) {
		if (++cnt > maxlen) return 0;
		v = (REBYTE)(lex & LEX_VALUE);   /* char num encoded into lex */
		if (!v && lex < LEX_NUMBER) return 0;  /* invalid char (word but no val) */
		n = (n << 4) + v;
		cp++;
	}

	if (cnt < minlen) return 0;
	*num = n;
	return cp;
}


//
//  Scan_Hex2: C
//  
//      Decode a %xx hex encoded byte into a char.
//  
//      The % should already be removed before calling this.
//  
//      We don't allow a %00 in files, urls, email, etc... so
//      a return of 0 is used to indicate an error.
//
REBOOL Scan_Hex2(const REBYTE *bp, REBUNI *n, REBFLG uni)
{
	REBUNI c1, c2;
	REBYTE d1, d2;
	REBYTE lex;

	if (uni) {
		const REBUNI *up = cast(const REBUNI*, bp);
		c1 = up[0];
		c2 = up[1];
	} else {
		c1 = bp[0];
		c2 = bp[1];
	}

	lex = Lex_Map[c1];
	d1 = lex & LEX_VALUE;
	if (lex < LEX_WORD || (!d1 && lex < LEX_NUMBER)) return FALSE;

	lex = Lex_Map[c2];
	d2 = lex & LEX_VALUE;
	if (lex < LEX_WORD || (!d2 && lex < LEX_NUMBER)) return FALSE;

    *n = (REBUNI)((d1 << 4) + d2);

	return TRUE;
}


//
//  Scan_Hex_Bytes: C
//  
//      Low level conversion of hex chars into binary bytes.
//      Returns the number of bytes in binary.
//
REBINT Scan_Hex_Bytes(REBVAL *val, REBCNT maxlen, REBYTE *out)
{
	REBYTE b, n = 0;
	REBCNT cnt;
	REBYTE lex;
	REBCNT len;
	REBUNI c;
	REBYTE *start = out;

	len = VAL_LEN(val);
	if (len > maxlen) return 0;

	for (cnt = 0; cnt < len; cnt++) {
		c = GET_ANY_CHAR(VAL_SERIES(val), VAL_INDEX(val)+cnt);
		if (c > 127) return 0;
		lex = Lex_Map[c];
		b = (REBYTE)(lex & LEX_VALUE);   /* char num encoded into lex */
		if (!b && lex < LEX_NUMBER) return 0;  /* invalid char (word but no val) */
		if ((cnt + len) & 1) *out++ = (n << 4) + b; // cnt + len deals with odd # of chars
		else n = b & 15;
	}

	return (out - start);
}


//
//  Scan_Hex_Value: C
//  
//      Given a string, scan it as hex. Chars can be 8 or 16 bit.
//      Result is 32 bits max.
//      Throw errors.
//
REBCNT Scan_Hex_Value(const void *p, REBCNT len, REBOOL uni)
{
	REBUNI c;
	REBCNT n;
	REBYTE lex;
	REBCNT num = 0;
	const REBYTE *bp = uni ? NULL : cast(const REBYTE *, p);
	const REBUNI *up = uni ? cast(const REBUNI *, p) : NULL;

	if (len > 8) goto bad_hex;

	for (n = 0; n < len; n++) {
		c = uni ? up[n] : cast(REBUNI, bp[n]);

		if (c > 255) goto bad_hex;

		lex = Lex_Map[c];
		if (lex > LEX_WORD) {
			c = lex & LEX_VALUE;
			if (!c && lex < LEX_NUMBER) goto bad_hex;
			num = (num << 4) + c;
		}
		else {
bad_hex:	Trap_DEAD_END(RE_INVALID_CHARS);
		}
	}
	return num;
}


//
//  Scan_Dec_Buf: C
//  
//      Validate a decimal number. Return on first invalid char
//      (or end). Return zero if not valid.
//  
//      len: max size of buffer (must be MAX_NUM_LEN or larger).
//  
//      Scan is valid for 1 1.2 1,2 1'234.5 1x 1.2x 1% 1.2% etc.
//
const REBYTE *Scan_Dec_Buf(const REBYTE *cp, REBCNT len, REBYTE *buf)
{
	REBYTE *bp = buf;
	REBYTE *be = bp + len - 1;
	REBOOL dig = FALSE;   /* flag that a digit was present */

	if (*cp == '+' || *cp == '-') *bp++ = *cp++;
	while (IS_LEX_NUMBER(*cp) || *cp == '\'')
		if (*cp != '\'') {
			*bp++ = *cp++;
			if (bp >= be) return 0;
			dig=1;
		}
		else cp++;
	if (*cp == ',' || *cp == '.') cp++;
	*bp++ = '.';
	if (bp >= be) return 0;
	while (IS_LEX_NUMBER(*cp) || *cp == '\'')
		if (*cp != '\'') {
			*bp++ = *cp++;
			if (bp >= be) return 0;
			dig=1;
		}
		else cp++;
	if (!dig) return 0;
	if (*cp == 'E' || *cp == 'e') {
			*bp++ = *cp++;
			if (bp >= be) return 0;
			dig = 0;
			if (*cp == '-' || *cp == '+') {
				*bp++ = *cp++;
				if (bp >= be) return 0;
			}
			while (IS_LEX_NUMBER(*cp)) {
				*bp++ = *cp++;
				if (bp >= be) return 0;
				dig=1;
			}
			if (!dig) return 0;
	}
	*bp = 0;
	return cp;
}


//
//  Scan_Decimal: C
//  
//      Scan and convert a decimal value.  Return zero if error.
//
const REBYTE *Scan_Decimal(const REBYTE *cp, REBCNT len, REBVAL *value, REBFLG dec_only)
{
	const REBYTE *bp = cp;
	REBYTE buf[MAX_NUM_LEN+4];
	REBYTE *ep = buf;
	REBOOL dig = FALSE;   /* flag that a digit was present */
	const char *se;

	if (len > MAX_NUM_LEN) return 0;

	if (*cp == '+' || *cp == '-') *ep++ = *cp++;
	while (IS_LEX_NUMBER(*cp) || *cp == '\'')
		if (*cp != '\'') *ep++ = *cp++, dig=1;
		else cp++;
	if (*cp == ',' || *cp == '.') cp++;
	*ep++ = '.';
	while (IS_LEX_NUMBER(*cp) || *cp == '\'')
		if (*cp != '\'') *ep++ = *cp++, dig=1;
		else cp++;
	if (!dig) return 0;
	if (*cp == 'E' || *cp == 'e') {
			*ep++ = *cp++;
			dig = 0;
			if (*cp == '-' || *cp == '+') *ep++ = *cp++;
			while (IS_LEX_NUMBER(*cp)) *ep++ = *cp++, dig=1;
			if (!dig) return 0;
	}
	if (*cp == '%') {
		if (dec_only) return 0;
		cp++; // ignore it
	}
	*ep = 0;

	if ((REBCNT)(cp-bp) != len) return 0;

	VAL_SET(value, REB_DECIMAL);

	// !!! need check for NaN, and INF
	VAL_DECIMAL(value) = STRTOD(s_cast(buf), &se);

	if (fabs(VAL_DECIMAL(value)) == HUGE_VAL) Trap_DEAD_END(RE_OVERFLOW);
	return cp;
}


//
//  Scan_Integer: C
//  
//      Scan and convert an integer value.  Return zero if error.
//      Allow preceding + - and any combination of ' marks.
//
const REBYTE *Scan_Integer(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	REBINT num = (REBINT)len;
	REBYTE buf[MAX_NUM_LEN+4];
	REBYTE *bp;
	REBI64 n;
	REBOOL neg = FALSE;

	// Super-fast conversion of zero and one (most common cases):
	if (num == 1) {
		if (*cp == '0') {SET_INTEGER(value, 0); return cp+1;}
		if (*cp == '1') {SET_INTEGER(value, 1); return cp+1;}
	}

	if (len > MAX_NUM_LEN) return 0; // prevent buffer overflow
	len = 0;
	bp = buf;

	// Strip leading signs:
	if (*cp == '-') *bp++ = *cp++, num--, neg = TRUE;
	else if (*cp == '+') cp++, num--;

	// Remove leading zeros:
	for (; num > 0; num--) {
		if (*cp == '0' || *cp == '\'') cp++;
		else break;
	}

	// Copy all digits, except ' :
	for (; num > 0; num--) {
		if (*cp >= '0' && *cp <= '9') *bp++ = *cp++;
		else if (*cp == '\'') cp++;
		else return 0;
	}
	*bp = 0;

	// Too many digits?
	len = bp - &buf[0];
	if (neg) len--;
	if (len > 19) return 0;

	// Convert, check, and return:
	errno = 0;
	n = CHR_TO_INT(buf);
	if (errno != 0) return 0; //overflow
	if ((n > 0 && neg) || (n < 0 && !neg)) return 0;
	SET_INTEGER(value, n);
	return cp;
}


//
//  Scan_Money: C
//  
//      Scan and convert money.  Return zero if error.
//
const REBYTE *Scan_Money(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	const REBYTE *end;

	if (*cp == '$') cp++, len--;
	if (len == 0) return 0;
	VAL_MONEY_AMOUNT(value) = string_to_deci(cp, &end);
	if (end != cp + len) return 0;
	VAL_SET(value, REB_MONEY);

	return end;

#ifdef ndef
	REBYTE *bp = cp;
	REBYTE buf[MAX_NUM_LEN+8];
	REBYTE *ep = buf;
	REBCNT n = 0;
	REBOOL dig = FALSE;

	if (*cp == '+') cp++;
	else if (*cp == '-') *ep++ = *cp++;

	if (*cp != '$') {
		for (; Upper_Case[*cp] >= 'A' && Upper_Case[*cp] <= 'Z' && n < 3; cp++, n++) {
			VAL_MONEY_DENOM(value)[n] = Upper_Case[*cp];
		}
		if (*cp != '$' || n > 3) return 0;
		VAL_MONEY_DENOM(value)[n] = 0;
	} else VAL_MONEY_DENOM(value)[0] = 0;
	cp++;

	while (ep < buf+MAX_NUM_LEN && (IS_LEX_NUMBER(*cp) || *cp == '\''))
		if (*cp != '\'') *ep++ = *cp++, dig=1;
		else cp++;
	if (*cp == ',' || *cp == '.') cp++;
	*ep++ = '.';
	while (ep < buf+MAX_NUM_LEN && (IS_LEX_NUMBER(*cp) || *cp == '\''))
		if (*cp != '\'') *ep++ = *cp++, dig=1;
		else cp++;
	if (!dig) return 0;
	if (ep >= buf+MAX_NUM_LEN) return 0;
	*ep = 0;

	if ((REBCNT)(cp-bp) != len) return 0;
	VAL_SET(value, REB_MONEY);
	VAL_MONEY_AMOUNT(value) = atof((char*)(&buf[0]));
	if (fabs(VAL_MONEY_AMOUNT(value)) == HUGE_VAL) Trap_DEAD_END(RE_OVERFLOW);
	return cp;
#endif
}


//
//  Scan_Date: C
//  
//      Scan and convert a date. Also can include a time and zone.
//
const REBYTE *Scan_Date(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	const REBYTE *ep;
	const REBYTE *end = cp + len;
	REBINT num;
	REBINT day = 0;
	REBINT month;
	REBINT year;
	REBINT tz = 0;
	REBYTE sep;
	REBCNT size;

	// Skip spaces:
	for (; *cp == ' ' && cp != end; cp++);

	// Skip day name, comma, and spaces:
	for (ep = cp; *ep != ',' && ep != end; ep++);
	if (ep != end) {
		cp = ep + 1;
		while (*cp == ' ' && cp != end) cp++;
	}
	if (cp == end) return 0;

	// Day or 4-digit year:
	ep = Grab_Int(cp, &num);
	if (num < 0) return 0;
	size = (REBCNT)(ep - cp);
	if (size >= 4) year = num;
	else if (size) day = num;
	else return 0;
	cp = ep;

	// Determine field separator:
	if (*cp != '/' && *cp != '-' && *cp != '.' && *cp != ' ') return 0;
	sep = *cp++;

	// Month as number or name:
	ep = Grab_Int(cp, &num);
	if (num < 0) return 0;
	size = (REBCNT)(ep - cp);
	if (size > 0) month = num; 	// got a number
	else {		// must be a word
		for (ep = cp; IS_LEX_WORD(*ep); ep++); // scan word
		size = (REBCNT)(ep - cp);
		if (size < 3) return 0;
		for (num = 0; num < 12; num++) {
			if (!Compare_Bytes(cb_cast(Month_Names[num]), cp, size, TRUE)) break;
		}
		month = num + 1;
	}
	if (month < 1 || month > 12) return 0;
	cp = ep;
	if (*cp++ != sep) return 0;

	// Year or day (if year was first):
	ep = Grab_Int(cp, &num);
	if (*cp == '-' || num < 0) return 0;
	size = (REBCNT)(ep - cp);
	if (!size) return 0;
	if (!day) day = num;
	else {	// it is a year
		// Allow shorthand form (e.g. /96) ranging +49,-51 years
		//		(so in year 2050 a 0 -> 2000 not 2100)
		if (size >= 3) year = num;
		else {
			year = (Current_Year / 100) * 100 + num;
			if (year - Current_Year > 50) year -=100;
			else if (year - Current_Year < -50) year += 100;
		}
	}
	if (year > MAX_YEAR || day < 1 || day > Month_Max_Days[month-1]) return 0;
	// Check February for leap year or century:
	if (month == 2 && day == 29) {
		if (((year % 4) != 0) ||		// not leap year
			((year % 100) == 0 && 		// century?
			(year % 400) != 0)) return 0; // not leap century
	}

	cp = ep;
	VAL_TIME(value) = NO_TIME;
	if (cp >= end) goto end_date;

	if (*cp == '/' || *cp == ' ') {
		sep = *cp++;
		if (cp >= end) goto end_date;
		cp = Scan_Time(cp, 0, value);
		if (!IS_TIME(value) || (VAL_TIME(value) < 0) || (VAL_TIME(value) >= TIME_SEC(24 * 60 * 60)))
			return 0;
	}

	if (*cp == sep) cp++;

	// Time zone can be 12:30 or 1230 (optional hour indicator)
	if (*cp == '-' || *cp == '+') {
		if (cp >= end) goto end_date;
		ep = Grab_Int(cp+1, &num);
		if (ep-cp == 0) return 0;
		if (*ep != ':') {
			int h, m;
			if (num < -1500 || num > 1500) return 0;
			h = (num / 100);
			m = (num - (h * 100));
			tz = (h * 60 + m) / ZONE_MINS;
		} else {
			if (num < -15 || num > 15) return 0;
			tz = num * (60/ZONE_MINS);
			if (*ep == ':') {
				ep = Grab_Int(ep+1, &num);
				if (num % ZONE_MINS != 0) return 0;
				tz += num / ZONE_MINS;
			}
		}
		if (ep != end) return 0;
		if (*cp == '-') tz = -tz;
		cp = ep;
	}
end_date:
	Set_Date_UTC(value, year, month, day, VAL_TIME(value), tz);
	return cp;
}


//
//  Scan_File: C
//  
//      Scan and convert a file name.
//
const REBYTE *Scan_File(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	REBUNI term = 0;
	const REBYTE *invalid = cb_cast(":;()[]\"");

	if (*cp == '%') cp++, len--;
	if (*cp == '"') {
		cp++;
		len--;
		term = '"';
		invalid = cb_cast(":;\"");
	}
	cp = Scan_Item(cp, cp+len, term, invalid);
	if (cp)
		Val_Init_File(value, Copy_String(BUF_MOLD, 0, -1));
	return cp;

#ifdef ndef
	extern REBYTE *Scan_Quote(REBYTE *src, SCAN_STATE *scan_state);

	if (*cp == '%') cp++, len--;
	if (len == 0) return 0;
	if (*cp == '"') {
		cp = Scan_Quote(cp, 0);
		if (cp) {
			int need_changes;
			Val_Init_String(value, Copy_String(BUF_MOLD, 0, -1));
			VAL_SET(value, REB_FILE);
		}
		return cp;
	}

	VAL_SERIES(value) = Make_Binary(len);
	VAL_INDEX(value) = 0;

	str = VAL_BIN(value);
	for (; len > 0; len--) {
		if (*cp == '%' && len > 2 && Scan_Hex2(cp+1, &n, FALSE)) {
			*str++ = n;
			cp += 3;
			len -= 2;
		}
		else if (*cp == '\\') cp++, *str++ = '/';
		else if (strchr(":;()[]\"", *cp)) return 0;  // chars not allowed in files !!!
		else *str++ = *cp++;
	}
	*str = 0;
	VAL_TAIL(value) = (REBCNT)(str - VAL_BIN(value));
	VAL_SET(value, REB_FILE);
	return cp;
#endif
}


//
//  Scan_Email: C
//  
//      Scan and convert email.
//
const REBYTE *Scan_Email(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	REBYTE *str;
	REBOOL at = FALSE;
	REBUNI n;

	VAL_SET(value, REB_EMAIL);
	VAL_SERIES(value) = Make_Binary(len);
	VAL_INDEX(value) = 0;

	str = VAL_BIN(value);
	for (; len > 0; len--) {
		if (*cp == '@') {
			if (at) return 0;
			at = TRUE;
		}
		if (*cp == '%') {
			if (len <= 2 || !Scan_Hex2(cp+1, &n, FALSE)) return 0;
			*str++ = (REBYTE)n;
			cp += 3;
			len -= 2;
		}
		else *str++ = *cp++;
	}
	*str = 0;
	if (!at) return 0;
	VAL_TAIL(value) = (REBCNT)(str - VAL_BIN(value));

	MANAGE_SERIES(VAL_SERIES(value));

	return cp;
}


//
//  Scan_URL: C
//  
//      Scan and convert a URL.
//
const REBYTE *Scan_URL(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	REBYTE *str;
	REBUNI n;

//  !!! Need to check for any possible scheme followed by ':'

//	for (n = 0; n < URL_MAX; n++) {
//		if (str = Match_Bytes(cp, (REBYTE *)(URL_Schemes[n]))) break;
//	}
//	if (n >= URL_MAX) return 0;
//	if (*str != ':') return 0;

	VAL_SET(value, REB_URL);
	VAL_SERIES(value) = Make_Binary(len);
	VAL_INDEX(value) = 0;

	str = VAL_BIN(value);
	for (; len > 0; len--) {
		//if (*cp == '%' && len > 2 && Scan_Hex2(cp+1, &n, FALSE)) {
		if (*cp == '%') {
			if (len <= 2 || !Scan_Hex2(cp+1, &n, FALSE)) return 0;
			*str++ = (REBYTE)n;
			cp += 3;
			len -= 2;
		}
		else *str++ = *cp++;
	}
	*str = 0;
	VAL_TAIL(value) = (REBCNT)(str - VAL_BIN(value));

	// All scanned code is assumed to be managed
	MANAGE_SERIES(VAL_SERIES(value));
	return cp;
}


//
//  Scan_Pair: C
//  
//      Scan and convert a pair
//
const REBYTE *Scan_Pair(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	const REBYTE *ep, *xp;
	REBYTE buf[MAX_NUM_LEN+4];

	ep = cp;
	//ep = Grab_Int(ep, &n);
	ep = Scan_Dec_Buf(cp, MAX_NUM_LEN, &buf[0]);
	if (!ep) return 0;
	VAL_PAIR_X(value) = (float)atof((char*)(&buf[0])); //n;
	if (*ep != 'x' && *ep != 'X') return 0;
	ep++;

	xp = Scan_Dec_Buf(ep, MAX_NUM_LEN, &buf[0]);
	if (!xp) return 0;
	VAL_PAIR_Y(value) = (float)atof((char*)(&buf[0])); //n;

	if (len > (REBCNT)(xp - cp)) return 0;
	VAL_SET(value, REB_PAIR);
	return xp;
}


//
//  Scan_Tuple: C
//  
//      Scan and convert a tuple.
//
const REBYTE *Scan_Tuple(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	const REBYTE *ep;
	REBYTE *tp;
	REBCNT size = 1;
	REBINT n;

	if (len == 0) return 0;
	for (n = (REBINT)len, ep = cp; n > 0; n--, ep++)  // count '.'
		if (*ep == '.') size++;
	if (size > MAX_TUPLE) return 0;
	if (size < 3) size = 3;
	VAL_TUPLE_LEN(value) = (REBYTE)size;
	tp = VAL_TUPLE(value);
	memset(tp, 0, sizeof(REBTUP)-2);
	for (ep = cp; len > (REBCNT)(ep - cp); ep++) {
		ep = Grab_Int(ep, &n);
		if (n < 0 || n > 255) return 0;
		*tp++ = (REBYTE)n;
		if (*ep != '.') break;
	}
	if (len > (REBCNT)(ep - cp)) return 0;
	VAL_SET(value, REB_TUPLE);
	return ep;
}


//
//  Scan_Binary: C
//  
//      Scan and convert binary strings.
//
const REBYTE *Scan_Binary(const REBYTE *cp, REBCNT len, REBVAL *value)
{
	const REBYTE *ep;
	REBINT base = 16;

	if (*cp != '#') {
		ep = Grab_Int(cp, &base);
		if (cp == ep || *ep != '#') return 0;
		len -= (REBCNT)(ep - cp);
		cp = ep;
	}
	cp++;  // skip #
	if (*cp++ != '{') return 0;
	len -= 2;

	cp = Decode_Binary(value, cp, len, base, '}');
	if (!cp) return 0;

	cp = Skip_To_Byte(cp, cp + len, '}');
	if (!cp) return 0; // series will be gc'd

	return cp;
}


//
//  Scan_Any: C
//  
//      Scan any string that does not require special decoding.
//
const REBYTE *Scan_Any(const REBYTE *cp, REBCNT len, REBVAL *value, REBYTE type)
{
	REBCNT n;

	VAL_SET(value, type);
	VAL_SERIES(value) = Append_UTF8(0, cp, len);
	VAL_INDEX(value) = 0;

	// We hand it over to management by the GC, but don't run the GC before
	// the source has been scanned and put somewhere safe!
	MANAGE_SERIES(VAL_SERIES(value));

	if (VAL_BYTE_SIZE(value)) {
		n = Deline_Bytes(VAL_BIN(value), VAL_LEN(value));
	} else {
		n = Deline_Uni(VAL_UNI(value), VAL_LEN(value));
	}
	VAL_TAIL(value) = n;

	return cp + len;
}


//
//  Append_Markup: C
//  
//      Add a new string or tag to a markup block, advancing the tail.
//
static void Append_Markup(REBSER *series, enum Reb_Kind type, const REBYTE *bp, REBINT len)
{
	REBVAL *val;
	if (SERIES_FULL(series)) Extend_Series(series, 8);
	val = BLK_TAIL(series);
	SET_END(val);
	series->tail++;
	SET_END(val+1);
	Val_Init_Series(val, type, Append_UTF8(0, bp, len));
}


//
//  Load_Markup: C
//  
//      Scan a string as HTML or XML and convert it to a block
//      of strings and tags.  Return the block as a series.
//
REBSER *Load_Markup(const REBYTE *cp, REBINT len)
{
	const REBYTE *bp = cp;
	REBSER *series;
	REBYTE quote;

	series = Make_Array(16);

	while (len > 0) {
		// Look for tag, gathering text as we go:
		for (; len > 0 && *cp != '<'; len--, cp++);
		if (len <= 0) break;
		if (!IS_LEX_WORD(cp[1]) && cp[1] != '/' && cp[1] != '?' && cp[1] != '!') {
			cp++; len--; continue;
		}
		if (cp != bp) Append_Markup(series, REB_STRING, bp, cp - bp);
		bp = ++cp;  // skip <

		// Check for comment tag:
		if (*cp == '!' && len > 7 && cp[1] == '-' && cp[2] == '-') {
			for (len -= 3, cp += 3; len > 2 &&
				!(*cp == '-' && cp[1] == '-' && cp[2] == '>'); cp++, len--);
			if (len > 2) cp += 2, len -= 2;
			// fall into tag code below...
		}
		// Look for end of tag, watch for quotes:
		for (len--; len > 0; len--, cp++) {
			if (*cp == '>') {
				Append_Markup(series, REB_TAG, bp, cp - bp);
				bp = ++cp; len--;
				break;
			}
			if (*cp == '"' || *cp == '\'') { // quote in tag
				quote = *cp++;
				for (len--; len > 0 && *cp != quote; len--, cp++); // find end quote
				if (len <= 0) break;
			}
		}
		// Note: if final tag does not end, then it is treated as text.
	}
	if (cp != bp) Append_Markup(series, REB_STRING, bp, cp - bp);

	return series;
}


//
//  Construct_Value: C
//  
//      Lexical datatype constructor. Return TRUE on success.
//  
//      This function makes datatypes that are not normally expressible
//      in unevaluated source code format. The format of the datatype
//      constructor is:
//  
//          #[datatype! | keyword spec]
//  
//      The first item is a datatype word or NONE, FALSE or TRUE. The
//      second part is a specification for the datatype, as a basic
//      type (such as a string) or a block.
//  
//      Keep in mind that this function is being called as part of the
//      scanner, so optimal performance is critical.
//
REBFLG Construct_Value(REBVAL *value, REBSER *spec)
{
	REBVAL *val;
	REBCNT type;
	MAKE_FUNC func;

	val = BLK_HEAD(spec);

	if (!IS_WORD(val)) return FALSE;

	Val_Init_Block(value, spec); //GC

	// Handle the datatype or keyword:
	type = VAL_WORD_CANON(val);
	if (type > REB_MAX) { // >, not >=, because they are one-based

		switch (type) {

		case SYM_NONE:
			SET_NONE(value);
			return TRUE;

		case SYM_FALSE:
			SET_FALSE(value);
			return TRUE;

		case SYM_TRUE:
			SET_TRUE(value);
			return TRUE;

		default:
			return FALSE;
		}
	}
	type--;	// The global word for datatype x is at word x+1.

	// Check for trivial types:
	if (type == REB_UNSET) {
		SET_UNSET(value);
		return TRUE;
	}
	if (type == REB_NONE) {
		SET_NONE(value);
		return TRUE;
	}

	val++;
	if (IS_END(val)) return FALSE;

	// Dispatch maker:
	if ((func = Make_Dispatch[type])) {
		if (func(value, val, type)) return TRUE;
	}

	return FALSE;
}


//
//  Scan_Net_Header: C
//  
//      Scan an Internet-style header (HTTP, SMTP).
//      Fields with duplicate words will be merged into a block.
//
REBSER *Scan_Net_Header(REBSER *blk, REBYTE *str)
{
	REBYTE *cp = str;
	REBYTE *start;
	REBVAL *val;
	REBINT len;
	REBSER *ser;

	while (IS_LEX_ANY_SPACE(*cp)) cp++; // skip white space

	while (1) {
		// Scan valid word:
		if (GET_LEX_CLASS(*cp) == LEX_CLASS_WORD) {
			start = cp;
			while (
				IS_LEX_AT_LEAST_WORD(*cp) || *cp == '.' || *cp == '-' || *cp == '_'
			) cp++; // word char or number
		}
		else break;

		if (*cp == ':') {
			REBCNT sym = Make_Word(start, cp-start);
			cp++;
			// Search if word already present:
			for (val = BLK_HEAD(blk); NOT_END(val); val += 2) {
				if (VAL_WORD_SYM(val) == sym) {
					// Does it already use a block?
					if (IS_BLOCK(val+1)) {
						// Block of values already exists:
						val = Alloc_Tail_Array(VAL_SERIES(val+1));
						SET_NONE(val);
					}
					else {
						// Create new block for values:
						REBVAL *val2;
						ser = Make_Array(2);
						val2 = Alloc_Tail_Array(ser); // prior value
						*val2 = val[1];
						Val_Init_Block(val + 1, ser);
						val = Alloc_Tail_Array(ser); // for new value
						SET_NONE(val);
					}
					break;
				}
			}
			if (IS_END(val)) {
				val = Alloc_Tail_Array(blk); // add new word
				Val_Init_Word_Unbound(val, REB_SET_WORD, sym);
				val = Alloc_Tail_Array(blk); // for new value
				SET_NONE(val);
			}
		}
		else break;

		// Get value:
		while (IS_LEX_SPACE(*cp)) cp++;
		start = cp;
		len = 0;
		while (NOT_NEWLINE(*cp)) len++, cp++;
		// Is it continued on next line?
		while (*cp) {
			if (*cp == CR) cp++;
			if (*cp == LF) cp++;
			if (IS_LEX_SPACE(*cp)) {
				while (IS_LEX_SPACE(*cp)) cp++;
				while (NOT_NEWLINE(*cp)) len++, cp++;
			}
			else break;
		}

		// Create string value (ignoring lines and indents):
		ser = Make_Binary(len);
		ser->tail = len;
		str = STR_HEAD(ser);
		cp = start;
		// Code below *MUST* mirror that above:
		while (NOT_NEWLINE(*cp)) *str++ = *cp++;
		while (*cp) {
			if (*cp == CR) cp++;
			if (*cp == LF) cp++;
			if (IS_LEX_SPACE(*cp)) {
				while (IS_LEX_SPACE(*cp)) cp++;
				while (NOT_NEWLINE(*cp)) *str++ = *cp++;
			}
			else break;
		}
		*str = 0;
		Val_Init_String(val, ser);
	}

	return blk;
}
