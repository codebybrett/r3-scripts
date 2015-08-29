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
**  Module:  b-init.c
**  Summary: initialization functions
**  Section: bootstrap
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#include <stddef.h> // for offsetof()

#define EVAL_DOSE 10000

// Boot Vars used locally:
static	REBCNT	Native_Count;
static	REBCNT	Native_Limit;
static	REBCNT	Action_Count;
static	REBCNT	Action_Marker;
static const REBFUN *Native_Functions;
static	BOOT_BLK *Boot_Block;


#ifdef WATCH_BOOT
#define DOUT(s) puts(s)
#else
#define DOUT(s)
#endif


/*******************************************************************************
**
**  Name: "Assert_Basics"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static void Assert_Basics(void)
{
	REBVAL val;

#if defined(SHOW_SIZEOFS)
	union Reb_Value_Data *dummy;
#endif

	VAL_SET(&val, 123);
#ifdef WATCH_BOOT
	printf("TYPE(123)=%d val=%d dat=%d gob=%d\n",
		VAL_TYPE(&val), sizeof(REBVAL), sizeof(REBDAT), sizeof(REBGOB));
#endif

#if defined(SHOW_SIZEOFS)
	// For debugging ports to some systems:
	printf("%d %s\n", sizeof(dummy->word), "word");
	printf("%d %s\n", sizeof(dummy->series), "series");
	printf("%d %s\n", sizeof(dummy->logic), "logic");
	printf("%d %s\n", sizeof(dummy->integer), "integer");
	printf("%d %s\n", sizeof(dummy->unteger), "unteger");
	printf("%d %s\n", sizeof(dummy->decimal), "decimal");
	printf("%d %s\n", sizeof(dummy->character), "char");
	printf("%d %s\n", sizeof(dummy->error), "error");
	printf("%d %s\n", sizeof(dummy->datatype), "datatype");
	printf("%d %s\n", sizeof(dummy->frame), "frame");
	printf("%d %s\n", sizeof(dummy->typeset), "typeset");
	printf("%d %s\n", sizeof(dummy->symbol), "symbol");
	printf("%d %s\n", sizeof(dummy->time), "time");
	printf("%d %s\n", sizeof(dummy->tuple), "tuple");
	printf("%d %s\n", sizeof(dummy->func), "func");
	printf("%d %s\n", sizeof(dummy->object), "object");
	printf("%d %s\n", sizeof(dummy->pair), "pair");
	printf("%d %s\n", sizeof(dummy->event), "event");
	printf("%d %s\n", sizeof(dummy->library), "library");
	printf("%d %s\n", sizeof(dummy->structure), "struct");
	printf("%d %s\n", sizeof(dummy->gob), "gob");
	printf("%d %s\n", sizeof(dummy->utype), "utype");
	printf("%d %s\n", sizeof(dummy->money), "money");
	printf("%d %s\n", sizeof(dummy->handle), "handle");
	printf("%d %s\n", sizeof(dummy->all), "all");
#endif

	if (cast(REBCNT, VAL_TYPE(&val)) != 123) Panic(RP_REBVAL_ALIGNMENT);
	if (sizeof(void *) == 8) {
		if (sizeof(REBVAL) != 32) Panic(RP_REBVAL_ALIGNMENT);
		if (sizeof(REBGOB) != 84) Panic(RP_BAD_SIZE);
	} else {
		if (sizeof(REBVAL) != 16) Panic(RP_REBVAL_ALIGNMENT);
		if (sizeof(REBGOB) != 64) Panic(RP_BAD_SIZE);
	}
	if (sizeof(REBDAT) != 4) Panic(RP_BAD_SIZE);

	// !!! C standard doesn't support 'offsetof(struct S, s_member.submember)'
	// so we're stuck using addition here.

	if (
		offsetof(struct Reb_Error, data)
		+ offsetof(union Reb_Error_Data, frame)
		!= offsetof(struct Reb_Object, frame)
	) {
		// When errors are exposed to the user then they must have a frame
		// and act like objects (they're dispatched through REBTYPE(Object))
		Panic(RP_MISC);
	}

	if (
		offsetof(struct Reb_Word, extra)
		+ offsetof(union Reb_Word_Extra, typebits)
		!= offsetof(struct Reb_Typeset, typebits)
	) {
		// Currently the typeset checking code is generic to run on both
		// an EXT_WORD_TYPED WORD! and a TYPESET!.
		Panic(RP_MISC);
	}
}


/*******************************************************************************
**
**  Name: "Print_Banner"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static void Print_Banner(REBARGS *rargs)
{
	if (rargs->options & RO_VERS) {
		Debug_Fmt(Str_Banner, REBOL_VER, REBOL_REV, REBOL_UPD, REBOL_SYS, REBOL_VAR);
		OS_EXIT(0);
	}
}


/*******************************************************************************
**
**  Name: "Do_Global_Block"
**  Summary: none
**  Details: {
**      Bind and evaluate a global block.
**      Rebind:
**          0: bind set into sys or lib
**         -1: bind shallow into sys (for NATIVE and ACTION)
**          1: add new words to LIB, bind/deep to LIB
**          2: add new words to SYS, bind/deep to LIB
**  
**      Expects result to be UNSET!}
**  Spec: none
**
*******************************************************************************/

static void Do_Global_Block(REBSER *block, REBINT rebind)
{
	REBVAL result;

	Bind_Values_Set_Forward_Shallow(
		BLK_HEAD(block), rebind > 1 ? Sys_Context : Lib_Context
	);

	if (rebind < 0) Bind_Values_Shallow(BLK_HEAD(block), Sys_Context);
	if (rebind > 0) Bind_Values_Deep(BLK_HEAD(block), Lib_Context);
	if (rebind > 1) Bind_Values_Deep(BLK_HEAD(block), Sys_Context);

	if (Do_Block_Throws(&result, block, 0))
		Panic(RP_EARLY_ERROR);

	if (!IS_UNSET(&result))
		Panic(RP_EARLY_ERROR);
}


