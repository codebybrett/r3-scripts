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
**  Module:  f-stubs.c
**  Summary: miscellaneous little functions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-deci-funcs.h"

//
//  REBCNT_To_Bytes: C
//
void REBCNT_To_Bytes(REBYTE *out, REBCNT in)
{
	assert(sizeof(REBCNT) == 4);
	out[0] = (REBYTE) in;
	out[1] = (REBYTE)(in >> 8);
	out[2] = (REBYTE)(in >> 16);
	out[3] = (REBYTE)(in >> 24);
}


//
//  Bytes_To_REBCNT: C
//
REBCNT Bytes_To_REBCNT(const REBYTE * const in)
{
	assert(sizeof(REBCNT) == 4);
	return (REBCNT) in[0]          // & 0xFF
		| (REBCNT)  in[1] <<  8    // & 0xFF00;
		| (REBCNT)  in[2] << 16    // & 0xFF0000;
		| (REBCNT)  in[3] << 24;   // & 0xFF000000;
}


//
//  Find_Int: C
//
REBCNT Find_Int(REBINT *array, REBINT num)
{
	REBCNT n;

	for (n = 0; array[n] && array[n] != num; n++);
	if (array[n]) return n;
	return NOT_FOUND;
}


//
//  Get_Num_Arg: C
//  
//      Get the amount to skip or pick.
//      Allow multiple types. Throw error if not valid.
//      Note that the result is one-based.
//
REBINT Get_Num_Arg(REBVAL *val)
{
	REBINT n;

	if (IS_INTEGER(val)) {
		if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
			Trap_Range_DEAD_END(val);
		n = VAL_INT32(val);
	}
	else if (IS_DECIMAL(val) || IS_PERCENT(val)) {
		if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
			Trap_Range_DEAD_END(val);
		n = (REBINT)VAL_DECIMAL(val);
	}
	else if (IS_LOGIC(val)) n = (VAL_LOGIC(val) ? 1 : 2);
	else Trap_Arg_DEAD_END(val);

	return n;
}


//
//  Float_Int16: C
//
REBINT Float_Int16(REBD32 f)
{
	if (fabs(f) > (REBD32)(0x7FFF)) {
		DS_PUSH_DECIMAL(f);
		Trap_Range_DEAD_END(DS_TOP);
	}
	return (REBINT)f;
}


//
//  Int32: C
//
REBINT Int32(const REBVAL *val)
{
	REBINT n = 0;

	if (IS_DECIMAL(val)) {
		if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
			Trap_Range_DEAD_END(val);
		n = (REBINT)VAL_DECIMAL(val);
	} else {
		if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
			Trap_Range_DEAD_END(val);
		n = VAL_INT32(val);
	}

	return n;
}


//
//  Int32s: C
//  
//      Get integer as positive, negative 32 bit value.
//      Sign field can be
//          0: >= 0
//          1: >  0
//         -1: <  0
//
REBINT Int32s(const REBVAL *val, REBINT sign)
{
	REBINT n = 0;

	if (IS_DECIMAL(val)) {
		if (VAL_DECIMAL(val) > MAX_I32 || VAL_DECIMAL(val) < MIN_I32)
			Trap_Range_DEAD_END(val);

		n = (REBINT)VAL_DECIMAL(val);
	} else {
		if (VAL_INT64(val) > (i64)MAX_I32 || VAL_INT64(val) < (i64)MIN_I32)
			Trap_Range_DEAD_END(val);

		n = VAL_INT32(val);
	}

	// More efficient to use positive sense:
	if (
		(sign == 0 && n >= 0) ||
		(sign >  0 && n >  0) ||
		(sign <  0 && n <  0)
	)
		return n;

	Trap_Range_DEAD_END(val);
	return 0;
}


//
//  Int64: C
//
REBI64 Int64(const REBVAL *val)
{
	if (IS_INTEGER(val)) return VAL_INT64(val);
	if (IS_DECIMAL(val) || IS_PERCENT(val)) return (REBI64)VAL_DECIMAL(val);
	if (IS_MONEY(val)) return deci_to_int(VAL_MONEY_AMOUNT(val));
	Trap_Arg_DEAD_END(val);
	return 0;
}


