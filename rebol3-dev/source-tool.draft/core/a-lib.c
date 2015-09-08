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
**  Module:  a-lib.c
**  Summary: exported REBOL library functions
**  Section: environment
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "reb-dialect.h"
#include "reb-ext.h"
#include "reb-evtypes.h"

// Linkage back to HOST functions. Needed when we compile as a DLL
// in order to use the OS_* macro functions.
#ifdef REB_API  // Included by C command line
REBOL_HOST_LIB *Host_Lib;
#endif

#include "reb-lib.h"

//#define DUMP_INIT_SCRIPT
#ifdef DUMP_INIT_SCRIPT
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>
#endif

extern const REBRXT Reb_To_RXT[REB_MAX];
extern RXIARG Value_To_RXI(REBVAL *val); // f-extension.c
extern void RXI_To_Value(REBVAL *val, RXIARG arg, REBCNT type); // f-extension.c
extern void RXI_To_Block(RXIFRM *frm, REBVAL *out); // f-extension.c
extern int Do_Callback(REBSER *obj, u32 name, RXIARG *args, RXIARG *result);


//
//  RL_Version: C
//  
//  Obtain current REBOL interpreter version information.
//  
//  Returns:
//      A byte array containing version, revision, update, and more.
//  Arguments:
//      vers - a byte array to hold the version info. First byte is length,
//          followed by version, revision, update, system, variation.
//  Notes:
//      This function can be called before any other initialization
//      to determine version compatiblity with the caller.
//

RL_API void RL_Version(REBYTE vers[])
{
	// [0] is length
	vers[1] = REBOL_VER;
	vers[2] = REBOL_REV;
	vers[3] = REBOL_UPD;
	vers[4] = REBOL_SYS;
	vers[5] = REBOL_VAR;
}


//
//  RL_Init: C
//  
//  Initialize the REBOL interpreter.
//  
//  Returns:
//      Zero on success, otherwise an error indicating that the
//      host library is not compatible with this release.
//  Arguments:
//      rargs - REBOL command line args and options structure.
//          See the host-args.c module for details.
//      lib - the host lib (OS_ functions) to be used by REBOL.
//          See host-lib.c for details.
//  Notes:
//      This function will allocate and initialize all memory
//      structures used by the REBOL interpreter. This is an
//      extensive process that takes time.
//

RL_API int RL_Init(REBARGS *rargs, void *lib)
{
	int marker;
	REBUPT bounds;
	const char *env_legacy = NULL;

	Host_Lib = cast(REBOL_HOST_LIB *, lib);

	if (Host_Lib->size < HOST_LIB_SIZE) return 1;
	if (((HOST_LIB_VER << 16) + HOST_LIB_SUM) != Host_Lib->ver_sum) return 2;

	bounds = (REBUPT)OS_CONFIG(1, 0);
	if (bounds == 0) bounds = (REBUPT)STACK_BOUNDS;

#ifdef OS_STACK_GROWS_UP
	Stack_Limit = (REBUPT)(&marker) + bounds;
#else
	if (bounds > (REBUPT) &marker) Stack_Limit = 100;
	else Stack_Limit = (REBUPT)&marker - bounds;
#endif

	Init_Core(rargs);

#if !defined(NDEBUG)
	env_legacy = getenv("R3_LEGACY");
	if (env_legacy != NULL && atoi(env_legacy) != 0) {
		Debug_Str(
			"**\n"
			"** R3_LEGACY is TRUE in environment variable!\n"
			"** system/options relating to historical behaviors are heeded:\n"
			"**\n"
			"** system/options: [\n"
			"**     (...)\n"
			"**     exit-functions-only: false\n"
			"**     broken-case-semantics: false\n"
			"** ]\n"
			"**\n"
		);
		PG_Legacy = TRUE;
	}
#endif

	GC_Active = TRUE; // Turn on GC
	if (rargs->options & RO_TRACE) {
		Trace_Level = 9999;
		Trace_Flags = 1;
	}

	return 0;
}


//
//  RL_Start: C
//  
//  Evaluate the default boot function.
//  
//  Returns:
//      Zero on success, otherwise indicates an error occurred.
//  Arguments:
//      bin - optional startup code (compressed), can be null
//      len - length of above bin
//      flags - special flags
//  Notes:
//      This function completes the startup sequence by calling
//      the sys/start function.
//