/*******************************************************************************
**
**  Name: "Load_Boot"
**  Summary: none
**  Details: {
**      Decompress and scan in the boot block structure.  Can
**      only be called at the correct point because it will
**      create new symbols.}
**  Spec: none
**
*******************************************************************************/

static void Load_Boot(void)
{
	REBSER *boot;
	REBSER *text;

	// Decompress binary data in Native_Specs to get the textual source
	// of the function specs for the native routines into `boot` series.
	//
	// (Native_Specs array is in b-boot.c, auto-generated by make-boot.r)

	text = Decompress(
		Native_Specs, NAT_COMPRESSED_SIZE, NAT_UNCOMPRESSED_SIZE, 0
	);

	if (!text || (STR_LEN(text) != NAT_UNCOMPRESSED_SIZE))
		Panic(RP_BOOT_DATA);

	boot = Scan_Source(STR_HEAD(text), NAT_UNCOMPRESSED_SIZE);
	Free_Series(text);

	Set_Root_Series(ROOT_BOOT, boot, "boot block");	// Do not let it get GC'd

	Boot_Block = cast(BOOT_BLK *, VAL_BLK_HEAD(BLK_HEAD(boot)));

	if (VAL_TAIL(&Boot_Block->types) != REB_MAX)
		Panic(RP_BAD_BOOT_TYPE_BLOCK);
	if (VAL_WORD_SYM(VAL_BLK_HEAD(&Boot_Block->types)) != SYM_END_TYPE)
		Panic(RP_BAD_END_TYPE_WORD);

	// Create low-level string pointers (used by RS_ constants):
	{
		REBYTE *cp;
		REBINT i;

		PG_Boot_Strs = ALLOC_ARRAY(REBYTE *, RS_MAX);
		*ROOT_STRINGS = Boot_Block->strings;
		cp = VAL_BIN(ROOT_STRINGS);
		for (i = 0; i < RS_MAX; i++) {
			PG_Boot_Strs[i] = cp;
			while (*cp++);
		}
	}

	if (COMPARE_BYTES(cb_cast("end!"), Get_Sym_Name(SYM_END_TYPE)) != 0)
		Panic(RP_BAD_END_CANON_WORD);
	if (COMPARE_BYTES(cb_cast("true"), Get_Sym_Name(SYM_TRUE)) != 0)
		Panic(RP_BAD_TRUE_CANON_WORD);
	if (COMPARE_BYTES(cb_cast("line"), BOOT_STR(RS_SCAN, 1)) != 0)
		Panic(RP_BAD_BOOT_STRING);
}


/*******************************************************************************
**
**  Name: "Init_Datatypes"
**  Summary: none
**  Details: "^/        Create the datatypes."
**  Spec: none
**
*******************************************************************************/

static void Init_Datatypes(void)
{
	REBVAL *word = VAL_BLK_HEAD(&Boot_Block->types);
	REBSER *specs = VAL_SERIES(&Boot_Block->typespecs);
	REBVAL *value;
	REBINT n;

	for (n = 0; NOT_END(word); word++, n++) {
		assert(n < REB_MAX);
		value = Append_Frame(Lib_Context, word, 0);
		VAL_SET(value, REB_DATATYPE);
		VAL_TYPE_KIND(value) = cast(enum Reb_Kind, n);
		VAL_TYPE_SPEC(value) = VAL_SERIES(BLK_SKIP(specs, n));
	}
}


/*******************************************************************************
**
**  Name: "Init_Datatype_Checks"
**  Summary: none
**  Details: {
**      Create datatype test functions (e.g. integer?, time?, etc)
**      Must be done after typesets are initialized, so this cannot
**      be merged with the above.}
**  Spec: none
**
*******************************************************************************/

static void Init_Datatype_Checks(void)
{
	REBVAL *word = VAL_BLK_HEAD(&Boot_Block->types);
	REBVAL *value;
	REBSER *spec;
	REBCNT sym;
	REBINT n = 1;
	REBYTE str[32];

	spec = VAL_SERIES(VAL_BLK_HEAD(&Boot_Block->booters));

	for (word++; NOT_END(word); word++, n++) {
		COPY_BYTES(str, Get_Word_Name(word), 32);
		str[31] = '\0';
		str[LEN_BYTES(str)-1] = '?';
		sym = Make_Word(str, LEN_BYTES(str));
		//Print("sym: %s", Get_Sym_Name(sym));
		value = Append_Frame(Lib_Context, 0, sym);
		VAL_INT64(BLK_LAST(spec)) = n;  // special datatype id location
		Make_Native(
			value,
			Copy_Array_Shallow(spec),
			cast(REBFUN, A_TYPE),
			REB_ACTION
		);
	}

	value = Append_Frame(Lib_Context, 0, SYM_DATATYPES);
	*value = Boot_Block->types;
}


/*******************************************************************************
**
**  Name: "Init_Constants"
**  Summary: none
**  Details: {
**      Init constant words.
**  
**      WARNING: Do not create direct pointers into the Lib_Context
**      because it may get expanded and the pointers will be invalid.}
**  Spec: none
**
*******************************************************************************/