//
//  Dec64: C
//
REBDEC Dec64(const REBVAL *val)
{
	if (IS_DECIMAL(val) || IS_PERCENT(val)) return VAL_DECIMAL(val);
	if (IS_INTEGER(val)) return (REBDEC)VAL_INT64(val);
	if (IS_MONEY(val)) return deci_to_decimal(VAL_MONEY_AMOUNT(val));
	Trap_Arg_DEAD_END(val);
	return 0;
}


//
//  Int64s: C
//  
//      Get integer as positive, negative 64 bit value.
//      Sign field can be
//          0: >= 0
//          1: >  0
//         -1: <  0
//
REBI64 Int64s(const REBVAL *val, REBINT sign)
{
	REBI64 n;

	if (IS_DECIMAL(val)) {
		if (VAL_DECIMAL(val) > MAX_I64 || VAL_DECIMAL(val) < MIN_I64)
			Trap_Range_DEAD_END(val);
		n = (REBI64)VAL_DECIMAL(val);
	} else {
		n = VAL_INT64(val);
	}

	// More efficient to use positive sense:
	if (
		(sign == 0 && n >= 0) ||
		(sign >  0 && n >  0) ||
		(sign <  0 && n <  0)
	)
		return n;

	Trap_Range_DEAD_END(val);
	DEAD_END;
}


//
//  Int8u: C
//
REBINT Int8u(const REBVAL *val)
{
	if (VAL_INT64(val) > (i64)255 || VAL_INT64(val) < (i64)0) Trap_Range_DEAD_END(val);
	return VAL_INT32(val);
}


//
//  Find_Refines: C
//  
//      Scans the stack for function refinements that have been
//      specified in the mask (each as a bit) and are being used.
//
REBCNT Find_Refines(struct Reb_Call *call_, REBCNT mask)
{
	REBINT n;
	REBCNT result = 0;

	REBINT max = DSF_NUM_ARGS(call_);

	for (n = 0; n < max; n++) {
		if ((mask & (1 << n) && D_REF(n + 1)))
			result |= 1 << n;
	}
	return result;
}


//
//  Set_Datatype: C
//
void Set_Datatype(REBVAL *value, REBINT n)
{
	*value = *BLK_SKIP(Lib_Context, n+1);
}


//
//  Get_Type: C
//  
//      Returns the specified datatype value from the system context.
//      The datatypes are all at the head of the context.
//
REBVAL *Get_Type(REBCNT index)
{
	assert(index < SERIES_TAIL(Lib_Context));
	return FRM_VALUES(Lib_Context) + index + 1;
}


//
//  Of_Type: C
//  
//      Returns the datatype value for the given value.
//      The datatypes are all at the head of the context.
//
REBVAL *Of_Type(const REBVAL *value)
{
	return FRM_VALUES(Lib_Context) + VAL_TYPE(value) + 1;
}


//
//  Get_Type_Sym: C
//  
//      Returns the datatype word for the given type number.
//
REBINT Get_Type_Sym(REBCNT type)
{
	return FRM_WORD_SYM(Lib_Context, type + 1);
}


//
//  Get_Type_Word: C
//  
//      Returns the datatype word for the given type number.
//
REBVAL *Get_Type_Word(REBCNT type)
{
	return FRM_WORD(Lib_Context, type + 1);
}


//
//  Get_Field_Name: C
//  
//      Get the name of a field of an object.
//
const REBYTE *Get_Field_Name(REBSER *obj, REBCNT index)
{
	assert(index < SERIES_TAIL(obj));
	return Get_Sym_Name(FRM_WORD_SYM(obj, index));
}


//
//  Get_Field: C
//  
//      Get an instance variable from an object series.
//
REBVAL *Get_Field(REBSER *obj, REBCNT index)
{
	assert(index < SERIES_TAIL(obj));
	return FRM_VALUES(obj) + index;
}