RL_API int RL_Start(REBYTE *bin, REBINT len, REBYTE *script, REBINT script_len, REBCNT flags)
{
	REBVAL *val;
	REBSER *ser;

	REBOL_STATE state;
	const REBVAL *error;

	REBVAL start_result;

	int result;
	REBVAL out;

	if (bin) {
		ser = Decompress(bin, len, 10000000, 0);
		if (!ser) return 1;

		val = BLK_SKIP(Sys_Context, SYS_CTX_BOOT_HOST);
		Val_Init_Binary(val, ser);
	}

	if (script && script_len > 4) {
		/* a 4-byte long payload type at the beginning */
		i32 ptype = 0;
		REBYTE *data = script + sizeof(ptype);
		script_len -= sizeof(ptype);

		memcpy(&ptype, script, sizeof(ptype));

		if (ptype == 1) {/* COMPRESSed data */
			ser = Decompress(data, script_len, 10000000, 0);
		} else {
			ser = Make_Binary(script_len);
			if (ser == NULL) {
				OS_FREE(script);
				return 1;
			}
			memcpy(BIN_HEAD(ser), data, script_len);
		}
		OS_FREE(script);

		val = BLK_SKIP(Sys_Context, SYS_CTX_BOOT_EMBEDDED);
		Val_Init_Binary(val, ser);
	}

	PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Trap()s can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		// Save error for EXPLAIN and return it
		*Get_System(SYS_STATE, STATE_LAST_ERROR) = *error;

		Print_Value(error, 1024, FALSE);

		// !!! Whether or not the Rebol interpreter just throws and quits
		// in an error case with a bad error code or breaks you into the
		// console to debug the environment should be controlled by
		// a command line option.  Defaulting to returning an error code
		// seems better, because kicking into an interactive session can
		// cause logging systems to hang.  For now we throw instead of
		// just quietly returning a code if the script fails, but add
		// that option!

		// For RE_HALT and all other errors we return the error
		// number.  Error numbers are not set in stone (currently), but
		// are never zero...which is why we can use 0 for success.
		return VAL_ERR_NUM(error);
	}

	if (!Do_Sys_Func(&out, SYS_CTX_FINISH_RL_START, 0)) {
		if (
			IS_WORD(&out) &&
			(VAL_WORD_SYM(&out) == SYM_QUIT || VAL_WORD_SYM(error) == SYM_EXIT)
		) {
			int status;

			TAKE_THROWN_ARG(&out, &out);
			status = Exit_Status_From_Value(&out);

			DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

			Shutdown_Core();
			OS_EXIT(status);
			DEAD_END;
		}

		Trap_Thrown(&out);
		DEAD_END;
	}

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	// The convention in the API was to return 0 for success.  We use the
	// convention (as for FINISH_INIT_CORE) that any non-UNSET! result from
	// FINISH_RL_START indicates something went wrong.

	if (IS_UNSET(&out))
		result = 0;
	else {
		assert(FALSE); // should not happen (raise an error instead)
		Debug_Fmt("** finish-rl-start returned non-NONE!:");
		Debug_Fmt("%r", &out);
		result = RE_MISC;
	}

	return result;
}


//
//  RL_Reset: C
//  
//  Reset REBOL (not implemented)
//  
//  Returns:
//      nothing
//  Arguments:
//      none
//  Notes:
//      Intended to reset the REBOL interpreter.
//

RL_API void RL_Reset(void)
{
	Panic(RP_NA);
}


//
//  RL_Extend: C
//  
//  Appends embedded extension to system/catalog/boot-exts.
//  
//  Returns:
//      A pointer to the REBOL library (see reb-lib.h).
//  Arguments:
//      source - A pointer to a UTF-8 (or ASCII) string that provides
//          extension module header, function definitions, and other
//          related functions and data.
//      call - A pointer to the extension's command dispatcher.
//  Notes:
//      This function simply adds the embedded extension to the
//      boot-exts list. All other processing and initialization
//      happens later during startup. Each embedded extension is
//      queried and init using LOAD-EXTENSION system native.
//      See c:extensions-embedded
//

RL_API void *RL_Extend(const REBYTE *source, RXICAL call)
{
	REBVAL *value;
	REBSER *ser;

	value = BLK_SKIP(Sys_Context, SYS_CTX_BOOT_EXTS);
	if (IS_BLOCK(value)) ser = VAL_SERIES(value);
	else {
		ser = Make_Array(2);
		Val_Init_Block(value, ser);
	}
	value = Alloc_Tail_Array(ser);
	Val_Init_Binary(value, Copy_Bytes(source, -1)); // UTF-8
	value = Alloc_Tail_Array(ser);
	SET_HANDLE_CODE(value, cast(CFUNC*, call));

	return Extension_Lib();
}