static void Init_Constants(void)
{
	REBVAL *value;
	extern const double pi1;

	value = Append_Frame(Lib_Context, 0, SYM_NONE);
	SET_NONE(value);

	value = Append_Frame(Lib_Context, 0, SYM_TRUE);
	SET_LOGIC(value, TRUE);

	value = Append_Frame(Lib_Context, 0, SYM_FALSE);
	SET_LOGIC(value, FALSE);

	value = Append_Frame(Lib_Context, 0, SYM_PI);
	SET_DECIMAL(value, pi1);
}


/*******************************************************************************
**
**  Name: "Use_Natives"
**  Summary: none
**  Details: {
**      Setup to use NATIVE function. If limit == 0, then the
**      native function table will be zero terminated (N_native).}
**  Spec: none
**
*******************************************************************************/

void Use_Natives(const REBFUN *funcs, REBCNT limit)
{
	Native_Count = 0;
	Native_Limit = limit;
	Native_Functions = funcs;
}


/*******************************************************************************
**
**  Name: "native"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBNATIVE(native)
{
	if ((Native_Limit == 0 && *Native_Functions) || (Native_Count < Native_Limit))
		Make_Native(D_OUT, VAL_SERIES(D_ARG(1)), *Native_Functions++, REB_NATIVE);
	else Trap(RE_MAX_NATIVES);
	Native_Count++;
	return R_OUT;
}


/*******************************************************************************
**
**  Name: "action"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBNATIVE(action)
{
	Action_Count++;
	if (Action_Count >= A_MAX_ACTION) Panic(RP_ACTION_OVERFLOW);
	Make_Native(
		D_OUT,
		VAL_SERIES(D_ARG(1)),
		cast(REBFUN, cast(REBUPT, Action_Count)),
		REB_ACTION
	);
	return R_OUT;
}


/*******************************************************************************
**
**  Name: "context"
**  Summary: "Defines a unique object."
**  Details: {
**      The spec block has already been bound to Lib_Context, to
**      allow any embedded values and functions to evaluate.
**  
**      Note: Overlaps MAKE OBJECT! code (REBTYPE(Object)'s A_MAKE)}
**  Spec: [
**      <1> spec
**  ]
**
*******************************************************************************/

REBNATIVE(context)
{
	REBVAL *spec = D_ARG(1);
	REBVAL evaluated;

	Val_Init_Object(D_OUT, Make_Object(0, VAL_BLK_HEAD(spec)));
	Bind_Values_Deep(VAL_BLK_HEAD(spec), VAL_OBJ_FRAME(D_OUT));

	if (Do_Block_Throws(&evaluated, VAL_SERIES(spec), 0)) {
		*D_OUT = evaluated;
		return R_OUT;
	}

	// On success, return the object (common case)
	return R_OUT;
}


/*******************************************************************************
**
**  Name: "Init_Ops"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static void Init_Ops(void)
{
	REBVAL *word;
	REBVAL *val;

	for (word = VAL_BLK_HEAD(&Boot_Block->ops); NOT_END(word); word++) {
		// Append the operator name to the lib frame:
		val = Append_Frame(Lib_Context, word, 0);

		// leave UNSET!, functions will be filled in later...
		cast(void, cast(REBUPT, val));
	}
}


/*******************************************************************************
**
**  Name: "Init_Natives"
**  Summary: none
**  Details: "^/        Create native functions."
**  Spec: none
**
*******************************************************************************/

static void Init_Natives(void)
{
	REBVAL *word;
	REBVAL *val;

	Action_Count = 0;
	Use_Natives(Native_Funcs, MAX_NATS);

	// Construct the first native, which is the NATIVE function creator itself:
	// native: native [spec [block!]]
	word = VAL_BLK_SKIP(&Boot_Block->booters, 1);
	if (!IS_SET_WORD(word) || VAL_WORD_SYM(word) != SYM_NATIVE)
		Panic(RE_NATIVE_BOOT);
	//val = BLK_SKIP(Sys_Context, SYS_CTX_NATIVE);
	val = Append_Frame(Lib_Context, word, 0);
	Make_Native(val, VAL_SERIES(word+2), Native_Functions[0], REB_NATIVE);

	word += 3; // action: native []
	//val = BLK_SKIP(Sys_Context, SYS_CTX_ACTION);
	val = Append_Frame(Lib_Context, word, 0);
	Make_Native(val, VAL_SERIES(word+2), Native_Functions[1], REB_NATIVE);
	Native_Count = 2;
	Native_Functions += 2;

	Action_Marker = SERIES_TAIL(Lib_Context)-1; // Save index for action words.
	Do_Global_Block(VAL_SERIES(&Boot_Block->actions), -1);
	Do_Global_Block(VAL_SERIES(&Boot_Block->natives), -1);
}


/***********************************************************************
**
*/	REBVAL *Get_Action_Word(REBCNT action)
/*
**		Return the word symbol for a given Action number.
**
***********************************************************************/
{
	return FRM_WORD(Lib_Context, Action_Marker+action);
}


/***********************************************************************
**
*/	REBVAL *Get_Action_Value(REBCNT action)
/*
**		Return the value (function) for a given Action number.
**
***********************************************************************/
{
	return FRM_VALUE(Lib_Context, Action_Marker+action);
}


/*******************************************************************************
**
**  Name: "Init_UType_Proto"
**  Summary: none
**  Details: "^/        Create prototype func object for UTypes."
**  Spec: none
**
*******************************************************************************/

void Init_UType_Proto(void)
{
	REBSER *frm = Make_Frame(A_MAX_ACTION - 1, TRUE);
	REBVAL *obj;
	REBINT n;

	Insert_Series(FRM_WORD_SERIES(frm), 1, (REBYTE*)FRM_WORD(Lib_Context, Action_Marker+1), A_MAX_ACTION);

	SERIES_TAIL(frm) = A_MAX_ACTION;
	for (n = 1; n < A_MAX_ACTION; n++)
		SET_NONE(BLK_SKIP(frm, n));
	BLK_TERM(frm);

	// !!! Termination was originally missing for the word series
	SERIES_TAIL(FRM_WORD_SERIES(frm)) = A_MAX_ACTION;
	BLK_TERM(FRM_WORD_SERIES(frm));

	obj = Get_System(SYS_STANDARD, STD_UTYPE);
	Val_Init_Object(obj, frm);
}