//
//  Get_Object: C
//  
//      Get an instance variable from an object value.
//
REBVAL *Get_Object(const REBVAL *objval, REBCNT index)
{
	REBSER *obj = VAL_OBJ_FRAME(objval);
	assert(IS_FRAME(BLK_HEAD(obj)));
	assert(index < SERIES_TAIL(obj));
	return FRM_VALUES(obj) + index;
}


//
//  In_Object: C
//  
//      Get value from nested list of objects. List is null terminated.
//      Returns object value, else returns 0 if not found.
//
REBVAL *In_Object(REBSER *base, ...)
{
	REBVAL *obj = 0;
	REBCNT n;
	va_list args;

	va_start(args, base);
	while ((n = va_arg(args, REBCNT))) {
		if (n >= SERIES_TAIL(base)) {
			va_end(args);
			return 0;
		}
		obj = OFV(base, n);
		if (!IS_OBJECT(obj)) {
			va_end(args);
			return 0;
		}
		base = VAL_OBJ_FRAME(obj);
	}
	va_end(args);

	return obj;
}


//
//  Get_System: C
//  
//      Return a second level object field of the system object.
//
REBVAL *Get_System(REBCNT i1, REBCNT i2)
{
	REBVAL *obj;

	obj = VAL_OBJ_VALUES(ROOT_SYSTEM) + i1;
	if (!i2) return obj;
	assert(IS_OBJECT(obj));
	return Get_Field(VAL_OBJ_FRAME(obj), i2);
}


//
//  Get_System_Int: C
//  
//      Get an integer from system object.
//
REBINT Get_System_Int(REBCNT i1, REBCNT i2, REBINT default_int)
{
	REBVAL *val = Get_System(i1, i2);
	if (IS_INTEGER(val)) return VAL_INT32(val);
	return default_int;
}


//
//  Make_Std_Object: C
//
REBSER *Make_Std_Object(REBCNT index)
{
	return Copy_Array_Shallow(
		VAL_OBJ_FRAME(Get_System(SYS_STANDARD, index))
	);
}


//
//  Set_Object_Values: C
//
void Set_Object_Values(REBSER *obj, REBVAL *vals)
{
	REBVAL *value;

	for (value = FRM_VALUES(obj) + 1; NOT_END(value); value++) { // skip self
		if (IS_END(vals)) SET_NONE(value);
		else *value = *vals++;
	}
}


//
//  Val_Init_Series_Index_Core: C
//  
//      Common function.
//
void Val_Init_Series_Index_Core(REBVAL *value, enum Reb_Kind type, REBSER *series, REBCNT index)
{
	ENSURE_SERIES_MANAGED(series);

	VAL_SET(value, type);
	VAL_SERIES(value) = series;
	VAL_INDEX(value) = index;
}


//
//  Set_Tuple: C
//
void Set_Tuple(REBVAL *value, REBYTE *bytes, REBCNT len)
{
	REBYTE *bp;

	VAL_SET(value, REB_TUPLE);
	VAL_TUPLE_LEN(value) = (REBYTE)len;
	for (bp = VAL_TUPLE(value); len > 0; len--)
		*bp++ = *bytes++;
}


//
//  Val_Init_Object: C
//
void Val_Init_Object(REBVAL *value, REBSER *series)
{
	ENSURE_FRAME_MANAGED(series);

	VAL_SET(value, REB_OBJECT);
	VAL_OBJ_FRAME(value) = series;
}


//
//  Val_Series_Len: C
//  
//      Get length of series, but avoid negative values.
//
REBCNT Val_Series_Len(const REBVAL *value)
{
	if (VAL_INDEX(value) >= VAL_TAIL(value)) return 0;
	return VAL_TAIL(value) - VAL_INDEX(value);
}


//
//  Val_Byte_Len: C
//  
//      Get length of series in bytes.
//
REBCNT Val_Byte_Len(const REBVAL *value)
{
	if (VAL_INDEX(value) >= VAL_TAIL(value)) return 0;
	return (VAL_TAIL(value) - VAL_INDEX(value)) * SERIES_WIDE(VAL_SERIES(value));
}


