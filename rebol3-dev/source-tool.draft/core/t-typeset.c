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
**  Module:  t-typeset.c
**  Summary: typeset datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	const REBU64 Typesets[] =
/*
**		Order of symbols is important- used below for Make_Typeset().
**
************************************************************************/
{
	1, 0, // First (0th) typeset is not valid
	SYM_ANY_TYPEX,     ((REBU64)1<<REB_MAX)-2, // do not include END!
	SYM_ANY_WORDX,     TS_WORD,
	SYM_ANY_PATHX,     TS_PATH,
	SYM_ANY_FUNCTIONX, TS_FUNCTION,
	SYM_NUMBERX,       TS_NUMBER,
	SYM_SCALARX,       TS_SCALAR,
	SYM_SERIESX,       TS_SERIES,
	SYM_ANY_STRINGX,   TS_STRING,
	SYM_ANY_OBJECTX,   TS_OBJECT,
	SYM_ANY_ARRAYX,    TS_ARRAY,
	0, 0
};


//
//  CT_Typeset: C
//

REBINT CT_Typeset(REBVAL *a, REBVAL *b, REBINT mode)
{
	if (mode < 0) return -1;
	return EQUAL_TYPESET(a, b);
}


//
//  Init_Typesets: C
//  
//      Create typeset variables that are defined above.
//      For example: NUMBER is both integer and decimal.
//      Add the new variables to the system context.
//

void Init_Typesets(void)
{
	REBVAL *value;
	REBINT n;

	Set_Root_Series(ROOT_TYPESETS, Make_Array(40), "typeset presets");

	for (n = 0; Typesets[n]; n += 2) {
		value = Alloc_Tail_Array(VAL_SERIES(ROOT_TYPESETS));
		VAL_SET(value, REB_TYPESET);
		VAL_TYPESET(value) = Typesets[n+1];
		if (Typesets[n] > 1)
			*Append_Frame(Lib_Context, 0, (REBCNT)(Typesets[n])) = *value;
	}
}


//
//  Make_Typeset: C
//  
//      block - block of datatypes (datatype words ok too)
//      value - value to hold result (can be word-spec type too)
//

REBFLG Make_Typeset(REBVAL *block, REBVAL *value, REBFLG load)
{
	const REBVAL *val;
	REBCNT sym;
	REBSER *types = VAL_SERIES(ROOT_TYPESETS);

	VAL_TYPESET(value) = 0;

	for (; NOT_END(block); block++) {
		val = NULL;
		if (IS_WORD(block)) {
			//Print("word: %s", Get_Word_Name(block));
			sym = VAL_WORD_SYM(block);
			if (VAL_WORD_FRAME(block)) { // Get word value
				val = GET_VAR(block);
			} else if (sym < REB_MAX) { // Accept datatype word
				TYPE_SET(value, VAL_WORD_SYM(block)-1);
				continue;
			} // Special typeset symbols:
			else if (sym >= SYM_ANY_TYPEX && sym < SYM_DATATYPES)
				val = BLK_SKIP(types, sym - SYM_ANY_TYPEX + 1);
		}
		if (!val) val = block;
		if (IS_DATATYPE(val)) {
			TYPE_SET(value, VAL_TYPE_KIND(val));
		} else if (IS_TYPESET(val)) {
			VAL_TYPESET(value) |= VAL_TYPESET(val);
		} else {
			if (load) return FALSE;
			Trap_Arg_DEAD_END(block);
		}
	}

	return TRUE;
}


//
//  MT_Typeset: C
//

REBFLG MT_Typeset(REBVAL *out, REBVAL *data, REBCNT type)
{
	if (!IS_BLOCK(data)) return FALSE;

	if (!Make_Typeset(VAL_BLK_HEAD(data), out, TRUE)) return FALSE;
	VAL_SET(out, REB_TYPESET);

	return TRUE;
}


//
//  Find_Typeset: C
//

REBINT Find_Typeset(REBVAL *block)
{
	REBVAL value;
	REBVAL *val;
	REBINT n;

	VAL_SET(&value, REB_TYPESET);
	Make_Typeset(block, &value, 0);

	val = VAL_BLK_SKIP(ROOT_TYPESETS, 1);

	for (n = 1; NOT_END(val); val++, n++) {
		if (EQUAL_TYPESET(&value, val)){
			//Print("FTS: %d", n);
			return n;
		}
	}

//	Print("Size Typesets: %d", VAL_TAIL(ROOT_TYPESETS));
	Append_Value(VAL_SERIES(ROOT_TYPESETS), &value);
	return n;
}


//
//  Typeset_To_Block: C
//  
//      Converts typeset value to a block of datatypes.
//      No order is specified.
//

REBSER *Typeset_To_Block(REBVAL *tset)
{
	REBSER *block;
	REBVAL *value;
	REBINT n;
	REBINT size = 0;

	for (n = 0; n < REB_MAX; n++) {
		if (TYPE_CHECK(tset, n)) size++;
	}

	block = Make_Array(size);

	// Convert bits to types:
	for (n = 0; n < REB_MAX; n++) {
		if (TYPE_CHECK(tset, n)) {
			value = Alloc_Tail_Array(block);
			Set_Datatype(value, n);
		}
	}
	return block;
}


//
//  REBTYPE: C
//

REBTYPE(Typeset)
{
	REBVAL *val = D_ARG(1);
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

	switch (action) {

	case A_FIND:
		if (IS_DATATYPE(arg)) {
			DECIDE(TYPE_CHECK(val, VAL_TYPE_KIND(arg)));
		}
		Trap_Arg_DEAD_END(arg);

	case A_MAKE:
	case A_TO:
		if (IS_BLOCK(arg)) {
			VAL_SET(D_OUT, REB_TYPESET);
			Make_Typeset(VAL_BLK_DATA(arg), D_OUT, 0);
			return R_OUT;
		}
	//	if (IS_NONE(arg)) {
	//		VAL_SET(arg, REB_TYPESET);
	//		VAL_TYPESET(arg) = 0L;
	//		return R_ARG2;
	//	}
		if (IS_TYPESET(arg)) return R_ARG2;
		Trap_Make_DEAD_END(REB_TYPESET, arg);

	case A_AND:
	case A_OR:
	case A_XOR:
		if (IS_DATATYPE(arg)) VAL_TYPESET(arg) = TYPESET(VAL_TYPE_KIND(arg));
		else if (!IS_TYPESET(arg)) Trap_Arg_DEAD_END(arg);

		if (action == A_OR) VAL_TYPESET(val) |= VAL_TYPESET(arg);
		else if (action == A_AND) VAL_TYPESET(val) &= VAL_TYPESET(arg);
		else VAL_TYPESET(val) ^= VAL_TYPESET(arg);
		return R_ARG1;

	case A_COMPLEMENT:
		VAL_TYPESET(val) = ~VAL_TYPESET(val);
		return R_ARG1;

	default:
		Trap_Action_DEAD_END(REB_TYPESET, action);
	}

is_true:
	return R_TRUE;

is_false:
	return R_FALSE;
}