//
//  RL_Escape: C
//  
//  Signal that code evaluation needs to be interrupted.
//  
//  Returns:
//      nothing
//  Arguments:
//      reserved - must be set to zero.
//  Notes:
//      This function set's a signal that is checked during evaluation
//      and will cause the interpreter to begin processing an escape
//      trap. Note that control must be passed back to REBOL for the
//      signal to be recognized and handled.
//

RL_API void RL_Escape(REBINT reserved)
{
	SET_SIGNAL(SIG_ESCAPE);
}


//
//  RL_Do_String: C
//  
//  Load a string and evaluate the resulting block.
//  
//  Returns:
//      The datatype of the result.
//  Arguments:
//      text - A null terminated UTF-8 (or ASCII) string to transcode
//          into a block and evaluate.
//      flags - set to zero for now
//      result - value returned from evaluation.
//

RL_API int RL_Do_String(const REBYTE *text, REBCNT flags, RXIARG *result)
{
	REBSER *code;
	REBCNT len;
	REBVAL vali;

	REBVAL temp;
	REBVAL out;

	REBOL_STATE state;
	const REBVAL *error;

	assert(DSP == -1);

	PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Trap()s can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		// !!! Through this interface we have no way to distinguish an error
		// returned as a value from one that was thrown.  Yet by contract
		// we must return some sort of value--we try and patch over this
		// by printing out the error and returning an UNSET!.  RenC has
		// a stronger answer of offering the actual error catching interface
		// to clients directly.

		DS_PUSH_UNSET;

		// !!! If the user halted during a Do_String what should we return?
		// For now, assume the halt printed a message and don't do it again.
		// Otherwise, we should print the FORMed error
		if (VAL_ERR_NUM(error) != RE_HALT) {
			// !!! statics are not safe for multithreading.
			static REBOOL why_alert = TRUE;

			Out_Value(error, 640, FALSE, 0);

			// Save error for WHY?
			*Get_System(SYS_STATE, STATE_LAST_ERROR) = *error;

			// Tell them about why on the first error only
			if (why_alert) {
				Out_Str(
					cb_cast("** Note: use WHY? for more error information"), 2
				);
				why_alert = FALSE;
			}
		}

		if (result) {
			REBRXT type = Reb_To_RXT[VAL_TYPE(DS_TOP)];
			*result = Value_To_RXI(DS_TOP);

			SET_TRASH(DS_TOP);
			DS_DROP;

			return type;
		}

		return -VAL_ERR_NUM(error);
	}

	code = Scan_Source(text, LEN_BYTES(text));
	SAVE_SERIES(code);

	// Bind into lib or user spaces?
	if (flags) {
		// Top words will be added to lib:
		Bind_Values_Set_Forward_Shallow(BLK_HEAD(code), Lib_Context);
		Bind_Values_Deep(BLK_HEAD(code), Lib_Context);
	} else {
		REBSER *user = VAL_OBJ_FRAME(Get_System(SYS_CONTEXTS, CTX_USER));
		len = user->tail;
		Bind_Values_All_Deep(BLK_HEAD(code), user);
		SET_INTEGER(&vali, len);
		Resolve_Context(user, Lib_Context, &vali, FALSE, 0);
	}

	if (Do_Block_Throws(&out, code, 0)) {
		UNSAVE_SERIES(code);

		if (
			IS_WORD(&out) &&
			(VAL_WORD_SYM(&out) == SYM_QUIT || VAL_WORD_SYM(&out) == SYM_EXIT)
		) {
			int status;

			TAKE_THROWN_ARG(&out, &out);
			status = Exit_Status_From_Value(&out);

			DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

			Shutdown_Core();
			OS_EXIT(status);
			DEAD_END;
		}

		Trap_Thrown(&out);
		DEAD_END;
	}

	UNSAVE_SERIES(code);

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	if (result) {
		// Convert Do_Blk output to RXT and RXI if that was requested.

		REBRXT type = Reb_To_RXT[VAL_TYPE(&out)];
		*result = Value_To_RXI(&out);

		return type;
	}

	// Value is pushed on top of stack if no result parameter.  :-/
	// (The RL API was written with ideas like "print the top of stack"
	// while not exposing ways to analyze it unless converted to RXT/RXI)

	DS_PUSH(&out);

	return 0;
}


