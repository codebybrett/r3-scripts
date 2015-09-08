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
**  Module:  t-object.c
**  Summary: object datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


static REBOOL Same_Object(REBVAL *val, REBVAL *arg)
{
	if (
		VAL_TYPE(arg) == VAL_TYPE(val) &&
		//VAL_OBJ_SPEC(val) == VAL_OBJ_SPEC(arg) &&
		VAL_OBJ_FRAME(val) == VAL_OBJ_FRAME(arg)
	) return TRUE;
	return FALSE;
}


static REBOOL Equal_Object(REBVAL *val, REBVAL *arg)
{
	REBSER *f1;
	REBSER *f2;
	REBSER *w1;
	REBSER *w2;
	REBINT n;

	if (VAL_TYPE(arg) != VAL_TYPE(val)) return FALSE;

	f1 = VAL_OBJ_FRAME(val);
	f2 = VAL_OBJ_FRAME(arg);
	if (f1 == f2) return TRUE;
	if (f1->tail != f2->tail) return FALSE;

	w1 = FRM_WORD_SERIES(f1);
	w2 = FRM_WORD_SERIES(f2);
	if (w1->tail != w2->tail) return FALSE;

	// Compare each entry:
	for (n = 1; n < (REBINT)(f1->tail); n++) {
		if (Cmp_Value(BLK_SKIP(w1, n), BLK_SKIP(w2, n), FALSE)) return FALSE;
		// !!! A comment here said "Use Compare_Modify_Values();"...but it
		// doesn't... it calls Cmp_Value (?)
		if (Cmp_Value(BLK_SKIP(f1, n), BLK_SKIP(f2, n), FALSE)) return FALSE;
	}

	return TRUE;
}

static void Append_Obj(REBSER *obj, REBVAL *arg)
{
	REBCNT i, len;
	REBVAL *word, *val;
	REBINT *binds; // for binding table

	// Can be a word:
	if (ANY_WORD(arg)) {
		if (!Find_Word_Index(obj, VAL_WORD_SYM(arg), TRUE)) {
			// bug fix, 'self is protected only in selfish frames
			if ((VAL_WORD_CANON(arg) == SYM_SELF) && !IS_SELFLESS(obj))
				Trap(RE_SELF_PROTECTED);
			Expand_Frame(obj, 1, 1); // copy word table also
			Append_Frame(obj, 0, VAL_WORD_SYM(arg));
			// val is UNSET
		}
		return;
	}

	if (!IS_BLOCK(arg)) Trap_Arg(arg);

	// Process word/value argument block:
	arg = VAL_BLK_DATA(arg);

	// Use binding table
	binds = WORDS_HEAD(Bind_Table);
	// Handle selfless
	Collect_Start(IS_SELFLESS(obj) ? BIND_NO_SELF | BIND_ALL : BIND_ALL);
	// Setup binding table with obj words:
	Collect_Object(obj);

	// Examine word/value argument block
	for (word = arg; NOT_END(word); word += 2) {

		if (!IS_WORD(word) && !IS_SET_WORD(word)) {
			// release binding table
			BLK_TERM(BUF_WORDS);
			Collect_End(obj);
			Trap_Arg(word);
		}

		if ((i = binds[VAL_WORD_CANON(word)])) {
			// bug fix, 'self is protected only in selfish frames:
			if ((VAL_WORD_CANON(word) == SYM_SELF) && !IS_SELFLESS(obj)) {
				// release binding table
				BLK_TERM(BUF_WORDS);
				Collect_End(obj);
				Trap(RE_SELF_PROTECTED);
			}
		} else {
			// collect the word
			binds[VAL_WORD_CANON(word)] = SERIES_TAIL(BUF_WORDS);
			EXPAND_SERIES_TAIL(BUF_WORDS, 1);
			val = BLK_LAST(BUF_WORDS);
			*val = *word;
		}
		if (IS_END(word + 1)) break; // fix bug#708
	}

	BLK_TERM(BUF_WORDS);

	// Append new words to obj
	len = SERIES_TAIL(obj);
	Expand_Frame(obj, SERIES_TAIL(BUF_WORDS) - len, 1);
	for (word = BLK_SKIP(BUF_WORDS, len); NOT_END(word); word++)
		Append_Frame(obj, 0, VAL_WORD_SYM(word));

	// Set new values to obj words
	for (word = arg; NOT_END(word); word += 2) {
		REBVAL *frame_word;

		i = binds[VAL_WORD_CANON(word)];
		val = FRM_VALUE(obj, i);
		frame_word = FRM_WORD(obj, i);

		if (
			VAL_GET_EXT(frame_word, EXT_WORD_HIDE)
			|| VAL_GET_EXT(frame_word, EXT_WORD_LOCK)
		) {
			// release binding table
			Collect_End(obj);
			if (VAL_GET_EXT(FRM_WORD(obj, i), EXT_WORD_LOCK))
				Trap1(RE_LOCKED_WORD, FRM_WORD(obj, i));
			Trap(RE_HIDDEN);
		}

		if (IS_END(word + 1)) SET_NONE(val);
		else *val = word[1];

		if (IS_END(word + 1)) break; // fix bug#708
	}

	// release binding table
	Collect_End(obj);
}

