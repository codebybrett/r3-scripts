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
**  Module:  t-money.c
**  Summary: extended precision datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-deci-funcs.h"


/*******************************************************************************
**
**  Name: "CT_Money"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT CT_Money(REBVAL *a, REBVAL *b, REBINT mode)
{
	REBFLG e, g;

	if (mode >= 3) e = deci_is_same(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));
	else {
		e = deci_is_equal(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));
		g = 0;
		if (mode < 0) {
			g = deci_is_lesser_or_equal(
				VAL_MONEY_AMOUNT(b), VAL_MONEY_AMOUNT(a)
			);
			if (mode == -1) e |= g;
			else e = g & !e;
		}
	}
	return e != 0;;
}


/*******************************************************************************
**
**  Name: "Emit_Money"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Emit_Money(const REBVAL *value, REBYTE *buf, REBCNT opts)
{
	return deci_to_string(buf, VAL_MONEY_AMOUNT(value), '$', '.');
}


/*******************************************************************************
**
**  Name: "Bin_To_Money"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Bin_To_Money(REBVAL *result, REBVAL *val)
{
	REBCNT len;
	REBYTE buf[MAX_HEX_LEN+4] = {0}; // binary to convert

	if (IS_BINARY(val)) {
		len = VAL_LEN(val);
		if (len > 12) len = 12;
		memcpy(buf, VAL_BIN_DATA(val), len);
	}
#ifdef removed
	else if (IS_ISSUE(val)) {
		//if (!(len = Scan_Hex_Bytes(val, 24, buf))) return FALSE;
		REBYTE *ap = Get_Word_Name(val);
		REBYTE *bp = &buf[0];
		REBCNT alen;
		REBUNI c;
		len = LEN_BYTES(ap);  // UTF-8 len
		if (len & 1) return FALSE; // must have even # of chars
		len /= 2;
		if (len > 12) return FALSE; // valid even for UTF-8
		for (alen = 0; alen < len; alen++) {
			if (!Scan_Hex2(ap, &c, 0)) return FALSE;
			*bp++ = (REBYTE)c;
			ap += 2;
		}
	}
#endif
	else {
		Trap_Arg_DEAD_END(val);
	}

	memcpy(buf + 12 - len, buf, len); // shift to right side
	memset(buf, 0, 12 - len);
	VAL_MONEY_AMOUNT(result) = binary_to_deci(buf);
	return TRUE;
}


/*******************************************************************************
**
**  Name: "REBTYPE"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBTYPE(Money)
{
	REBVAL *val = D_ARG(1);
	REBVAL *arg;
	const REBYTE *str;
	REBINT equal = 1;

	if (IS_BINARY_ACT(action)) {
		arg = D_ARG(2);

		if (IS_MONEY(arg))
			;
		else if (IS_INTEGER(arg)) {
			VAL_MONEY_AMOUNT(D_OUT) = int_to_deci(VAL_INT64(arg));
			arg = D_OUT;
		}
		else if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
			VAL_MONEY_AMOUNT(D_OUT) = decimal_to_deci(VAL_DECIMAL(arg));
			arg = D_OUT;
		}
		else Trap_Math_Args(REB_MONEY, action);

		switch (action) {
		case A_ADD:
			VAL_MONEY_AMOUNT(D_OUT) = deci_add(
				VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
			);
			break;

		case A_SUBTRACT:
			VAL_MONEY_AMOUNT(D_OUT) = deci_subtract(
				VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
			);
			break;

		case A_MULTIPLY:
			VAL_MONEY_AMOUNT(D_OUT) = deci_multiply(
				VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
			);
			break;

		case A_DIVIDE:
			VAL_MONEY_AMOUNT(D_OUT) = deci_divide(
				VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
			);
			break;

		case A_REMAINDER:
			VAL_MONEY_AMOUNT(D_OUT) = deci_mod(
				VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
			);
			break;

		default:
			Trap_Action_DEAD_END(REB_MONEY, action);
		}

		SET_TYPE(D_OUT, REB_MONEY);
		return R_OUT;
	}

	switch(action) {
	case A_NEGATE:
		VAL_MONEY_AMOUNT(val).s = !VAL_MONEY_AMOUNT(val).s;
		return R_ARG1;

	case A_ABSOLUTE:
		VAL_MONEY_AMOUNT(val).s = 0;
		return R_ARG1;

	case A_ROUND:
		arg = D_ARG(3);
		if (D_REF(2)) {
			if (IS_INTEGER(arg))
				VAL_MONEY_AMOUNT(arg) = int_to_deci(VAL_INT64(arg));
			else if (IS_DECIMAL(arg) || IS_PERCENT(arg))
				VAL_MONEY_AMOUNT(arg) = decimal_to_deci(VAL_DECIMAL(arg));
			else if (!IS_MONEY(arg)) Trap_Arg_DEAD_END(arg);
		}
		VAL_MONEY_AMOUNT(D_OUT) = Round_Deci(
			VAL_MONEY_AMOUNT(val),
			Get_Round_Flags(call_),
			VAL_MONEY_AMOUNT(arg)
		);
		if (D_REF(2)) {
			if (IS_DECIMAL(arg) || IS_PERCENT(arg)) {
				VAL_DECIMAL(D_OUT) = deci_to_decimal(VAL_MONEY_AMOUNT(D_OUT));
				SET_TYPE(D_OUT, VAL_TYPE(arg));
				return R_OUT;
			}
			if (IS_INTEGER(arg)) {
				VAL_INT64(D_OUT) = deci_to_int(VAL_MONEY_AMOUNT(D_OUT));;
				SET_TYPE(D_OUT, REB_INTEGER);
				return R_OUT;
			}
		}
		break;

	case A_EVENQ:
	case A_ODDQ:
		equal = 1 & (REBINT)deci_to_int(VAL_MONEY_AMOUNT(val));
		if (action == A_EVENQ) equal = !equal;
		if (equal) goto is_true;
		goto is_false;

	case A_MAKE:
	case A_TO:
		arg = D_ARG(2);

		switch (VAL_TYPE(arg)) {

		case REB_INTEGER:
			VAL_MONEY_AMOUNT(D_OUT) = int_to_deci(VAL_INT64(arg));
			break;

		case REB_DECIMAL:
		case REB_PERCENT:
			VAL_MONEY_AMOUNT(D_OUT) = decimal_to_deci(VAL_DECIMAL(arg));
			break;

		case REB_MONEY:
			return R_ARG2;

		case REB_STRING:
		{
			const REBYTE *end;
			str = Qualify_String(arg, 36, 0, FALSE);
			VAL_MONEY_AMOUNT(D_OUT) = string_to_deci(str, &end);
			if (end == str || *end != 0) Trap_Make_DEAD_END(REB_MONEY, arg);
			break;
		}

//		case REB_ISSUE:
		case REB_BINARY:
			if (!Bin_To_Money(D_OUT, arg)) goto err;
			break;

		case REB_LOGIC:
			equal = !VAL_LOGIC(arg);
//		case REB_NONE: // 'equal defaults to 1
			VAL_MONEY_AMOUNT(D_OUT) = int_to_deci(equal ? 0 : 1);
			break;

		default:
		err:
			Trap_Make_DEAD_END(REB_MONEY, arg);
		}
		break;

	default:
		Trap_Action_DEAD_END(REB_MONEY, action);
	}

	SET_TYPE(D_OUT, REB_MONEY);
	return R_OUT;

is_true:  return R_TRUE;
is_false: return R_FALSE;
}