//
//  RL_Do_Binary: C
//  
//  Evaluate an encoded binary script such as compressed text.
//  
//  Returns:
//      The datatype of the result or zero if error in the encoding.
//  Arguments:
//      bin - by default, a REBOL compressed UTF-8 (or ASCII) script.
//      length - the length of the data.
//      flags - special flags (set to zero at this time).
//      key - encoding, encryption, or signature key.
//      result - value returned from evaluation.
//  Notes:
//      As of A104, only compressed scripts are supported, however,
//      rebin, cloaked, signed, and encrypted formats will be supported.
//

RL_API int RL_Do_Binary(const REBYTE *bin, REBINT length, REBCNT flags, REBCNT key, RXIARG *result)
{
	REBSER *text;
#ifdef DUMP_INIT_SCRIPT
	int f;
#endif
	REBRXT rxt;

	text = Decompress(bin, length, 10000000, 0);
	if (!text) return FALSE;
	Append_Codepoint_Raw(text, 0);

#ifdef DUMP_INIT_SCRIPT
	f = _open("host-boot.r", _O_CREAT | _O_RDWR, _S_IREAD | _S_IWRITE );
	_write(f, STR_HEAD(text), LEN_BYTES(STR_HEAD(text)));
	_close(f);
#endif

	SAVE_SERIES(text);
	rxt = RL_Do_String(text->data, flags, result);
	UNSAVE_SERIES(text);

	Free_Series(text);
	return rxt;
}


//
//  RL_Do_Block: C
//  
//  Evaluate a block. (not implemented)
//  
//  Returns:
//      The datatype of the result or zero if error in the encoding.
//  Arguments:
//      blk - A pointer to the block series
//      flags - set to zero for now
//      result - value returned from evaluation
//  Notes:
//      Not implemented. Contact Carl on R3 Chat if you think you
//      could use it for something.
//

RL_API int RL_Do_Block(REBSER *blk, REBCNT flags, RXIARG *result)
{
	return 0;
}


//
//  RL_Do_Commands: C
//  
//  Evaluate a block of extension commands at high speed.
//  
//  Returns:
//      Nothing
//  Arguments:
//      blk - a pointer to the block series
//      flags - set to zero for now
//      context - command evaluation context struct or zero if not used.
//  Notes:
//      For command blocks only, not for other blocks.
//      The context allows passing to each command a struct that is
//      used for back-referencing your environment data or for tracking
//      the evaluation block and its index.
//

RL_API void RL_Do_Commands(REBSER *blk, REBCNT flags, REBCEC *context)
{
	REBVAL out;
	Do_Commands(&out, blk, context);
}


//
//  RL_Print: C
//  
//  Low level print of formatted data to the console.
//  
//  Returns:
//      nothing
//  Arguments:
//      fmt - A format string similar but not identical to printf.
//          Special options are available.
//      ... - Values to be formatted.
//  Notes:
//      This function is low level and handles only a few C datatypes
//      at this time.
//

RL_API void RL_Print(const REBYTE *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Debug_Buf(cs_cast(fmt), &args);
	va_end(args);
}


//
//  RL_Print_TOS: C
//  
//  Print top REBOL stack value to the console and drop it.
//  
//  Returns:
//      Nothing
//  Arguments:
//      flags - special flags (set to zero at this time).
//      marker - placed at beginning of line to indicate output.
//  Notes:
//      This function is used for the main console evaluation
//      input loop to print the results of evaluation from stack.
//      The REBOL data stack is an abstract structure that can
//      change between releases. This function allows the host
//      to print the result of processed functions.
//      Marker is usually "==" to show output.
//      The system/options/result-types determine which values
//      are automatically printed.
//

RL_API void RL_Print_TOS(REBCNT flags, const REBYTE *marker)
{
	if (DSP != 0)
		Debug_Fmt(Str_Stack_Misaligned, DSP);

	// We shouldn't get any THROWN() errors exposed to the user
	assert(!IS_ERROR(DS_TOP) || !THROWN(DS_TOP));

	if (!IS_UNSET(DS_TOP)) {
		if (marker) Out_Str(marker, 0);
		Out_Value(DS_TOP, 500, TRUE, 1); // limit, molded
	}

	DS_DROP;
}


//
//  RL_Event: C
//  
//  Appends an application event (e.g. GUI) to the event port.
//  
//  Returns:
//      Returns TRUE if queued, or FALSE if event queue is full.
//  Arguments:
//      evt - A properly initialized event structure. The
//          contents of this structure are copied as part of
//          the function, allowing use of locals.
//  Notes:
//      Sets a signal to get REBOL attention for WAIT and awake.
//      To avoid environment problems, this function only appends
//      to the event queue (no auto-expand). So if the queue is full
//

