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
**  Module:  t-tuple.c
**  Summary: tuple datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  CT_Tuple: C
//

REBINT CT_Tuple(REBVAL *a, REBVAL *b, REBINT mode)
{
	REBINT num = Cmp_Tuple(a, b);
	if (mode > 1) return (num == 0 && VAL_TUPLE_LEN(a) == VAL_TUPLE_LEN(b));
	if (mode >= 0)  return (num == 0);
	if (mode == -1) return (num >= 0);
	return (num > 0);
}


//
//  MT_Tuple: C
//

REBFLG MT_Tuple(REBVAL *out, REBVAL *data, REBCNT type)
{
	REBYTE	*vp;
	REBINT len = 0;
	REBINT n;

	vp = VAL_TUPLE(out);
	for (; NOT_END(data); data++, vp++, len++) {
		if (len >= 10) return FALSE;
		if (IS_INTEGER(data)) {
			n = Int32(data);
		}
		else if (IS_CHAR(data)) {
			n = VAL_CHAR(data);
		}
		else return FALSE;
		if (n > 255 || n < 0) return FALSE;
		*vp = n;
	}

	VAL_TUPLE_LEN(out) = len;

	for (; len < 10; len++) *vp++ = 0;

	VAL_SET(out, type);
	return TRUE;
}


//
//  Cmp_Tuple: C
//  
//  Given two tuples, compare them.
//

REBINT Cmp_Tuple(const REBVAL *t1, const REBVAL *t2)
{
	REBCNT	len;
	const REBYTE *vp1, *vp2;
	REBINT	n;

	len = MAX(VAL_TUPLE_LEN(t1), VAL_TUPLE_LEN(t2));
	vp1 = VAL_TUPLE(t1);
	vp2 = VAL_TUPLE(t2);

	for (;len > 0; len--, vp1++,vp2++) {
		n = (REBINT)(*vp1 - *vp2);
		if (n != 0)
			return n;
	}
	return 0;
}


//
//  PD_Tuple: C
//  
//      Implements PATH and SET_PATH for tuple.
//      Sets DS_TOP if found. Always returns 0.
//

REBINT PD_Tuple(REBPVS *pvs)
{
	REBVAL *val;
	REBINT n;
	REBINT i;
	REBYTE *dat;
	REBINT len;

	dat = VAL_TUPLE(pvs->value);
	len = VAL_TUPLE_LEN(pvs->value);
	if (len < 3) len = 3;
	n = Get_Num_Arg(pvs->select);

	if ((val = pvs->setval)) {
		if (n <= 0 || n > MAX_TUPLE) return PE_BAD_SELECT;
		if (IS_INTEGER(val) || IS_DECIMAL(val)) i = Int32(val);
		else if (IS_NONE(val)) {
			n--;
			CLEAR(dat+n, MAX_TUPLE-n);
			VAL_TUPLE_LEN(pvs->value) = n;
			return PE_OK;
		}
		else return PE_BAD_SET;
		if (i < 0) i = 0;
		else if (i > 255) i = 255;
		dat[n-1] = i;
		if (n > len) VAL_TUPLE_LEN(pvs->value) = n;
		return PE_OK;
	} else {
		if (n > 0 && n <= len) {
			SET_INTEGER(pvs->store, dat[n-1]);
			return PE_USE;
		}
		else return PE_NONE;
	}
}


//
//  Emit_Tuple: C
//  
//      The out array must be large enough to hold longest tuple.
//      Longest is: (3 digits + '.') * 11 nums + 1 term => 45
//

REBINT Emit_Tuple(const REBVAL *value, REBYTE *out)
{
	REBCNT len = VAL_TUPLE_LEN(value);
	const REBYTE *tp = cast(const REBYTE *, VAL_TUPLE(value));
	REBYTE *start = out;

	for (; len > 0; len--, tp++) {
		out = Form_Int(out, *tp);
		*out++ = '.';
	}

	len = VAL_TUPLE_LEN(value);
	while (len++ < 3) {
		*out++ = '0';
		*out++ = '.';
	}
	*--out = 0;

	return out-start;
}


//
//  REBTYPE: C
//