//
//  Get_Logic_Arg: C
//
REBFLG Get_Logic_Arg(REBVAL *arg)
{
	if (IS_NONE(arg)) return 0;
	if (IS_INTEGER(arg)) return (VAL_INT64(arg) != 0);
	if (IS_LOGIC(arg)) return (VAL_LOGIC(arg) != 0);
	if (IS_DECIMAL(arg) || IS_PERCENT(arg)) return (VAL_DECIMAL(arg) != 0.0);
	Trap_Arg_DEAD_END(arg);
	DEAD_END;
}


//
//  Partial1: C
//  
//      Process the /part (or /skip) and other length modifying
//      arguments.
//
REBINT Partial1(REBVAL *sval, REBVAL *lval)
{
	REBI64 len;
	REBINT maxlen;
	REBINT is_ser = ANY_SERIES(sval);

	// If lval = NONE, use the current len of the target value:
	if (IS_NONE(lval)) {
		if (!is_ser) return 1;
		if (VAL_INDEX(sval) >= VAL_TAIL(sval)) return 0;
		return (VAL_TAIL(sval) - VAL_INDEX(sval));
	}
	if (IS_INTEGER(lval) || IS_DECIMAL(lval)) len = Int32(lval);
	else {
		if (is_ser && VAL_TYPE(sval) == VAL_TYPE(lval) && VAL_SERIES(sval) == VAL_SERIES(lval))
			len = (REBINT)VAL_INDEX(lval) - (REBINT)VAL_INDEX(sval);
		else
			Trap1_DEAD_END(RE_INVALID_PART, lval);

	}

	if (is_ser) {
		// Restrict length to the size available:
		if (len >= 0) {
			maxlen = (REBINT)VAL_LEN(sval);
			if (len > maxlen) len = maxlen;
		} else {
			len = -len;
			if (len > (REBINT)VAL_INDEX(sval)) len = (REBINT)VAL_INDEX(sval);
			VAL_INDEX(sval) -= (REBCNT)len;
		}
	}

	return (REBINT)len;
}


//
//  Partial: C
//  
//      Args:
//          aval: target value
//          bval: argument to modify target (optional)
//          lval: length value (or none)
//  
//      Determine the length of a /PART value. It can be:
//          1. integer or decimal
//          2. relative to A value (bval is null)
//          3. relative to B value
//  
//      Flag: indicates special treatment for CHANGE. As in:
//          CHANGE/part "abcde" "xy" 3 => "xyde"
//  
//      NOTE: Can modify the value's index!
//      The result can be negative. ???
//
REBINT Partial(REBVAL *aval, REBVAL *bval, REBVAL *lval, REBFLG flag)
{
	REBVAL *val;
	REBINT len;
	REBINT maxlen;

	// If lval = NONE, use the current len of the target value:
	if (IS_NONE(lval)) {
		val = (bval && ANY_SERIES(bval)) ? bval : aval;
		if (VAL_INDEX(val) >= VAL_TAIL(val)) return 0;
		return (VAL_TAIL(val) - VAL_INDEX(val));
	}

	if (IS_INTEGER(lval)) {
		len = Int32(lval);
		val = flag ? aval : bval;
	}

	else if (IS_DECIMAL(lval)) {
		len = Int32(lval);
		val = bval;
	}

	else {
		// So, lval must be relative to aval or bval series:
		if (VAL_TYPE(aval) == VAL_TYPE(lval) && VAL_SERIES(aval) == VAL_SERIES(lval))
			val = aval;
		else if (bval && VAL_TYPE(bval) == VAL_TYPE(lval) && VAL_SERIES(bval) == VAL_SERIES(lval))
			val = bval;
		else
			Trap1_DEAD_END(RE_INVALID_PART, lval);

		len = (REBINT)VAL_INDEX(lval) - (REBINT)VAL_INDEX(val);
	}

	if (!val) val = aval;

	// Restrict length to the size available:
	if (len >= 0) {
		maxlen = (REBINT)VAL_LEN(val);
		if (len > maxlen) len = maxlen;
	} else {
		len = -len;
		if (len > (REBINT)VAL_INDEX(val)) len = (REBINT)VAL_INDEX(val);
		VAL_INDEX(val) -= (REBCNT)len;
//		if ((-len) > (REBINT)VAL_INDEX(val)) len = -(REBINT)VAL_INDEX(val);
	}

	return len;
}