RL_API int RL_Event(REBEVT *evt)
{
	REBVAL *event = Append_Event();		// sets signal

	if (event) {						// null if no room left in series
		VAL_SET(event, REB_EVENT);		// (has more space, if we need it)
		event->data.event = *evt;
		return 1;
	}

	return 0;
}


//
//  RL_Update_Event: C
//  
//  Updates an application event (e.g. GUI) to the event port.
//  
//  Returns:
//      Returns 1 if updated, or 0 if event appended, and -1 if full.
//  Arguments:
//      evt - A properly initialized event structure. The
//           model and type of the event are used to address
//           the unhandled event in the queue, when it is found,
//           it will be replaced with this one
//

RL_API int RL_Update_Event(REBEVT *evt)
{
	REBVAL *event = Find_Last_Event(evt->model, evt->type);

	if (event) {
		event->data.event = *evt;
		return 1;
	}

	return RL_Event(evt) - 1;
}


/***********************************************************************
**
*/ RL_API REBEVT *RL_Find_Event (REBINT model, REBINT type)
/*
**	Find an application event (e.g. GUI) to the event port.
**
**	Returns:
**  	A pointer to the find event
**  Arguments:
**      model - event model
**      type - event type
**
***********************************************************************/
{
	REBVAL * val = Find_Last_Event(model, type);
	if (val != NULL) {
		return &val->data.event;
	}
	return NULL;
}


//
//  RL_Make_Block: C
//  
//  Allocate a series suitable for storing Rebol values.  This series
//  can be used as a backing store for a BLOCK!, but also for any
//  other Rebol Array type (PAREN!, PATH!, GET-PATH!, SET-PATH!, or
//  LIT-PATH!).
//  
//  Returns:
//      A pointer to a block series.
//  Arguments:
//      size - the length of the block. The system will add one extra
//          for the end-of-block marker.
//  Notes:
//      Blocks are allocated with REBOL's internal memory manager.
//      Internal structures may change, so NO assumptions should be made!
//      Blocks are automatically garbage collected if there are
//      no references to them from REBOL code (C code does nothing.)
//      However, you can lock blocks to prevent deallocation. (?? default)
//

RL_API void *RL_Make_Block(u32 size)
{
	return Make_Array(size);
}


//
//  RL_Make_String: C
//  
//  Allocate a new string or binary series.
//  
//  Returns:
//      A pointer to a string or binary series.
//  Arguments:
//      size - the length of the string. The system will add one extra
//          for a null terminator (not strictly required, but good for C.)
//      unicode - set FALSE for ASCII/Latin1 strings, set TRUE for Unicode.
//  Notes:
//      Strings can be REBYTE or REBCHR sized (depends on R3 config.)
//      Strings are allocated with REBOL's internal memory manager.
//      Internal structures may change, so NO assumptions should be made!
//      Strings are automatically garbage collected if there are
//      no references to them from REBOL code (C code does nothing.)
//      However, you can lock strings to prevent deallocation. (?? default)
//

RL_API void *RL_Make_String(u32 size, int unicode)
{
	REBSER *result = unicode ? Make_Unicode(size) : Make_Binary(size);

	// !!! Assume client does not have Free_Series() or MANAGE_SERIES()
	// APIs, so the series we give back must be managed.  But how can
	// we be sure they get what usage they needed before the GC happens?
	MANAGE_SERIES(result);
	return result;
}


//
//  RL_Make_Image: C
//  
//  Allocate a new image of the given size.
//  
//  Returns:
//      A pointer to an image series, or zero if size is too large.
//  Arguments:
//      width - the width of the image in pixels
//      height - the height of the image in lines
//  Notes:
//      Images are allocated with REBOL's internal memory manager.
//      Image are automatically garbage collected if there are
//      no references to them from REBOL code (C code does nothing.)
//

RL_API void *RL_Make_Image(u32 width, u32 height)
{
	return Make_Image(width, height, FALSE);
}


//
//  RL_Protect_GC: C
//  
//  Protect memory from garbage collection.
//  
//  Returns:
//      nothing
//  Arguments:
//      series - a series to protect (block, string, image, ...)
//      flags - set to 1 to protect, 0 to unprotect
//  Notes:
//      You should only use this function when absolutely necessary,
//      because it bypasses garbage collection for the specified series.
//      Meaning: if you protect a series, it will never be freed.
//      Also, you only need this function if you allocate several series
//      such as strings, blocks, images, etc. within the same command
//      and you don't store those references somewhere where the GC can
//      find them, such as in an existing block or object (variable).
//