/*******************************************************************************
**
**  Name: "Init_Root_Context"
**  Summary: none
**  Details: {
**      Hand-build the root context where special REBOL values are
**      stored. Called early, so it cannot depend on any other
**      system structures or values.
**  
**      Note that the Root_Context's word table is unset!
**      None of its values are exported.}
**  Spec: none
**
*******************************************************************************/

static void Init_Root_Context(void)
{
	REBVAL *value;
	REBINT n;
	REBSER *frame;

	frame = Make_Array(ROOT_MAX);  // Only half the context! (No words)
	KEEP_SERIES(frame, "root context");
	LOCK_SERIES(frame);
	Root_Context = (ROOT_CTX*)(frame->data);

	// Get first value (the SELF for the context):
	value = ROOT_SELF;
	SET_FRAME(value, 0, 0); // No words or spec (at first)

	// Set all other values to NONE:
	for (n = 1; n < ROOT_MAX; n++) SET_NONE(value+n);
	SET_END(value+ROOT_MAX);
	SERIES_TAIL(frame) = ROOT_MAX;

	// Set the UNSET_VAL to UNSET!, so we have a sample UNSET! value
	// to pass as an arg if we need an UNSET but don't want to pay for making
	// a new one.  (There is also a NONE_VALUE for this purpose for NONE!s,
	// and an empty block as well.)
	SET_UNSET(ROOT_UNSET_VAL);
	assert(IS_NONE(NONE_VALUE));
	assert(IS_UNSET(UNSET_VALUE));

	Val_Init_Block(ROOT_EMPTY_BLOCK, Make_Array(0));
	SERIES_SET_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SER_PROT);
	SERIES_SET_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SER_LOCK);

	// We can't actually put a REB_END value in the middle of a block,
	// so we poke this one into a program global
	SET_END(&PG_End_Val);
	assert(IS_END(END_VALUE));

	// Initialize a few fields:
	Val_Init_Block(ROOT_ROOT, frame);
	Val_Init_Word_Unbound(ROOT_NONAME, REB_WORD, SYM__UNNAMED_);
}


/*******************************************************************************
**
**  Name: "Set_Root_Series"
**  Summary: none
**  Details: {
**      Used to set block and string values in the ROOT context.}
**  Spec: none
**
*******************************************************************************/

void Set_Root_Series(REBVAL *value, REBSER *ser, const char *label)
{
	KEEP_SERIES(ser, label);

	// Val_Init_Block and Val_Init_String would call Manage_Series and
	// make the series GC Managed, which would be a bad thing for series
	// like BUF_WORDS...because that would make all the series copied
	// from it managed too, and we don't always want that.  For now
	// we reproduce the logic of the routines.

	if (Is_Array_Series(ser)) {
		VAL_SET(value, REB_BLOCK);
		VAL_SERIES(value) = ser;
		VAL_INDEX(value) = 0;
	}
	else {
		assert(SERIES_WIDE(ser) == 1 || SERIES_WIDE(ser) == 2);
		VAL_SET(value, REB_STRING);
		VAL_SERIES(value) = ser;
		VAL_INDEX(value) = 0;
	}
}


/*******************************************************************************
**
**  Name: "Init_Task_Context"
**  Summary: none
**  Details: {
**      See above notes (same as root context, except for tasks)}
**  Spec: none
**
*******************************************************************************/

static void Init_Task_Context(void)
{
	REBVAL *value;
	REBINT n;
	REBSER *frame;

	//Print_Str("Task Context");

	Task_Series = frame = Make_Array(TASK_MAX);
	KEEP_SERIES(frame, "task context");
	LOCK_SERIES(frame);
	Task_Context = (TASK_CTX*)(frame->data);

	// Get first value (the SELF for the context):
	value = TASK_SELF;
	SET_FRAME(value, 0, 0); // No words or spec (at first)

	// Set all other values to NONE:
	for (n = 1; n < TASK_MAX; n++) SET_NONE(value+n);
	SET_END(value+TASK_MAX);
	SERIES_TAIL(frame) = TASK_MAX;

	// Initialize a few fields:
	SET_INTEGER(TASK_BALLAST, MEM_BALLAST);
	SET_INTEGER(TASK_MAX_BALLAST, MEM_BALLAST);

	// The THROWN_ARG lives under the root set, and must be a value
	// that won't trip up the GC.
	SET_TRASH_SAFE(TASK_THROWN_ARG);
}


/*******************************************************************************
**
**  Name: "Init_System_Object"
**  Summary: none
**  Details: "^/        The system object is defined in boot.r."
**  Spec: none
**
*******************************************************************************/