//
//  Clip_Int: C
//
int Clip_Int(int val, int mini, int maxi)
{
	if (val < mini) val = mini;
	else if (val > maxi) val = maxi;
	return val;
}

//
//  memswapl: C
//  
//      For long integer memory units, not chars. It is assumed that
//      the len is an exact modulo of long.
//
void memswapl(void *m1, void *m2, size_t len)
{
	long t, *a, *b;

	a = cast(long*, m1);
	b = cast(long*, m2);
	len /= sizeof(long);
	while (len--) {
		t = *b;
		*b++ = *a;
		*a++ = t;
	}
}


//
//  Add_Max: C
//
i64 Add_Max(int type, i64 n, i64 m, i64 maxi)
{
	i64 r = n + m;
	if (r < -maxi || r > maxi) {
		if (type) Trap1_DEAD_END(RE_TYPE_LIMIT, Get_Type(type));
		r = r > 0 ? maxi : -maxi;
	}
	return r;
}


//
//  Mul_Max: C
//
int Mul_Max(int type, i64 n, i64 m, i64 maxi)
{
	i64 r = n * m;
	if (r < -maxi || r > maxi) Trap1_DEAD_END(RE_TYPE_LIMIT, Get_Type(type));
	return (int)r;
}


//
//  Make_OS_Error: C
//
void Make_OS_Error(REBVAL *out, int errnum)
{
	REBCHR str[100];

	OS_FORM_ERROR(errnum, str, 100);
	Val_Init_String(out, Copy_OS_Str(str, OS_STRLEN(str)));
}


//
//  At_Head: C
//  
//      Return the series for a value, but if it has an index
//      offset, return a copy of the series from that position.
//      Useful for functions that do not accept index offsets.
//
REBSER *At_Head(REBVAL *value)
{
	REBCNT len;
	REBSER *ser;
	REBSER *src = VAL_SERIES(value);
	REBYTE wide;

	if (VAL_INDEX(value) == 0) return src;

	len = VAL_LEN(value);
	wide = SERIES_WIDE(src);
	ser = Make_Series(len, wide, Is_Array_Series(src) ? MKS_ARRAY : MKS_NONE);

	memcpy(ser->data, src->data + (VAL_INDEX(value) * wide), len * wide);
	ser->tail = len;

	return ser;
}


//
//  Collect_Set_Words: C
//  
//      Scan a block, collecting all of its SET words as a block.
//
REBSER *Collect_Set_Words(REBVAL *val)
{
	REBCNT cnt = 0;
	REBVAL *val2 = val;
	REBSER *ser;

	for (; NOT_END(val); val++) if (IS_SET_WORD(val)) cnt++;
	val = val2;

	ser = Make_Array(cnt);
	val2 = BLK_HEAD(ser);
	for (; NOT_END(val); val++) {
		if (IS_SET_WORD(val))
			Val_Init_Word_Unbound(val2++, REB_WORD, VAL_WORD_SYM(val));
	}
	SET_END(val2);
	SERIES_TAIL(ser) = cnt;

	return ser;
}


//
//  What_Reflector: C
//
REBINT What_Reflector(REBVAL *word)
{
	if (IS_WORD(word)) {
		switch (VAL_WORD_SYM(word)) {
		case SYM_SPEC:   return OF_SPEC;
		case SYM_BODY:   return OF_BODY;
		case SYM_WORDS:  return OF_WORDS;
		case SYM_VALUES: return OF_VALUES;
		case SYM_TYPES:  return OF_TYPES;
		case SYM_TITLE:  return OF_TITLE;
		}
	}
	return 0;
}