static REBSER *Trim_Object(REBSER *obj)
{
	REBVAL *val;
	REBINT cnt = 0;
	REBSER *nobj;
	REBVAL *nval;
	REBVAL *word;
	REBVAL *nwrd;

	word = FRM_WORDS(obj)+1;
	for (val = FRM_VALUES(obj)+1; NOT_END(val); val++, word++) {
		if (VAL_TYPE(val) > REB_NONE && !VAL_GET_EXT(word, EXT_WORD_HIDE))
			cnt++;
	}

	nobj = Make_Frame(cnt, TRUE);
	nval = FRM_VALUES(nobj)+1;
	word = FRM_WORDS(obj)+1;
	nwrd = FRM_WORDS(nobj)+1;
	for (val = FRM_VALUES(obj)+1; NOT_END(val); val++, word++) {
		if (VAL_TYPE(val) > REB_NONE && !VAL_GET_EXT(word, EXT_WORD_HIDE)) {
			*nval++ = *val;
			*nwrd++ = *word;
		}
	}
	SET_END(nval);
	SET_END(nwrd);
	SERIES_TAIL(nobj) = cnt+1;
	SERIES_TAIL(FRM_WORD_SERIES(nobj)) = cnt+1;

	return nobj;
}


//
//  CT_Object: C
//
REBINT CT_Object(REBVAL *a, REBVAL *b, REBINT mode)
{
	if (mode < 0) return -1;
	if (mode == 3) return Same_Object(a, b);
	return Equal_Object(a, b);
}


//
//  CT_Frame: C
//
REBINT CT_Frame(REBVAL *a, REBVAL *b, REBINT mode)
{
	if (mode < 0) return -1;
	return VAL_SERIES(a) == VAL_SERIES(b);
}



//
//  MT_Object: C
//
REBFLG MT_Object(REBVAL *out, REBVAL *data, REBCNT type)
{
	if (!IS_BLOCK(data)) return FALSE;
	VAL_OBJ_FRAME(out) = Construct_Object(NULL, VAL_BLK_DATA(data), FALSE);
	VAL_SET(out, type);
	if (type == REB_ERROR) {
		REBVAL result;
		if (!Make_Error_Object(&result, out)) {
			assert(THROWN(&result));
			*out = result;
			return FALSE;
		}
		assert(IS_ERROR(&result));
		*out = result;
	}
	return TRUE;
}


//
//  PD_Object: C
//
REBINT PD_Object(REBPVS *pvs)
{
	REBINT n = 0;

	if (!VAL_OBJ_FRAME(pvs->value)) {
		return PE_NONE; // Error objects may not have a frame.
	}

	if (IS_WORD(pvs->select)) {
		n = Find_Word_Index(VAL_OBJ_FRAME(pvs->value), VAL_WORD_SYM(pvs->select), FALSE);
	}
//	else if (IS_INTEGER(pvs->select)) {
//		n = Int32s(pvs->select, 1);
//	}
	else return PE_BAD_SELECT;

	if (n <= 0 || (REBCNT)n >= SERIES_TAIL(VAL_OBJ_FRAME(pvs->value)))
		return PE_BAD_SELECT;

	if (
		pvs->setval
		&& IS_END(pvs->path+1)
		&& VAL_GET_EXT(VAL_FRM_WORD(pvs->value, n), EXT_WORD_LOCK)
	) {
		Trap1_DEAD_END(RE_LOCKED_WORD, pvs->select);
	}

	pvs->value = VAL_OBJ_VALUES(pvs->value) + n;
	return PE_SET;
	// if setval, check PROTECT mode!!!
	// VAL_FLAGS((VAL_OBJ_VALUES(value) + n)) &= ~FLAGS_CLEAN;
}