static void Init_System_Object(void)
{
	REBSER *frame;
	REBVAL *value;
	REBCNT n;
	REBVAL result;

	// Evaluate the system object and create the global SYSTEM word.
	// We do not BIND_ALL here to keep the internal system words out
	// of the global context. See also N_context() which creates the
	// subobjects of the system object.

	// Create the system object from the sysobj block and bind its fields:
	frame = Make_Object(0, VAL_BLK_HEAD(&Boot_Block->sysobj));
	Bind_Values_Deep(VAL_BLK_HEAD(&Boot_Block->sysobj), Lib_Context);

	// Bind it so CONTEXT native will work (only used at topmost depth):
	Bind_Values_Shallow(VAL_BLK_HEAD(&Boot_Block->sysobj), frame);

	// Evaluate the block (will eval FRAMEs within):
	if (Do_Block_Throws(&result, VAL_SERIES(&Boot_Block->sysobj), 0))
		Panic(RP_EARLY_ERROR);

	// Expects UNSET! by convention
	if (!IS_UNSET(&result))
		Panic(RP_EARLY_ERROR);

	// Create a global value for it:
	value = Append_Frame(Lib_Context, 0, SYM_SYSTEM);
	Val_Init_Object(value, frame);
	Val_Init_Object(ROOT_SYSTEM, frame);

	// Create system/datatypes block:
//	value = Get_System(SYS_DATATYPES, 0);
	value = Get_System(SYS_CATALOG, CAT_DATATYPES);
	frame = VAL_SERIES(value);
	Extend_Series(frame, REB_MAX-1);
	for (n = 1; n <= REB_MAX; n++) {
		Append_Value(frame, FRM_VALUES(Lib_Context) + n);
	}

	// Create system/catalog/datatypes block:
//	value = Get_System(SYS_CATALOG, CAT_DATATYPES);
//	Val_Init_Block(value, Copy_Blk(VAL_SERIES(&Boot_Block->types)));

	// Create system/catalog/actions block:
	value = Get_System(SYS_CATALOG, CAT_ACTIONS);
	Val_Init_Block(value, Collect_Set_Words(VAL_BLK_HEAD(&Boot_Block->actions)));

	// Create system/catalog/actions block:
	value = Get_System(SYS_CATALOG, CAT_NATIVES);
	Val_Init_Block(value, Collect_Set_Words(VAL_BLK_HEAD(&Boot_Block->natives)));

	// Create system/codecs object:
	value = Get_System(SYS_CODECS, 0);
	frame = Make_Frame(10, TRUE);
	Val_Init_Object(value, frame);

	// Set system/words to be the main context:
//	value = Get_System(SYS_WORDS, 0);
//	Val_Init_Object(value, Lib_Context);

	Init_UType_Proto();
}


/*******************************************************************************
**
**  Name: "Init_Contexts_Object"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static void Init_Contexts_Object(void)
{
	REBVAL *value;
//	REBSER *frame;

	value = Get_System(SYS_CONTEXTS, CTX_SYS);
	Val_Init_Object(value, Sys_Context);

	value = Get_System(SYS_CONTEXTS, CTX_LIB);
	Val_Init_Object(value, Lib_Context);

	value = Get_System(SYS_CONTEXTS, CTX_USER);  // default for new code evaluation
	Val_Init_Object(value, Lib_Context);

	// Make the boot context - used to store values created
	// during boot, but processed in REBOL code (e.g. codecs)
//	value = Get_System(SYS_CONTEXTS, CTX_BOOT);
//	frame = Make_Frame(4, TRUE);
//	Val_Init_Object(value, frame);
}

/*******************************************************************************
**
**  Name: "Codec_Text"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Codec_Text(REBCDI *codi)
{
	codi->error = 0;

	if (codi->action == CODI_ACT_IDENTIFY) {
		return CODI_CHECK; // error code is inverted result
	}

	if (codi->action == CODI_ACT_DECODE) {
		return CODI_TEXT;
	}

	if (codi->action == CODI_ACT_ENCODE) {
		return CODI_BINARY;
	}

	codi->error = CODI_ERR_NA;
	return CODI_ERROR;
}

/***********************************************************************
**
*/	REBINT Codec_UTF16(REBCDI *codi, int le)
/*
 * le: little endian
***********************************************************************/
{
	codi->error = 0;

	if (codi->action == CODI_ACT_IDENTIFY) {
		return CODI_CHECK; // error code is inverted result
	}

	if (codi->action == CODI_ACT_DECODE) {
		REBSER *ser = Make_Unicode(codi->len);
		REBINT size = Decode_UTF16(UNI_HEAD(ser), codi->data, codi->len, le, FALSE);
		SERIES_TAIL(ser) = size;
		if (size < 0) { //ASCII
			REBSER *dst = Make_Binary((size = -size));
			Append_Uni_Bytes(dst, UNI_HEAD(ser), size);
			ser = dst;
		}
		codi->data = SERIES_DATA(ser);
		codi->len = SERIES_TAIL(ser);
		codi->w = SERIES_WIDE(ser);
		return CODI_TEXT;
	}

	if (codi->action == CODI_ACT_ENCODE) {
		u16 * data = ALLOC_ARRAY(u16, codi->len);
		if (codi->w == 1) {
			/* in ASCII */
			REBCNT i = 0;
			for (i = 0; i < codi->len; i ++) {
#ifdef ENDIAN_LITTLE
				if (le) {
					data[i] = cast(char*, codi->extra.other)[i];
				} else {
					data[i] = cast(char*, codi->extra.other)[i] << 8;
				}
#elif defined (ENDIAN_BIG)
				if (le) {
					data[i] = cast(char*, codi->extra.other)[i] << 8;
				} else {
					data[i] = cast(char*, codi->extra.other)[i];
				}
#else
#error "Unsupported CPU endian"
#endif
			}
		} else if (codi->w == 2) {
			/* already in UTF16 */
#ifdef ENDIAN_LITTLE
			if (le) {
				memcpy(data, codi->extra.other, codi->len * sizeof(u16));
			} else {
				REBCNT i = 0;
				for (i = 0; i < codi->len; i ++) {
					REBUNI uni = cast(REBUNI*, codi->extra.other)[i];
					data[i] = ((uni & 0xff) << 8) | ((uni & 0xff00) >> 8);
				}
			}
#elif defined (ENDIAN_BIG)
			if (le) {
				REBCNT i = 0;
				for (i = 0; i < codi->len; i ++) {
					REBUNI uni = cast(REBUNI*, codi->extra.other)[i];
					data[i] = ((uni & 0xff) << 8) | ((uni & 0xff00) >> 8);
				}
			} else {
				memcpy(data, codi->extra.other, codi->len * sizeof(u16));
			}
#else
#error "Unsupported CPU endian"
#endif
		} else {
			/* RESERVED for future unicode expansion */
			codi->error = CODI_ERR_NA;
			return CODI_ERROR;
		}

		codi->len *= sizeof(u16);

		return CODI_BINARY;
	}

	codi->error = CODI_ERR_NA;
	return CODI_ERROR;
}