REBTYPE(Tuple)
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;
	REBYTE	*vp;
	const REBYTE *ap;
	REBCNT len;
	REBCNT alen;
	REBINT	a;
	REBDEC	dec;

	if (IS_TUPLE(value)) {
		vp = VAL_TUPLE(value);
		len = VAL_TUPLE_LEN(value);
	} else if (!(IS_DATATYPE(value) && (action == A_MAKE || action == A_TO))) {
		Trap_Arg_DEAD_END(value);
	}

	if (IS_BINARY_ACT(action)) {
		assert(vp);

		if (IS_INTEGER(arg)) {
			a = VAL_INT32(arg);
			ap = 0;
		} else if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
			dec=VAL_DECIMAL(arg);
			a = (REBINT)dec;
			ap = 0;
		} else if (IS_TUPLE(arg)) {
			ap = VAL_TUPLE(arg);
			alen = VAL_TUPLE_LEN(arg);
			if (len < alen)
				len = VAL_TUPLE_LEN(value) = alen;
		} else Trap_Math_Args(REB_TUPLE, action);

		for (;len > 0; len--, vp++) {
			REBINT v = *vp;
			if (ap)
				a = (REBINT) *ap++;
			switch (action) {
			case A_ADD:	v += a; break;
			case A_SUBTRACT: v -= a; break;
			case A_MULTIPLY:
				if (IS_DECIMAL(arg) || IS_PERCENT(arg))
					v=(REBINT)(v*dec);
				else
					v *= a;
				break;
			case A_DIVIDE:
				if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
					if (dec == 0.0) Trap_DEAD_END(RE_ZERO_DIVIDE);
					v=(REBINT)Round_Dec(v/dec, 0, 1.0);
				} else {
					if (a == 0) Trap_DEAD_END(RE_ZERO_DIVIDE);
					v /= a;
				}
				break;
			case A_REMAINDER:
				if (a == 0) Trap_DEAD_END(RE_ZERO_DIVIDE);
				v %= a;
				break;
			case A_AND:	v &= a; break;
			case A_OR:	v |= a; break;
			case A_XOR:	v ^= a; break;
			default:
				Trap_Action_DEAD_END(REB_TUPLE, action);
			}

			if (v > 255) v = 255;
			else if (v < 0) v = 0;
			*vp = (REBYTE) v;
		}
		goto ret_value;
	}

	// !!!! merge with SWITCH below !!!
	if (action == A_COMPLEMENT) {
		for (;len > 0; len--, vp++)
			*vp = (REBYTE)~*vp;
		goto ret_value;
	}
	if (action == A_RANDOM) {
		if (D_REF(2)) Trap_DEAD_END(RE_BAD_REFINES); // seed
		for (;len > 0; len--, vp++) {
			if (*vp)
				*vp = (REBYTE)(Random_Int(D_REF(3)) % (1+*vp));
		}
		goto ret_value;
	}
/*
	if (action == A_ZEROQ) {
		for (;len > 0; len--, vp++) {
			if (*vp != 0)
				goto is_false;
		}
		goto is_true;
	}
*/
	//a = 1; //???
	switch (action) {
	case A_LENGTH:
		len = MAX(len, 3);
		SET_INTEGER(D_OUT, len);
		return R_OUT;

	case A_PICK:
		Pick_Path(D_OUT, value, arg, 0);
		return R_OUT;

///	case A_POKE:
///		Pick_Path(D_OUT, value, arg, D_ARG(3));
///		return R_ARG3;

    case A_REVERSE:
		if (D_REF(2)) {
			len = Get_Num_Arg(D_ARG(3));
			len = MIN(len, VAL_TUPLE_LEN(value));
		}
		if (len > 0) {
			REBCNT i;
			//len = MAX(len, 3);
			for (i = 0; i < len/2; i++) {
				a = vp[len - i - 1];
				vp[len - i - 1] = vp[i];
				vp[i] = a;
			}
		}
		goto ret_value;
/*
  poke_it:
		a = Get_Num_Arg(arg);
		if (a <= 0 || a > len) {
			if (action == A_PICK) return R_NONE;
			Trap_Range_DEAD_END(arg);
		}
		if (action == A_PICK) {
			SET_INTEGER(D_OUT, vp[a-1]);
			return R_OUT;
		}
		// Poke:
		if (!IS_INTEGER(D_ARG(3))) Trap_Arg_DEAD_END(D_ARG(3));
		v = VAL_INT32(D_ARG(3));
		if (v < 0)
			v = 0;
		if (v > 255)
			v = 255;
		vp[a-1] = v;
		goto ret_value;

*/
	case A_MAKE:
	case A_TO:
		if (IS_TUPLE(arg)) {
			return R_ARG2;
		}
		if (IS_STRING(arg)) {
			ap = Qualify_String(arg, 11*4+1, &len, FALSE); // can trap, ret diff str
			if (Scan_Tuple(ap, len, D_OUT)) return R_OUT;
			goto bad_arg;
		}
		if (ANY_BLOCK(arg)) {
			if (!MT_Tuple(D_OUT, VAL_BLK_DATA(arg), REB_TUPLE)) Trap_Make_DEAD_END(REB_TUPLE, arg);
			return R_OUT;
		}

		VAL_SET(value, REB_TUPLE);
		vp = VAL_TUPLE(value);
		if (IS_ISSUE(arg)) {
			REBUNI c;
			ap = Get_Word_Name(arg);
			len = LEN_BYTES(ap);  // UTF-8 len
			if (len & 1) goto bad_arg; // must have even # of chars
			len /= 2;
			if (len > MAX_TUPLE) goto bad_arg; // valid even for UTF-8
			VAL_TUPLE_LEN(value) = len;
			for (alen = 0; alen < len; alen++) {
				if (!Scan_Hex2(ap, &c, 0)) goto bad_arg;
				*vp++ = (REBYTE)c;
				ap += 2;
			}
		}
		else if (IS_BINARY(arg)) {
			ap = VAL_BIN_DATA(arg);
			len = VAL_LEN(arg);
			if (len > MAX_TUPLE) len = MAX_TUPLE;
			VAL_TUPLE_LEN(value) = len;
			for (alen = 0; alen < len; alen++) *vp++ = *ap++;
		}
		else goto bad_arg;

		for (; alen < MAX_TUPLE; alen++) *vp++ = 0;
		goto ret_value;

bad_arg:
		Trap_Make_DEAD_END(REB_TUPLE, arg);
	}

	Trap_Action_DEAD_END(REB_TUPLE, action);

ret_value:
	*D_OUT = *value;
	return R_OUT;
}