//
//  REBTYPE: C
//  
//  Handles object! and error! datatypes.
//
REBTYPE(Object)
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;
	REBINT n;
	REBVAL *val;
	REBSER *obj, *src_obj;
	REBCNT type = 0;

	switch (action) {

	case A_MAKE:
		// make object! | error! | module! | task!
		if (IS_DATATYPE(value)) {

			type = VAL_TYPE_KIND(value); // target type

			if (IS_BLOCK(arg)) {

				// make object! [init]
				if (type == REB_OBJECT) {
					obj = Make_Object(0, VAL_BLK_DATA(arg));
					Val_Init_Object(D_OUT, obj); // GC save
					Bind_Values_Deep(VAL_BLK_DATA(arg), obj);

					// GC-OK
					if (Do_Block_Throws(D_OUT, VAL_SERIES(arg), 0))
						return R_OUT;

					break; // returns obj
				}

				if (type == REB_MODULE) {
					Make_Module(value, arg);
					type = 0;
				//	VAL_MOD_BODY(value) = VAL_SERIES(arg);
				//	VAL_SET(value, REB_MODULE); // GC protected
				//	DO_BLK(arg);
				//	DS_DROP;
					break; // returns value
				}

				// make task! [init]
				if (type == REB_TASK) {
					// Does it include a spec?
					if (IS_BLOCK(VAL_BLK_HEAD(arg))) {
						arg = VAL_BLK_HEAD(arg);
						if (!IS_BLOCK(arg+1)) Trap_Make_DEAD_END(REB_TASK, value);
						obj = Make_Module_Spec(arg);
						VAL_MOD_BODY(value) = VAL_SERIES(arg+1);
					} else {
						obj = Make_Module_Spec(0);
						VAL_MOD_BODY(value) = VAL_SERIES(arg);
					}
					break; // returns obj
				}
			}

			// make error! [....]
			if (type == REB_ERROR) {
				// arg is block/string, returns value
				if (!Make_Error_Object(value, arg)) {
					assert(THROWN(value));
					// going to return it anyway, no special handling needed
				}
				type = 0;
				break; // returns value
			}

			// make object! 10
			if (IS_NUMBER(arg)) {
				n = Int32s(arg, 0);
				obj = Make_Frame(n, TRUE);
				MANAGE_FRAME(obj);
				break; // returns obj
			}

			// make object! map!
			if (IS_MAP(arg)) {
				obj = Map_To_Object(VAL_SERIES(arg));
				break; // returns obj
			}

			//if (IS_NONE(arg)) {obj = Make_Frame(0, TRUE); break;}

			Trap_Make_DEAD_END(type, arg);
		}

		// make parent-object ....
		if (IS_OBJECT(value)) {
			type = REB_OBJECT;
			src_obj  = VAL_OBJ_FRAME(value);

			// make parent none | []
			if (IS_NONE(arg) || (IS_BLOCK(arg) && IS_EMPTY(arg))) {
				obj = Copy_Array_Core_Managed(
					src_obj, 0, SERIES_TAIL(src_obj), TRUE, TS_CLONE
				);
				Rebind_Frame(src_obj, obj);
				break;	// returns obj
			}

			// make parent [...]
			if (IS_BLOCK(arg)) {
				obj = Make_Object(src_obj, VAL_BLK_DATA(arg));
				Rebind_Frame(src_obj, obj);
				Val_Init_Object(D_OUT, obj);
				Bind_Values_Deep(VAL_BLK_DATA(arg), obj);

				// GC-OK
				if (Do_Block_Throws(D_OUT, VAL_SERIES(arg), 0)) return R_OUT;

				break; // returns obj
			}

			// make parent-object object
			if (IS_OBJECT(arg)) {
				obj = Merge_Frames(src_obj, VAL_OBJ_FRAME(arg));
				MANAGE_FRAME(obj);
				break; // returns obj
			}
		}
		Trap_Make_DEAD_END(VAL_TYPE(value), value);

	case A_TO:
		// special conversions to object! | error! | module!
		if (IS_DATATYPE(value)) {
			type = VAL_TYPE_KIND(value);
			if (type == REB_ERROR) {
				// arg is block/string, returns value
				if (!Make_Error_Object(value, arg)) {
					assert(THROWN(value));
					// going to return it anyway, no special handling needed
				}
				type = 0; // type already set
				break; // returns value
			}
			else if (type == REB_OBJECT) {
				if (IS_ERROR(arg)) {
					if (VAL_ERR_NUM(arg) < 100) Trap_Arg_DEAD_END(arg);
					obj = VAL_ERR_OBJECT(arg);
					break; // returns obj
				}
			}
			else if (type == REB_MODULE) {
				if (!IS_BLOCK(arg) || IS_EMPTY(arg)) Trap_Make_DEAD_END(REB_MODULE, arg);
				val = VAL_BLK_DATA(arg); // module spec
				if (!IS_OBJECT(val)) Trap_Arg_DEAD_END(val);
				obj = VAL_OBJ_FRAME(val);
				val++; // module object
				if (!IS_OBJECT(val)) Trap_Arg_DEAD_END(val);
				VAL_MOD_SPEC(val) = obj;
				VAL_MOD_BODY(val) = NULL;
				*value = *val;
				VAL_SET(value, REB_MODULE);
				type = 0; // type already set
				break; // returns value
			}
		}
		else type = VAL_TYPE(value);
		Trap_Make_DEAD_END(type, arg);

	case A_APPEND:
		TRAP_PROTECT(VAL_OBJ_FRAME(value));
		if (IS_OBJECT(value)) {
			Append_Obj(VAL_OBJ_FRAME(value), arg);
			return R_ARG1;
		}
		else
			Trap_Action_DEAD_END(VAL_TYPE(value), action); // !!! needs better error

	case A_LENGTH:
		if (IS_OBJECT(value)) {
			SET_INTEGER(D_OUT, SERIES_TAIL(VAL_OBJ_FRAME(value)) - 1);
			return R_OUT;
		}
		Trap_Action_DEAD_END(VAL_TYPE(value), action);

	case A_COPY:
		// Note: words are not copied and bindings not changed!
	{
		REBU64 types = 0;
		if (D_REF(ARG_COPY_PART)) Trap_DEAD_END(RE_BAD_REFINES);
		if (D_REF(ARG_COPY_DEEP)) {
			types |= D_REF(ARG_COPY_TYPES) ? 0 : TS_STD_SERIES;
		}
		if (D_REF(ARG_COPY_TYPES)) {
			arg = D_ARG(ARG_COPY_KINDS);
			if (IS_DATATYPE(arg)) types |= TYPESET(VAL_TYPE_KIND(arg));
			else types |= VAL_TYPESET(arg);
		}
		VAL_OBJ_FRAME(value) = obj = Copy_Array_Shallow(VAL_OBJ_FRAME(value));
		if (types != 0) {
			Clonify_Values_Len_Managed(
				BLK_SKIP(obj, 1),
				SERIES_TAIL(obj) - 1,
				D_REF(ARG_COPY_DEEP),
				types
			);
		}
		break; // returns value
	}
	case A_SELECT:
	case A_FIND:
		n = 0;
		if (IS_WORD(arg))
			n = Find_Word_Index(VAL_OBJ_FRAME(value), VAL_WORD_SYM(arg), FALSE);

		if (n <= 0 || (REBCNT)n >= SERIES_TAIL(VAL_OBJ_FRAME(value)))
			return R_NONE;

		if (action == A_FIND) goto is_true;

		value = VAL_OBJ_VALUES(value) + n;
		break;

	case A_REFLECT:
		action = What_Reflector(arg); // zero on error
		if (action == OF_SPEC) {
			if (!VAL_MOD_SPEC(value)) return R_NONE;
			VAL_OBJ_FRAME(value) = VAL_MOD_SPEC(value);
			VAL_SET(value, REB_OBJECT);
			break;
		}
		// Adjust for compatibility with PICK:
		if (action == OF_VALUES) action = 2;
		else if (action == OF_BODY) action = 3;
		if (action < 1 || action > 3) Trap_Reflect_DEAD_END(VAL_TYPE(value), arg);
#ifdef obsolete
		goto reflect;

	case A_PICK:
		action = Get_Num_Arg(arg); // integer, decimal, logic
		if (action < 1 || action > 3) Trap_Arg_DEAD_END(arg);
		if (action < 3) action |= 4;  // add SELF to list
reflect:
#endif
		Val_Init_Block(value, Make_Object_Block(VAL_OBJ_FRAME(value), action));
		break;

	case A_TRIM:
		if (Find_Refines(call_, ALL_TRIM_REFS)) Trap_DEAD_END(RE_BAD_REFINES); // none allowed
		type = VAL_TYPE(value);
		obj = Trim_Object(VAL_OBJ_FRAME(value));
		break;

	case A_TAILQ:
		if (IS_OBJECT(value)) {
			SET_LOGIC(D_OUT, SERIES_TAIL(VAL_OBJ_FRAME(value)) <= 1);
			return R_OUT;
		}
		Trap_Action_DEAD_END(VAL_TYPE(value), action);

	default:
		Trap_Action_DEAD_END(VAL_TYPE(value), action);
	}

	if (type) {
		VAL_SET(value, type);
		VAL_OBJ_FRAME(value) = obj;
	}

	*D_OUT = *value;
	return R_OUT;