/*******************************************************************************
**
**  Name: "Codec_UTF16LE"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Codec_UTF16LE(REBCDI *codi)
{
	return Codec_UTF16(codi, TRUE);
}

/*******************************************************************************
**
**  Name: "Codec_UTF16BE"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Codec_UTF16BE(REBCDI *codi)
{
	return Codec_UTF16(codi, FALSE);
}

/*******************************************************************************
**
**  Name: "Codec_Markup"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBINT Codec_Markup(REBCDI *codi)
{
	codi->error = 0;

	if (codi->action == CODI_ACT_IDENTIFY) {
		return CODI_CHECK; // error code is inverted result
	}

	if (codi->action == CODI_ACT_DECODE) {
		codi->extra.other = Load_Markup(codi->data, codi->len);
		return CODI_BLOCK;
	}

	codi->error = CODI_ERR_NA;
	return CODI_ERROR;
}


/*******************************************************************************
**
**  Name: "Register_Codec"
**  Summary: none
**  Details: "^/        Internal function for adding a codec."
**  Spec: none
**
*******************************************************************************/

void Register_Codec(const REBYTE *name, codo dispatcher)
{
	REBVAL *value = Get_System(SYS_CODECS, 0);
	REBCNT sym = Make_Word(name, LEN_BYTES(name));

	value = Append_Frame(VAL_OBJ_FRAME(value), 0, sym);
	SET_HANDLE_CODE(value, cast(CFUNC*, dispatcher));
}


/*******************************************************************************
**
**  Name: "Init_Codecs"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

static void Init_Codecs(void)
{
	Register_Codec(cb_cast("text"), Codec_Text);
	Register_Codec(cb_cast("utf-16le"), Codec_UTF16LE);
	Register_Codec(cb_cast("utf-16be"), Codec_UTF16BE);
	Register_Codec(cb_cast("markup"), Codec_Markup);
	Init_BMP_Codec();
	Init_GIF_Codec();
	Init_PNG_Codec();
	Init_JPEG_Codec();
}


static void Set_Option_String(REBCHR *str, REBCNT field)
{
	REBVAL *val;
	if (str) {
		val = Get_System(SYS_OPTIONS, field);
		Val_Init_String(val, Copy_OS_Str(str, OS_STRLEN(str)));
	}
}

static REBCNT Set_Option_Word(REBCHR *str, REBCNT field)
{
	REBVAL *val;
	REBYTE *bp;
	REBYTE buf[40]; // option words always short ASCII strings
	REBCNT n = 0;

	if (str) {
		n = OS_STRLEN(str); // WC correct
		if (n > 38) return 0;
		bp = &buf[0];
		while ((*bp++ = cast(REBYTE, OS_CH_VALUE(*(str++))))); // clips unicode
		n = Make_Word(buf, n);
		val = Get_System(SYS_OPTIONS, field);
		Val_Init_Word_Unbound(val, REB_WORD, n);
	}
	return n;
}

/*******************************************************************************
**
**  Name: "Init_Main_Args"
**  Summary: none
**  Details: "^/        The system object is defined in boot.r."
**  Spec: none
**
*******************************************************************************/

