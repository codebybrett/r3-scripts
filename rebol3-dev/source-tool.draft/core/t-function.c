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
**  Module:  t-function.c
**  Summary: function related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

static REBOOL Same_Func(REBVAL *val, REBVAL *arg)
{
	if (VAL_TYPE(val) == VAL_TYPE(arg) &&
		VAL_FUNC_SPEC(val) == VAL_FUNC_SPEC(arg) &&
		VAL_FUNC_WORDS(val) == VAL_FUNC_WORDS(arg) &&
		VAL_FUNC_CODE(val) == VAL_FUNC_CODE(arg)) return TRUE;
	return FALSE;
}


//
//  CT_Function: C
//
REBINT CT_Function(REBVAL *a, REBVAL *b, REBINT mode)
{
	if (mode >= 0) return Same_Func(a, b);
	return -1;
}


//
//  As_Typesets: C
//
REBSER *As_Typesets(REBSER *types)
{
	REBVAL *val;

	types = Copy_Array_At_Shallow(types, 1);
	for (val = BLK_HEAD(types); NOT_END(val); val++) {
		SET_TYPE(val, REB_TYPESET);
	}
	return types;
}


//
//  MT_Function: C
//
REBFLG MT_Function(REBVAL *out, REBVAL *data, REBCNT type)
{
	return Make_Function(type, out, data);
}


//
//  REBTYPE: C
//
REBTYPE(Function)
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = D_ARG(2);
	REBCNT type = VAL_TYPE(value);
	REBCNT n;

	switch (action) {

	case A_MAKE:
	case A_TO:
		// make function! [[args] [body]]
		if (IS_DATATYPE(value)) {
			n = VAL_TYPE_KIND(value);
			if (Make_Function(n, value, arg)) break;
			Trap_Make_DEAD_END(n, arg);
		}

		// make :func []
		// make :func [[args]]
		// make :func [* [body]]
		if (ANY_FUNC(value)) {
			if (!IS_BLOCK(arg)) goto bad_arg;
			if (!ANY_FUNC(value)) goto bad_arg;
			if (!Copy_Function(value, arg)) goto bad_arg;
			break;
		}
		if (!IS_NONE(arg)) goto bad_arg;
		// fall thru...
	case A_COPY:
		Copy_Function(value, 0);
		break;

	case A_REFLECT:
		n = What_Reflector(arg); // zero on error
		switch (n) {
		case OF_WORDS:
			Val_Init_Block(value, List_Func_Words(value));
			break;
		case OF_BODY:
			switch (type) {
			case REB_FUNCTION:
				Val_Init_Block(
					D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_BODY(value))
				);
				// See CC#2221 for why function body copies don't unbind locals
				return R_OUT;

			case REB_CLOSURE:
				Val_Init_Block(
					D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_BODY(value))
				);
				// See CC#2221 for why closure body copies have locals unbound
				Unbind_Values_Core(
					VAL_BLK_HEAD(D_OUT), VAL_FUNC_WORDS(value), TRUE
				);
				return R_OUT;

			case REB_NATIVE:
			case REB_COMMAND:
			case REB_ACTION:
				SET_NONE(value);
				break;
			}
			break;
		case OF_SPEC:
			Val_Init_Block(
				value, Copy_Array_Deep_Managed(VAL_FUNC_SPEC(value))
			);
			Unbind_Values_Deep(VAL_BLK_HEAD(value));
			break;
		case OF_TYPES:
			Val_Init_Block(value, As_Typesets(VAL_FUNC_WORDS(value)));
			break;
		case OF_TITLE:
			arg = BLK_HEAD(VAL_FUNC_SPEC(value));
			for (; NOT_END(arg) && !IS_STRING(arg) && !IS_WORD(arg); arg++);
			if (!IS_STRING(arg)) return R_NONE;
			Val_Init_String(value, Copy_Sequence(VAL_SERIES(arg)));
			break;
		default:
		bad_arg:
			Trap_Reflect_DEAD_END(type, arg);
		}
		break;

	default: Trap_Action_DEAD_END(type, action);
	}

	*D_OUT = *value;
	return R_OUT;
}
