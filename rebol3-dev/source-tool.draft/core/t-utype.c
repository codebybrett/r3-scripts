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
**  Module:  t-utype.c
**  Summary: user defined datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:   NOT IMPLEMENTED
**
***********************************************************************/

#include "sys-core.h"

#define	SET_UTYPE(v,f) VAL_UTYPE_FUNC(v) = (f), VAL_UTYPE_DATA(v) = 0, VAL_SET(v, REB_UTYPE)


//
//  CT_Utype: C
//

REBINT CT_Utype(REBVAL *a, REBVAL *b, REBINT mode)
{
	return FALSE;
}


//
//  MT_Utype: C
//

REBFLG MT_Utype(REBVAL *out, REBVAL *data, REBCNT type)
{
	return FALSE;
}


//
//  REBTYPE: C
//

REBTYPE(Utype)
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = D_ARG(2);
	REBVAL *spec;
	REBVAL *body;

	if (action == A_MAKE) {
		// MAKE udef! [spec body]
		if (IS_DATATYPE(value)) {
			if (!IS_BLOCK(arg)) Trap_Arg_DEAD_END(arg);
			spec = VAL_BLK_HEAD(arg);
			if (!IS_BLOCK(spec)) Trap_Arg_DEAD_END(arg);
			body = VAL_BLK_SKIP(arg, 1);
			if (!IS_BLOCK(body)) Trap_Arg_DEAD_END(arg);

			spec = Get_System(SYS_STANDARD, STD_UTYPE);
			if (!IS_OBJECT(spec)) Trap_Arg_DEAD_END(spec);
			SET_UTYPE(D_OUT, Make_Object(VAL_OBJ_FRAME(spec), body));
			VAL_UTYPE_DATA(D_OUT) = 0;
			return R_OUT;
		}
		else Trap_Arg_DEAD_END(arg);
	}

	if (!IS_UTYPE(value)) Trap1_DEAD_END(RE_INVALID_TYPE, Get_Type(REB_UTYPE));
//	if (!VAL_UTYPE_DATA(D_OUT) || SERIES_TAIL(VAL_UTYPE_FUNC(value)) <= action)
//		Trap_Action_DEAD_END(REB_UTYPE, action);

	body = OFV(VAL_UTYPE_FUNC(value), action);
	if (!IS_FUNCTION(body)) Trap_Action_DEAD_END(REB_UTYPE, action);

	Do_Function(body);

	return R_OUT;
}