static void Init_Main_Args(REBARGS *rargs)
{
	REBVAL *val;
	REBSER *ser;
	REBCHR *data;
	REBCNT n;


	ser = Make_Array(3);
	n = 2; // skip first flag (ROF_EXT)
	val = Get_System(SYS_CATALOG, CAT_BOOT_FLAGS);
	for (val = VAL_BLK_HEAD(val); NOT_END(val); val++) {
		VAL_CLR_OPT(val, OPT_VALUE_LINE);
		if (rargs->options & n) Append_Value(ser, val);
		n <<= 1;
	}
	val = Alloc_Tail_Array(ser);
	SET_TRUE(val);
	val = Get_System(SYS_OPTIONS, OPTIONS_FLAGS);
	Val_Init_Block(val, ser);

	// For compatibility:
	if (rargs->options & RO_QUIET) {
		val = Get_System(SYS_OPTIONS, OPTIONS_QUIET);
		SET_TRUE(val);
	}

	// Print("script: %s", rargs->script);
	if (rargs->script) {
		ser = To_REBOL_Path(rargs->script, 0, OS_WIDE, 0);
		val = Get_System(SYS_OPTIONS, OPTIONS_SCRIPT);
		Val_Init_File(val, ser);
	}

	if (rargs->exe_path) {
		ser = To_REBOL_Path(rargs->exe_path, 0, OS_WIDE, 0);
		val = Get_System(SYS_OPTIONS, OPTIONS_BOOT);
		Val_Init_File(val, ser);
	}

	// Print("home: %s", rargs->home_dir);
	if (rargs->home_dir) {
		ser = To_REBOL_Path(rargs->home_dir, 0, OS_WIDE, TRUE);
		val = Get_System(SYS_OPTIONS, OPTIONS_HOME);
		Val_Init_File(val, ser);
	}

	n = Set_Option_Word(rargs->boot, OPTIONS_BOOT_LEVEL);
	if (n >= SYM_BASE && n <= SYM_MODS)
		PG_Boot_Level = n - SYM_BASE; // 0 - 3

	if (rargs->args) {
		n = 0;
		while (rargs->args[n++]) NOOP;
		// n == number_of_args + 1
		ser = Make_Array(n);
		Val_Init_Block(Get_System(SYS_OPTIONS, OPTIONS_ARGS), ser);
		SERIES_TAIL(ser) = n - 1;
		for (n = 0; (data = rargs->args[n]); ++n)
			Val_Init_String(
				BLK_SKIP(ser, n), Copy_OS_Str(data, OS_STRLEN(data))
			);
		BLK_TERM(ser);
	}

	Set_Option_String(rargs->debug, OPTIONS_DEBUG);
	Set_Option_String(rargs->version, OPTIONS_VERSION);
	Set_Option_String(rargs->import, OPTIONS_IMPORT);

	// !!! The argument to --do exists in REBCHR* form in rargs->do_arg,
	// hence platform-specific encoding.  The host_main.c executes the --do
	// directly instead of using the Rebol-Value string set here.  Ultimately,
	// the Ren/C core will *not* be taking responsibility for setting any
	// "do-arg" variable in the system/options context...if a client of the
	// library has a --do option and wants to expose it, then it will have
	// to do so itself.  We'll leave this non-INTERN'd block here for now.
	Set_Option_String(rargs->do_arg, OPTIONS_DO_ARG);

	Set_Option_Word(rargs->secure, OPTIONS_SECURE);

	if ((data = OS_GET_LOCALE(0))) {
		val = Get_System(SYS_LOCALE, LOCALE_LANGUAGE);
		Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
		OS_FREE(data);
	}

	if ((data = OS_GET_LOCALE(1))) {
		val = Get_System(SYS_LOCALE, LOCALE_LANGUAGE_P);
		Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
		OS_FREE(data);
	}

	if ((data = OS_GET_LOCALE(2))) {
		val = Get_System(SYS_LOCALE, LOCALE_LOCALE);
		Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
		OS_FREE(data);
	}

	if ((data = OS_GET_LOCALE(3))) {
		val = Get_System(SYS_LOCALE, LOCALE_LOCALE_P);
		Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
		OS_FREE(data);
	}
}


/*******************************************************************************
**
**  Name: "Init_Task"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

void Init_Task(void)
{
	// Thread locals:
	Trace_Level = 0;
	Saved_State = 0;

	Eval_Cycles = 0;
	Eval_Dose = EVAL_DOSE;
	Eval_Signals = 0;
	Eval_Sigmask = ALL_BITS;

	// errors? problem with PG_Boot_Phase shared?

	Init_Pools(-4);
	Init_GC();
	Init_Task_Context();	// Special REBOL values per task

	Init_Raw_Print();
	Init_Words(TRUE);
	Init_Stacks(STACK_MIN/4);
	Init_Scanner();
	Init_Mold(MIN_COMMON/4);
	Init_Frame();
	//Inspect_Series(0);
}


/*******************************************************************************
**
**  Name: "Init_Year"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

void Init_Year(void)
{
	REBOL_DAT dat;

	OS_GET_TIME(&dat);
	Current_Year = dat.year;
}


/*******************************************************************************
**
**  Name: "Init_Core"
**  Summary: none
**  Details: {
**      GC is disabled during all init code, so these functions
**      need not protect themselves.}
**  Spec: none
**
*******************************************************************************/