is_true:
	return R_TRUE;
}


//
//  PD_Frame: C
//  
//  pvs->value points to the first value in frame (SELF).
//
REBINT PD_Frame(REBPVS *pvs)
{
	REBCNT sym;
	REBCNT s;
	REBVAL *word;
	REBVAL *val;

	if (IS_WORD(pvs->select)) {
		sym = VAL_WORD_SYM(pvs->select);
		s = SYMBOL_TO_CANON(sym);
		word = BLK_SKIP(VAL_FRM_WORDS(pvs->value), 1);
		for (val = pvs->value + 1; NOT_END(val); val++, word++) {
			if (sym == VAL_BIND_SYM(word) || s == VAL_BIND_CANON(word)) {
				if (VAL_GET_EXT(word, EXT_WORD_HIDE)) break;
				if (VAL_GET_EXT(word, EXT_WORD_LOCK))
					Trap1_DEAD_END(RE_LOCKED_WORD, word);
				pvs->value = val;
				return PE_SET;
			}
		}
	}
	return PE_BAD_SELECT;
}


//
//  REBTYPE: C
//
REBTYPE(Frame)
{
	switch (action) {
	case A_MAKE:
	case A_TO:
		Trap_Make_DEAD_END(REB_FRAME, D_ARG(2));
	}

	return R_ARG1;
}


#ifdef GET_OBJ_MODS_FINISHED
/***********************************************************************
**
**	Get_Obj_Mods -- return a block of modified words from an object
**
***********************************************************************/
REBVAL *Get_Obj_Mods(REBFRM *frame, REBVAL **inter_block)
{
	REBVAL *obj  = D_ARG(1);
	REBVAL *words, *val;
	REBFRM *frm  = VAL_OBJ_FRAME(obj);
	REBSER *ser  = Make_Array(2);
	REBOOL clear = D_REF(2);

	val   = BLK_HEAD(frm->values);
	words = BLK_HEAD(frm->words);
	for (; NOT_END(val); val++, words++)
		if (!(VAL_FLAGS(val) & FLAGS_CLEAN)) {
			Append_Value(ser, words);
			if (clear) VAL_FLAGS(val) |= FLAGS_CLEAN;
		}
	if (!STR_LEN(ser)) {
		goto is_none;
	}

	Bind_Values_Shallow(BLK_HEAD(ser), frm);
	VAL_SERIES(Temp_Blk_Value) = ser;

	return Temp_Blk_Value;
}
#endif
