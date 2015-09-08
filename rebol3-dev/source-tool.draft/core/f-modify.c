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
**  Module:  f-modify.c
**  Summary: block series modification (insert, append, change)
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  Modify_Array: C
//  
//      action: INSERT, APPEND, CHANGE
//  
//      dst_ser:    target
//      dst_idx:    position
//      src_val:    source
//      flags:        AN_ONLY, AN_PART
//      dst_len:    length to remove
//      dups:        dup count
//  
//      return: new dst_idx
//

REBCNT Modify_Array(REBCNT action, REBSER *dst_ser, REBCNT dst_idx, const REBVAL *src_val, REBCNT flags, REBINT dst_len, REBINT dups)
{
	REBCNT tail  = SERIES_TAIL(dst_ser);
	REBINT ilen  = 1;	// length to be inserted
	REBINT size;		// total to insert

#if !defined(NDEBUG)
	REBINT index;
#endif

	if (dups < 0) return (action == A_APPEND) ? 0 : dst_idx;
	if (action == A_APPEND || dst_idx > tail) dst_idx = tail;

	// Check /PART, compute LEN:
	if (!GET_FLAG(flags, AN_ONLY) && ANY_BLOCK(src_val)) {
		// Adjust length of insertion if changing /PART:
		if (action != A_CHANGE && GET_FLAG(flags, AN_PART))
			ilen = dst_len;
		else
			ilen = VAL_LEN(src_val);

		// Are we modifying ourselves? If so, copy src_val block first:
		if (dst_ser == VAL_SERIES(src_val)) {
			REBSER *series = Copy_Array_At_Shallow(
				VAL_SERIES(src_val), VAL_INDEX(src_val)
			);
			src_val = BLK_HEAD(series);
		}
		else
			src_val = VAL_BLK_DATA(src_val); // skips by VAL_INDEX values
	}

	// Total to insert:
	size = dups * ilen;

	if (action != A_CHANGE) {
		// Always expand dst_ser for INSERT and APPEND actions:
		Expand_Series(dst_ser, dst_idx, size);
	} else {
		if (size > dst_len)
			Expand_Series(dst_ser, dst_idx, size-dst_len);
		else if (size < dst_len && GET_FLAG(flags, AN_PART))
			Remove_Series(dst_ser, dst_idx, dst_len-size);
		else if (size + dst_idx > tail) {
			EXPAND_SERIES_TAIL(dst_ser, size - (tail - dst_idx));
		}
	}

	tail = (action == A_APPEND) ? 0 : size + dst_idx;

#if !defined(NDEBUG)
	for (index = 0; index < ilen; index++) {
		if (SERIES_GET_FLAG(dst_ser, SER_MANAGED))
			ASSERT_VALUE_MANAGED(&src_val[index]);
	}
#endif

	dst_idx *= SERIES_WIDE(dst_ser); // loop invariant
	ilen  *= SERIES_WIDE(dst_ser); // loop invariant
	for (; dups > 0; dups--) {
		memcpy(dst_ser->data + dst_idx, src_val, ilen);
		dst_idx += ilen;
	}
	BLK_TERM(dst_ser);

	return tail;
}


//
//  Modify_String: C
//  
//      action: INSERT, APPEND, CHANGE
//  
//      dst_ser:    target
//      dst_idx:    position
//      src_val:    source
//      flags:        AN_PART
//      dst_len:    length to remove
//      dups:        dup count
//  
//      return: new dst_idx
//

REBCNT Modify_String(REBCNT action, REBSER *dst_ser, REBCNT dst_idx, const REBVAL *src_val, REBCNT flags, REBINT dst_len, REBINT dups)
{
	REBSER *src_ser = 0;
	REBCNT src_idx = 0;
	REBCNT src_len;
	REBCNT tail  = SERIES_TAIL(dst_ser);
	REBINT size;		// total to insert
	REBOOL needs_free;

	if (dups < 0) return (action == A_APPEND) ? 0 : dst_idx;
	if (action == A_APPEND || dst_idx > tail) dst_idx = tail;

	// If the src_val is not a string, then we need to create a string:
	if (GET_FLAG(flags, AN_SERIES)) { // used to indicate a BINARY series
		if (IS_INTEGER(src_val)) {
			src_ser = Make_Series_Codepoint(Int8u(src_val));
			needs_free = TRUE;
		}
		else if (IS_BLOCK(src_val)) {
			src_ser = Join_Binary(src_val); // NOTE: it's the shared FORM buffer!
			needs_free = FALSE;
		}
		else if (IS_CHAR(src_val)) {
			// "UTF-8 was originally specified to allow codepoints with up to
			// 31 bits (or 6 bytes). But with RFC3629, this was reduced to 4
			// bytes max. to be more compatible to UTF-16."  So depending on
			// which RFC you consider "the UTF-8", max size is either 4 or 6.
			src_ser = Make_Binary(6);
			src_ser->tail = Encode_UTF8_Char(BIN_HEAD(src_ser), VAL_CHAR(src_val));
			needs_free = TRUE;
		}
		else if (!ANY_BINSTR(src_val)) Trap_Arg_DEAD_END(src_val);
	}
	else if (IS_CHAR(src_val)) {
		src_ser = Make_Series_Codepoint(VAL_CHAR(src_val));
		needs_free = TRUE;
	}
	else if (IS_BLOCK(src_val)) {
		src_ser = Form_Tight_Block(src_val);
		needs_free = TRUE;
	}
	else if (!ANY_STR(src_val) || IS_TAG(src_val)) {
		src_ser = Copy_Form_Value(src_val, 0);
		needs_free = TRUE;
	}

	// Use either new src or the one that was passed:
	if (src_ser) {
		src_len = SERIES_TAIL(src_ser);
	}
	else {
		src_ser = VAL_SERIES(src_val);
		src_idx = VAL_INDEX(src_val);
		src_len = VAL_LEN(src_val);
		needs_free = FALSE;
	}

	// For INSERT or APPEND with /PART use the dst_len not src_len:
	if (action != A_CHANGE && GET_FLAG(flags, AN_PART)) src_len = dst_len;

	// If Source == Destination we need to prevent possible conflicts.
	// Clone the argument just to be safe.
	// (Note: It may be possible to optimize special cases like append !!)
	if (dst_ser == src_ser) {
		assert(!needs_free);
		src_ser = Copy_Sequence_At_Len(src_ser, src_idx, src_len);
		needs_free = TRUE;
		src_idx = 0;
	}

	// Total to insert:
	size = dups * src_len;

	if (action != A_CHANGE) {
		// Always expand dst_ser for INSERT and APPEND actions:
		Expand_Series(dst_ser, dst_idx, size);
	} else {
		if (size > dst_len)
			Expand_Series(dst_ser, dst_idx, size - dst_len);
		else if (size < dst_len && GET_FLAG(flags, AN_PART))
			Remove_Series(dst_ser, dst_idx, dst_len - size);
		else if (size + dst_idx > tail) {
			EXPAND_SERIES_TAIL(dst_ser, size - (tail - dst_idx));
		}
	}

	// For dup count:
	for (; dups > 0; dups--) {
		Insert_String(dst_ser, dst_idx, src_ser, src_idx, src_len, TRUE);
		dst_idx += src_len;
	}

	TERM_SERIES(dst_ser);

	if (needs_free) {
		// If we did not use the series that was passed in, but rather
		// created an internal temporary one, we need to free it.
		Free_Series(src_ser);
	}

	return (action == A_APPEND) ? 0 : dst_idx;
}