RL_API void RL_Protect_GC(REBSER *series, u32 flags)
{
	(flags == 1) ? SERIES_SET_FLAG(series, SER_KEEP) : SERIES_CLR_FLAG(series, SER_KEEP);
}


//
//  RL_Get_String: C
//  
//  Obtain a pointer into a string (bytes or unicode).
//  
//  Returns:
//      The length and type of string. When len > 0, string is unicode.
//      When len < 0, string is bytes.
//  Arguments:
//      series - string series pointer
//      index - index from beginning (zero-based)
//      str   - pointer to first character
//  Notes:
//      If the len is less than zero, then the string is optimized to
//      codepoints (chars) 255 or less for ASCII and LATIN-1 charsets.
//      Strings are allowed to move in memory. Therefore, you will want
//      to make a copy of the string if needed.
//

RL_API int RL_Get_String(REBSER *series, u32 index, void **str)
{	// ret: len or -len
	int len = (index >= series->tail) ? 0 : series->tail - index;

	if (BYTE_SIZE(series)) {
		*str = BIN_SKIP(series, index);
		len = -len;
	}
	else {
		*str = UNI_SKIP(series, index);
	}

	return len;
}


//
//  RL_Map_Word: C
//  
//  Given a word as a string, return its global word identifier.
//  
//  Returns:
//      The word identifier that matches the string.
//  Arguments:
//      string - a valid word as a UTF-8 encoded string.
//  Notes:
//      Word identifiers are persistent, and you can use them anytime.
//      If the word is new (not found in master symbol table)
//      it will be added and the new word identifier is returned.
//

RL_API u32 RL_Map_Word(REBYTE *string)
{
	return Make_Word(string, LEN_BYTES(string));
}


//
//  RL_Map_Words: C
//  
//  Given a block of word values, return an array of word ids.
//  
//  Returns:
//      An array of global word identifiers (integers). The [0] value is the size.
//  Arguments:
//      series - block of words as values (from REBOL blocks, not strings.)
//  Notes:
//      Word identifiers are persistent, and you can use them anytime.
//      The block can include any kind of word, including set-words, lit-words, etc.
//      If the input block contains non-words, they will be skipped.
//      The array is allocated with OS_ALLOC and you can OS_FREE it any time.
//

RL_API u32 *RL_Map_Words(REBSER *series)
{
	REBCNT i = 1;
	u32 *words;
	REBVAL *val = BLK_HEAD(series);

	words = OS_ALLOC_ARRAY(u32, series->tail + 2);

	for (; NOT_END(val); val++) {
		if (ANY_WORD(val)) words[i++] = VAL_WORD_CANON(val);
	}

	words[0] = i;
	words[i] = 0;

	return words;
}


//
//  RL_Word_String: C
//  
//  Return a string related to a given global word identifier.
//  
//  Returns:
//      A copy of the word string, null terminated.
//  Arguments:
//      word - a global word identifier
//  Notes:
//      The result is a null terminated copy of the name for your own use.
//      The string is always UTF-8 encoded (chars > 127 are encoded.)
//      In this API, word identifiers are always canonical. Therefore,
//      the returned string may have different spelling/casing than expected.
//      The string is allocated with OS_ALLOC and you can OS_FREE it any time.
//

RL_API REBYTE *RL_Word_String(u32 word)
{
	REBYTE *s1, *s2;
	// !!This code should use a function from c-words.c (but nothing perfect yet.)
	if (word == 0 || word >= PG_Word_Table.series->tail) return 0;
	s1 = VAL_SYM_NAME(BLK_SKIP(PG_Word_Table.series, word));
	s2 = OS_ALLOC_ARRAY(REBYTE, LEN_BYTES(s1) + 1);
	COPY_BYTES(s2, s1, LEN_BYTES(s1) + 1);
	return s2;
}


//
//  RL_Find_Word: C
//  
//  Given an array of word ids, return the index of the given word.
//  
//  Returns:
//      The index of the given word or zero.
//  Arguments:
//      words - a word array like that returned from MAP_WORDS (first element is size)
//      word - a word id
//  Notes:
//      The first element of the word array is the length of the array.
//

RL_API u32 RL_Find_Word(u32 *words, u32 word)
{
	REBCNT n = 0;

	if (words == 0) return 0;

	for (n = 1; n < words[0]; n++) {
		if (words[n] == word) return n;
	}
	return 0;
}