void Init_Core(REBARGS *rargs)
{
	const REBVAL *error;
	REBOL_STATE state;
	REBVAL out;

	const REBYTE transparent[] = "transparent";
	const REBYTE infix[] = "infix";

	DOUT("Main init");

#ifndef NDEBUG
	PG_Always_Malloc = FALSE;
	PG_Legacy = FALSE;
#endif

	// Globals
	PG_Boot_Phase = BOOT_START;
	PG_Boot_Level = BOOT_LEVEL_FULL;
	PG_Mem_Usage = 0;
	PG_Mem_Limit = 0;
	PG_Reb_Stats = ALLOC(REB_STATS);
	Reb_Opts = ALLOC(REB_OPTS);
	Saved_State = NULL;

	// Thread locals:
	Trace_Level = 0;
	Saved_State = 0;
	Eval_Dose = EVAL_DOSE;
	Eval_Limit = 0;
	Eval_Signals = 0;
	Eval_Sigmask = ALL_BITS; /// dups Init_Task

	Init_StdIO();

	Assert_Basics();
	PG_Boot_Time = OS_DELTA_TIME(0, 0);

	DOUT("Level 0");
	Init_Pools(0);			// Memory allocator
	Init_GC();
	Init_Root_Context();	// Special REBOL values per program
	Init_Task_Context();	// Special REBOL values per task

	Init_Raw_Print();		// Low level output (Print)

	Print_Banner(rargs);

	DOUT("Level 1");
	Init_Char_Cases();
	Init_CRC();				// For word hashing
	Set_Random(0);
	Init_Words(FALSE);		// Symbol table
	Init_Stacks(STACK_MIN * 4);
	Init_Scanner();
	Init_Mold(MIN_COMMON);	// Output buffer
	Init_Frame();			// Frames

	Lib_Context = Make_Frame(600, TRUE); // !! Have MAKE-BOOT compute # of words
	Sys_Context = Make_Frame(50, TRUE);

	DOUT("Level 2");
	Load_Boot();			// Protected strings now available
	PG_Boot_Phase = BOOT_LOADED;
	//Debug_Str(BOOT_STR(RS_INFO,0)); // Booting...

	// Get the words of the ROOT context (to avoid it being an exception case):
	PG_Root_Words = Collect_Frame(
		NULL, VAL_BLK_HEAD(&Boot_Block->root), BIND_ALL
	);
	KEEP_SERIES(PG_Root_Words, "root words");
	VAL_FRM_WORDS(ROOT_SELF) = PG_Root_Words;

	// Create main values:
	DOUT("Level 3");
	Init_Datatypes();		// Create REBOL datatypes
	Init_Typesets();		// Create standard typesets
	Init_Datatype_Checks();	// The TYPE? checks
	Init_Constants();		// Constant values

	// Run actual code:
	DOUT("Level 4");
	Init_Natives();			// Built-in native functions
	Init_Ops();				// Built-in operators
	Init_System_Object();
	Init_Contexts_Object();
	Init_Main_Args(rargs);
	Init_Ports();
	Init_Codecs();
	Init_Errors(&Boot_Block->errors); // Needs system/standard/error object
	PG_Boot_Phase = BOOT_ERRORS;

	// We need these values around to compare to the tags we find in function
	// specs.  There may be a better place to put them or a better way to do
	// it, but it didn't seem there was a "compare UTF8 byte array to
	// arbitrary decoded REB_TAG which may or may not be REBUNI" routine.

	Val_Init_Tag(
		ROOT_TRANSPARENT_TAG,
		Append_UTF8(NULL, transparent, LEN_BYTES(transparent))
	);
	SERIES_SET_FLAG(VAL_SERIES(ROOT_TRANSPARENT_TAG), SER_LOCK);
	SERIES_SET_FLAG(VAL_SERIES(ROOT_TRANSPARENT_TAG), SER_PROT);

	Val_Init_Tag(
		ROOT_INFIX_TAG,
		Append_UTF8(NULL, infix, LEN_BYTES(infix))
	);
	SERIES_SET_FLAG(VAL_SERIES(ROOT_INFIX_TAG), SER_LOCK);
	SERIES_SET_FLAG(VAL_SERIES(ROOT_INFIX_TAG), SER_PROT);

	// Special pre-made errors:
	Val_Init_Error(TASK_STACK_ERROR, Make_Error(RE_STACK_OVERFLOW, 0, 0, 0));
	Val_Init_Error(TASK_HALT_ERROR, Make_Error(RE_HALT, 0, 0, 0));

	// With error trapping enabled, set up to catch them if they happen.
	PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Trap()s can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		// You shouldn't be able to halt during Init_Core() startup.
		// The only way you should be able to stop Init_Core() is by raising
		// an error, at which point the system will Panic out.
		// !!! TBD: Enforce not being *able* to trigger HALT
		assert(VAL_ERR_NUM(error) != RE_HALT);

		// If an error was raised during startup, print it and crash.
		Print_Value(error, 1024, FALSE);
		Panic(RP_EARLY_ERROR);
		DEAD_END_VOID;
	}

	Init_Year();

	// Initialize mezzanine functions:
	DOUT("Level 5");
	if (PG_Boot_Level >= BOOT_LEVEL_SYS) {
		Do_Global_Block(VAL_SERIES(&Boot_Block->base), 1);
		Do_Global_Block(VAL_SERIES(&Boot_Block->sys), 2);
	}

	*FRM_VALUE(Sys_Context, SYS_CTX_BOOT_MEZZ) = Boot_Block->mezz;
	*FRM_VALUE(Sys_Context, SYS_CTX_BOOT_PROT) = Boot_Block->protocols;

	// No longer needs protecting:
	SET_NONE(ROOT_BOOT);
	Boot_Block = NULL;
	PG_Boot_Phase = BOOT_MEZZ;

	assert(DSP == -1 && !DSF);

	if (!Do_Sys_Func(&out, SYS_CTX_FINISH_INIT_CORE, 0)) {
		// You shouldn't be able to exit or quit during Init_Core() startup.
		// The only way you should be able to stop Init_Core() is by raising
		// an error, at which point the system will Panic out.
		Debug_Fmt("** 'finish-init-core' returned THROWN() result: %r", &out);

		// !!! TBD: Enforce not being *able* to trigger QUIT or EXIT, but we
		// let them slide for the moment, even though we shouldn't.
		if (
			IS_WORD(&out) &&
			(VAL_WORD_SYM(&out) == SYM_QUIT || VAL_WORD_SYM(&out) == SYM_EXIT)
		) {
			int status;

			TAKE_THROWN_ARG(&out, &out);
			status = Exit_Status_From_Value(&out);

			Shutdown_Core();
			OS_EXIT(status);
			DEAD_END_VOID;
		}

		Panic(RP_EARLY_ERROR);
	}

	// Success of the 'finish-init-core' Rebol code is signified by returning
	// a UNSET! (all other return results indicate an error state)

	if (!IS_UNSET(&out)) {
		Debug_Fmt("** 'finish-init-core' returned non-none!: %r", &out);
		Panic(RP_EARLY_ERROR);
	}

	assert(DSP == -1 && !DSF);

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	PG_Boot_Phase = BOOT_DONE;

	Recycle(); // necessary?

	DOUT("Boot done");
}


/*******************************************************************************
**
**  Name: "Shutdown_Core"
**  Summary: none
**  Details: {
**      !!! Merging soon to a Git branch near you:
**      !!!    The ability to do clean shutdown, zero leaks.}
**  Spec: none
**
*******************************************************************************/

void Shutdown_Core(void)
{
	Shutdown_Stacks();
	assert(Saved_State == NULL);
	// assert(IS_TRASH(TASK_THROWN_ARG));
}