//
//  RL_Series: C
//  
//  Get series information.
//  
//  Returns:
//      Returns information related to a series.
//  Arguments:
//      series - any series pointer (string or block)
//      what - indicates what information to return (see RXI_SER enum)
//  Notes:
//      Invalid what arg nums will return zero.
//

RL_API REBUPT RL_Series(REBSER *series, REBCNT what)
{
	switch (what) {
	case RXI_SER_DATA: return (REBUPT)SERIES_DATA(series);
	case RXI_SER_TAIL: return SERIES_TAIL(series);
	case RXI_SER_LEFT: return SERIES_AVAIL(series);
	case RXI_SER_SIZE: return SERIES_REST(series);
	case RXI_SER_WIDE: return SERIES_WIDE(series);
	}
	return 0;
}


//
//  RL_Get_Char: C
//  
//  Get a character from byte or unicode string.
//  
//  Returns:
//      A Unicode character point from string. If index is
//      at or past the tail, a -1 is returned.
//  Arguments:
//      series - string series pointer
//      index - zero based index of character
//  Notes:
//      This function works for byte and unicoded strings.
//      The maximum size of a Unicode char is determined by
//      R3 build options. The default is 16 bits.
//

RL_API int RL_Get_Char(REBSER *series, u32 index)
{
	if (index >= series->tail) return -1;
	return GET_ANY_CHAR(series, index);
}


//
//  RL_Set_Char: C
//  
//  Set a character into a byte or unicode string.
//  
//  Returns:
//      The index passed as an argument.
//  Arguments:
//      series - string series pointer
//      index - where to store the character. If past the tail,
//          the string will be auto-expanded by one and the char
//          will be appended.
//

RL_API u32 RL_Set_Char(REBSER *series, u32 index, u32 chr)
{
	if (index >= series->tail) {
		index = series->tail;
		EXPAND_SERIES_TAIL(series, 1);
	}
	SET_ANY_CHAR(series, index, chr);
	return index;
}


//
//  RL_Get_Value: C
//  
//  Get a value from a block.
//  
//  Returns:
//      Datatype of value or zero if index is past tail.
//  Arguments:
//      series - block series pointer
//      index - index of the value in the block (zero based)
//      result - set to the value of the field
//

RL_API int RL_Get_Value(REBSER *series, u32 index, RXIARG *result)
{
	REBVAL *value;
	if (index >= series->tail) return 0;
	value = BLK_SKIP(series, index);
	*result = Value_To_RXI(value);
	return Reb_To_RXT[VAL_TYPE(value)];
}


//
//  RL_Set_Value: C
//  
//  Set a value in a block.
//  
//  Returns:
//      TRUE if index past end and value was appended to tail of block.
//  Arguments:
//      series - block series pointer
//      index - index of the value in the block (zero based)
//      val  - new value for field
//      type - datatype of value
//

RL_API int RL_Set_Value(REBSER *series, u32 index, RXIARG val, int type)
{
	REBVAL value;
	CLEARS(&value);
	RXI_To_Value(&value, val, type);
	if (index >= series->tail) {
		Append_Value(series, &value);
		return TRUE;
	}
	*BLK_SKIP(series, index) = value;
	return FALSE;
}


//
//  RL_Words_Of_Object: C
//  
//  Returns information about the object.
//  
//  Returns:
//      Returns an array of words used as fields of the object.
//  Arguments:
//      obj  - object pointer (e.g. from RXA_OBJECT)
//  Notes:
//      Returns a word array similar to MAP_WORDS().
//      The array is allocated with OS_ALLOC. You can OS_FREE it any time.
//

RL_API u32 *RL_Words_Of_Object(REBSER *obj)
{
	REBCNT index;
	u32 *words;
	REBVAL *syms;

	syms = FRM_WORD(obj, 1);
	// One less, because SELF not included.
	words = OS_ALLOC_ARRAY(u32, obj->tail);
	for (index = 0; index < (obj->tail-1); syms++, index++) {
		words[index] = VAL_BIND_CANON(syms);
	}
	words[index] = 0;
	return words;
}


//
//  RL_Get_Field: C
//  
//  Get a field value (context variable) of an object.
//  
//  Returns:
//      Datatype of value or zero if word is not found in the object.
//  Arguments:
//      obj  - object pointer (e.g. from RXA_OBJECT)
//      word - global word identifier (integer)
//      result - gets set to the value of the field
//

RL_API int RL_Get_Field(REBSER *obj, u32 word, RXIARG *result)
{
	REBVAL *value;
	if (!(word = Find_Word_Index(obj, word, FALSE))) return 0;
	value = BLK_SKIP(obj, word);
	*result = Value_To_RXI(value);
	return Reb_To_RXT[VAL_TYPE(value)];
}


//
//  RL_Set_Field: C
//  
//  Set a field (context variable) of an object.
//  
//  Returns:
//      The type arg, or zero if word not found in object or if field is protected.
//  Arguments:
//      obj  - object pointer (e.g. from RXA_OBJECT)
//      word - global word identifier (integer)
//      val  - new value for field
//      type - datatype of value
//

RL_API int RL_Set_Field(REBSER *obj, u32 word, RXIARG val, int type)
{
	REBVAL value;
	CLEARS(&value);
	if (!(word = Find_Word_Index(obj, word, FALSE))) return 0;
	if (VAL_GET_EXT(FRM_WORDS(obj) + word, EXT_WORD_LOCK)) return 0;
	RXI_To_Value(FRM_VALUES(obj)+word, val, type);
	return type;
}


//
//  RL_Callback: C
//  
//  Evaluate a REBOL callback function, either synchronous or asynchronous.
//  
//  Returns:
//      Sync callback: type of the result; async callback: true if queued
//  Arguments:
//      cbi - callback information including special option flags,
//          object pointer (where function is located), function name
//          as global word identifier (within above object), argument list
//          passed to callback (see notes below), and result value.
//  Notes:
//      The flag value will determine the type of callback. It can be either
//      synchronous, where the code will re-enter the interpreter environment
//      and call the specified function, or asynchronous where an EVT_CALLBACK
//      event is queued, and the callback will be evaluated later when events
//      are processed within the interpreter's environment.
//      For asynchronous callbacks, the cbi and the args array must be managed
//      because the data isn't processed until the callback event is
//      handled. Therefore, these cannot be allocated locally on
//      the C stack; they should be dynamic (or global if so desired.)
//      See c:extensions-callbacks
//

RL_API int RL_Callback(RXICBI *cbi)
{
	REBEVT evt;

	// Synchronous callback?
	if (!GET_FLAG(cbi->flags, RXC_ASYNC)) {
		return Do_Callback(cbi->obj, cbi->word, cbi->args, &(cbi->result));
	}

	CLEARS(&evt);
	evt.type = EVT_CALLBACK;
	evt.model = EVM_CALLBACK;
	evt.eventee.ser = cbi;
	SET_FLAG(cbi->flags, RXC_QUEUED);

	return RL_Event(&evt);	// (returns 0 if queue is full, ignored)
}


//
//  RL_Length_As_UTF8: C
//  
//      Calculate the UTF8 length of an array of unicode codepoints
//  
//  Returns:
//      How long the UTF8 encoded string would be
//  
//  Arguments:
//      p - pointer to array of bytes or wide characters
//      len - length of src in codepoints (not including terminator)
//      uni - true if src is in wide character format
//      ccr - convert linefeeds into linefeed + carraige-return
//  
//      !!! Host code is not supposed to call any Rebol routines except
//      for those in the RL_Api.  This exposes Rebol's internal UTF8
//      length routine, as it was being used by host code.  It should
//      be reviewed along with the rest of the RL_Api.
//

RL_API REBCNT RL_Length_As_UTF8(const void *p, REBCNT len, REBOOL uni, REBOOL ccr)
{
	return Length_As_UTF8(p, len, uni, ccr);
}


//
//  RL_Encode_UTF8: C
//  
//      Encode the unicode into UTF8 byte string.
//  
//  Returns:
//      Number of source chars used.
//  
//  Arguments:
//      dst - destination for encoded UTF8 bytes
//      max - maximum size of the result in bytes
//      src - source array of bytes or wide characters
//      len - input is source length, updated to reflect dst bytes used
//      uni - true if src is in wide character format
//      ccr - convert linefeed + carriage-return into just linefeed
//  
//  Notes:
//      Does not add a terminator.
//  
//      !!! Host code is not supposed to call any Rebol routines except
//      for those in the RL_Api.  This exposes Rebol's internal UTF8
//      length routine, as it was being used by the Linux host code by
//      Atronix.  Should be reviewed along with the rest of the RL_Api.
//

RL_API REBCNT RL_Encode_UTF8(REBYTE *dst, REBINT max, const void *src, REBCNT *len, REBFLG uni, REBFLG ccr)
{
	return Encode_UTF8(dst, max, src, len, uni, ccr);
}


#include "reb-lib-lib.h"

//
//  Extension_Lib: C
//

void *Extension_Lib(void)
{
	return &Ext_Lib;
}
