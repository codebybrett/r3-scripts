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
**  Module:  n-io.c
**  Summary: native functions for input and output
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/** Helper Functions **************************************************/


//
//  echo: native [
//      "Copies console output to a file."
//      target [file! none! logic!]
//  ]
//

REBNATIVE(echo)
{
	REBVAL *val = D_ARG(1);
	REBSER *ser = 0;

	Echo_File(0);

	if (IS_FILE(val))
		ser = Value_To_OS_Path(val, TRUE);
	else if (IS_LOGIC(val) && VAL_LOGIC(val))
		ser = To_Local_Path("output.txt", 10, FALSE, TRUE);

	if (ser) {
		if (!Echo_File(cast(REBCHR*, ser->data)))
			Trap1_DEAD_END(RE_CANNOT_OPEN, val);
	}

	return R_OUT;
}


//
//  form: native [
//      "Converts a value to a human-readable string."
//      value [any-type!] "The value to form"
//  ]
//  
//      Converts a value to a REBOL readable string.
//      value    "The value to mold"
//      /only   "For a block value, give only contents, no outer [ ]"
//      /all    "Mold in serialized format"
//      /flat    "No line indentation"
//

REBNATIVE(form)
{
	Val_Init_String(D_OUT, Copy_Form_Value(D_ARG(1), 0));
	return R_OUT;
}


//
//  mold: native [
//      "Converts a value to a REBOL-readable string."
//      value [any-type!] "The value to mold"
//      /only {For a block value, mold only its contents, no outer []}
//      /all "Use construction syntax"
//      /flat "No indentation"
//  ]
//  
//      Converts a value to a REBOL readable string.
//      value    "The value to mold"
//      /only   "For a block value, give only contents, no outer [ ]"
//      /all    "Mold in serialized format"
//      /flat    "No line indentation"
//

REBNATIVE(mold)
{
	REBVAL *val = D_ARG(1);

	REB_MOLD mo;
	CLEARS(&mo);
	if (D_REF(3)) SET_FLAG(mo.opts, MOPT_MOLD_ALL);
	if (D_REF(4)) SET_FLAG(mo.opts, MOPT_INDENT);
	Reset_Mold(&mo);

	if (D_REF(2) && IS_BLOCK(val)) SET_FLAG(mo.opts, MOPT_ONLY);

	Mold_Value(&mo, val, TRUE);

	Val_Init_String(D_OUT, Copy_String(mo.series, 0, -1));

	return R_OUT;
}


//
//  print: native [
//      "Outputs a value followed by a line break."
//      value [any-type!] "The value to print"
//  ]
//

REBNATIVE(print)
{
	REBVAL *value = D_ARG(1);

	if (IS_BLOCK(value))
		Reduce_Block(value, VAL_SERIES(value), VAL_INDEX(value), FALSE);

	// value is safe from GC due to being in arg slot
	Print_Value(value, 0, 0);

	return R_UNSET;
}


//
//  prin: native [
//      "Outputs a value with no line break."
//      value [any-type!]
//  ]
//

REBNATIVE(prin)
{
	REBVAL *value = D_ARG(1);

	if (IS_BLOCK(value))
		Reduce_Block(value, VAL_SERIES(value), VAL_INDEX(value), FALSE);

	// value is safe from GC due to being in arg slot
	Prin_Value(value, 0, 0);

	return R_UNSET;
}


//
//  new-line: native [
//      {Sets or clears the new-line marker within a block or paren.}
//      position [block! paren!] "Position to change marker (modified)"
//      value "Set TRUE for newline"
//      /all "Set/clear marker to end of series"
//      /skip {Set/clear marker periodically to the end of the series}
//      size [integer!]
//  ]
//

REBNATIVE(new_line)
{
	REBVAL *value = D_ARG(1);
	REBVAL *val;
	REBOOL cond = IS_CONDITIONAL_TRUE(D_ARG(2));
	REBCNT n;
	REBINT skip = -1;

	val = VAL_BLK_DATA(value);
	if (D_REF(3)) skip = 1; // all
	if (D_REF(4)) { // skip
		skip = Int32s(D_ARG(5), 1); // size
		if (skip < 1) skip = 1;
	}
	for (n = 0; NOT_END(val); n++, val++) {
		if (cond ^ (n % skip != 0))
			VAL_SET_OPT(val, OPT_VALUE_LINE);
		else
			VAL_CLR_OPT(val, OPT_VALUE_LINE);
		if (skip < 0) break;
	}

	return R_ARG1;
}


//
//  new-line?: native [
//      {Returns the state of the new-line marker within a block or paren.}
//      position [block! paren!] "Position to check marker"
//  ]
//

REBNATIVE(new_lineq)
{
	if VAL_GET_OPT(VAL_BLK_DATA(D_ARG(1)), OPT_VALUE_LINE) return R_TRUE;
	return R_FALSE;
}


//
//  now: native [
//      "Returns date and time."
//      /year "Returns year only"
//      /month "Returns month only"
//      /day "Returns day of the month only"
//      /time "Returns time only"
//      /zone "Returns time zone offset from UCT (GMT) only"
//      /date "Returns date only"
//      /weekday {Returns day of the week as integer (Monday is day 1)}
//      /yearday "Returns day of the year (Julian)"
//      /precise "High precision time"
//      /utc "Universal time (no zone)"
//  ]
//  
//  Return the current date and time with timezone adjustment.
//  
//      1  /year {Returns year only.}
//      2  /month {Returns month only.}
//      3  /day {Returns day of the month only.}
//      4  /time {Returns time only.}
//      5  /zone {Returns time zone offset from GMT only.}
//      6  /date {Returns date only.}
//      7  /weekday {Returns day of the week as integer (Monday is day 1).}
//      8  /yearday {Returns day of the year (Julian)}
//      9  /precise {Higher precision}
//      10 /utc {Universal time (no zone)}
//

REBNATIVE(now)
{
	REBOL_DAT dat;
	REBINT n = -1;
	REBVAL *ret = D_OUT;

	OS_GET_TIME(&dat);
	if (!D_REF(9)) dat.nano = 0; // Not /precise
	Set_Date(ret, &dat);
	Current_Year = dat.year;

	if (D_REF(10)) { // UTC
		VAL_ZONE(ret) = 0;
	}
	else {
		if (D_REF(1) || D_REF(2) || D_REF(3) || D_REF(4)
			|| D_REF(6) || D_REF(7) || D_REF(8))
			Adjust_Date_Zone(ret, FALSE); // Add time zone, adjust date and time
	}

	// Check for /date, /time, /zone
	if (D_REF(6)) {			// date
		VAL_TIME(ret) = NO_TIME;
		VAL_ZONE(ret) = 0;
	}
	else if (D_REF(4)) {	// time
		//if (dat.time == ???) SET_NONE(ret);
		VAL_SET(ret, REB_TIME);
	}
	else if (D_REF(5)) {	// zone
		VAL_SET(ret, REB_TIME);
		VAL_TIME(ret) = VAL_ZONE(ret) * ZONE_MINS * MIN_SEC;
	}
	else if (D_REF(7)) n = Week_Day(VAL_DATE(ret));
	else if (D_REF(8)) n = Julian_Date(VAL_DATE(ret));
	else if (D_REF(1)) n = VAL_YEAR(ret);
	else if (D_REF(2)) n = VAL_MONTH(ret);
	else if (D_REF(3)) n = VAL_DAY(ret);

	if (n > 0) SET_INTEGER(ret, n);

	return R_OUT;
}


//
//  wait: native [
//      "Waits for a duration, port, or both."
//      value [number! time! port! block! none!]
//      /all "Returns all in a block"
//      /only {only check for ports given in the block to this function}
//  ]
//

REBNATIVE(wait)
{
	REBVAL *val = D_ARG(1);
	REBINT timeout = 0;	// in milliseconds
	REBSER *ports = 0;
	REBINT n = 0;

	SET_NONE(D_OUT);

	if (IS_BLOCK(val)) {
		REBVAL unsafe; // temporary not safe from GC
		Reduce_Block(&unsafe, VAL_SERIES(val), VAL_INDEX(val), FALSE);
		ports = VAL_SERIES(&unsafe);
		for (val = BLK_HEAD(ports); NOT_END(val); val++) { // find timeout
			if (Pending_Port(val)) n++;
			if (IS_INTEGER(val) || IS_DECIMAL(val)) break;
		}
		if (IS_END(val)) {
			if (n == 0) return R_NONE; // has no pending ports!
			// SET_NONE(val); // no timeout -- BUG: unterminated block in GC
		}
	}

	switch (VAL_TYPE(val)) {
	case REB_INTEGER:
		timeout = 1000 * Int32(val);
		goto chk_neg;

	case REB_DECIMAL:
		timeout = (REBINT)(1000 * VAL_DECIMAL(val));
		goto chk_neg;

	case REB_TIME:
		timeout = (REBINT) (VAL_TIME(val) / (SEC_SEC / 1000));
chk_neg:
		if (timeout < 0) Trap_Range_DEAD_END(val);
		break;

	case REB_PORT:
		if (!Pending_Port(val)) return R_NONE;
		ports = Make_Array(1);
		Append_Value(ports, val);
		// fall thru...
	case REB_NONE:
	case REB_END:
		timeout = ALL_BITS;	// wait for all windows
		break;

	default:
		Trap_Arg_DEAD_END(val);
	}

	// Prevent GC on temp port block:
	// Note: Port block is always a copy of the block.
	if (ports) Val_Init_Block(D_OUT, ports);

	// Process port events [stack-move]:
	if (!Wait_Ports(ports, timeout, D_REF(3))) {
		Sieve_Ports(NULL); /* just reset the waked list */
		return R_NONE;
	}
	if (!ports) return R_NONE;

	// Determine what port(s) waked us:
	Sieve_Ports(ports);

	if (!D_REF(2)) { // not /all ports
		val = BLK_HEAD(ports);
		if (IS_PORT(val)) *D_OUT = *val;
		else SET_NONE(D_OUT);
	}

	return R_OUT;
}


//
//  wake-up: native [
//      "Awake and update a port with event."
//      port [port!]
//      event [event!]
//  ]
//  
//      Calls port update for native actors.
//      Calls port awake function.
//

REBNATIVE(wake_up)
{
	REBVAL *val = D_ARG(1);
	REBSER *port = VAL_PORT(val);
	REBOOL awakened = TRUE; // start by assuming success

	if (SERIES_TAIL(port) < STD_PORT_MAX) Panic_DEAD_END(9910);

	val = OFV(port, STD_PORT_ACTOR);
	if (IS_NATIVE(val)) {
		Do_Port_Action(call_, port, A_UPDATE); // uses current stack frame
	}

	val = OFV(port, STD_PORT_AWAKE);
	if (ANY_FUNC(val)) {
		Apply_Func(D_OUT, val, D_ARG(2), 0);
		if (!(IS_LOGIC(D_OUT) && VAL_LOGIC(D_OUT))) awakened = FALSE;
		SET_TRASH_SAFE(D_OUT);
	}
	return awakened ? R_TRUE : R_FALSE;
}


//
//  to-rebol-file: native [
//      {Converts a local system file path to a REBOL file path.}
//      path [file! string!]
//  ]
//

REBNATIVE(to_rebol_file)
{
	REBVAL *arg = D_ARG(1);
	REBSER *ser;

	ser = Value_To_REBOL_Path(arg, 0);
	if (!ser) Trap_Arg_DEAD_END(arg);
	Val_Init_File(D_OUT, ser);

	return R_OUT;
}


//
//  to-local-file: native [
//      {Converts a REBOL file path to the local system file path.}
//      path [file! string!]
//      /full {Prepends current dir for full path (for relative paths only)}
//  ]
//

REBNATIVE(to_local_file)
{
	REBVAL *arg = D_ARG(1);
	REBSER *ser;

	ser = Value_To_Local_Path(arg, D_REF(2));
	if (!ser) Trap_Arg_DEAD_END(arg);
	Val_Init_String(D_OUT, ser);

	return R_OUT;
}


//
//  what-dir: native ["Returns the current directory path."]
//

REBNATIVE(what_dir)
{
	REBSER *ser;
	REBCHR *lpath;
	REBINT len;

	len = OS_GET_CURRENT_DIR(&lpath);
	ser = To_REBOL_Path(lpath, len, OS_WIDE, TRUE); // allocates extra for end /
	assert(ser); // should never be NULL
	OS_FREE(lpath);
	Val_Init_File(D_OUT, ser);

	return R_OUT;
}


//
//  change-dir: native [
//      "Changes the current directory path."
//      path [file!]
//  ]
//

REBNATIVE(change_dir)
{
	REBVAL *arg = D_ARG(1);
	REBSER *ser;
	REBINT n;
	REBVAL val;

	ser = Value_To_OS_Path(arg, TRUE);
	if (!ser) Trap_Arg_DEAD_END(arg); // !!! ERROR MSG

	Val_Init_String(&val, ser); // may be unicode or utf-8
	Check_Security(SYM_FILE, POL_EXEC, &val);

	n = OS_SET_CURRENT_DIR(cast(REBCHR*, ser->data));  // use len for bool
	if (!n) Trap_Arg_DEAD_END(arg); // !!! ERROR MSG

	return R_ARG1;
}


//
//  browse: native [
//      "Open web browser to a URL or local file."
//      url [url! file! none!]
//  ]
//

REBNATIVE(browse)
{
	REBINT r;
	REBCHR *url = 0;
	REBVAL *arg = D_ARG(1);

	Check_Security(SYM_BROWSE, POL_EXEC, arg);

	if (IS_NONE(arg))
		return R_UNSET;

	// !!! By passing NULL we don't get backing series to protect!
	url = Val_Str_To_OS_Managed(NULL, arg);

	r = OS_BROWSE(url, 0);

	if (r == 0) {
		return R_UNSET;
	} else {
		Make_OS_Error(D_OUT, r);
		Trap1_DEAD_END(RE_CALL_FAIL, D_OUT);
	}

	return R_UNSET;
}


/***********************************************************************
**
*/	REBNATIVE(call)
/*
 *
	/wait "Wait for command to terminate before returning"
    /console "Runs command with I/O redirected to console"
    /shell "Forces command to be run from shell"
	/info "Return process information object"
    /input in [string! file! none] "Redirects stdin to in"
    /output out [string! file! none] "Redirects stdout to out"
    /error err [string! file! none] "Redirects stderr to err"
***********************************************************************/
{
#define INHERIT_TYPE 0
#define NONE_TYPE 1
#define STRING_TYPE 2
#define FILE_TYPE 3
#define BINARY_TYPE 4

#define FLAG_WAIT 1
#define FLAG_CONSOLE 2
#define FLAG_SHELL 4
#define FLAG_INFO 8

	REBINT r;
	REBVAL *arg = D_ARG(1);
	REBU64 pid = MAX_U64; // Was REBI64 of -1, but OS_CREATE_PROCESS wants u64
	u32 flags = 0;

	// We synthesize the argc and argv from our REBVAL arg, and in the
	// process we may need to do dynamic allocations of argc strings.  In
	// Rebol this is always done by making a series, and if those series
	// are managed then we need to keep them SAVEd from the GC for the
	// duration they will be used.  Due to an artifact of the current
	// implementation, FILE! and STRING! turned into OS-compatible character
	// representations must be managed...so we need to save them over
	// the duration of the call.  We hold the pointers to remember to unsave.
	//
	int argc;
	const REBCHR **argv;
	REBCHR *cmd;
	REBSER *argv_ser = NULL;
	REBSER *argv_saved_sers = NULL;
	REBSER *cmd_ser = NULL;

	REBVAL *input = NULL;
	REBVAL *output = NULL;
	REBVAL *err = NULL;

	// Sometimes OS_CREATE_PROCESS passes back a input/output/err pointers,
	// and sometimes it expects one as input.  If it expects one as input
	// then we may have to transform the REBVAL into pointer data the OS
	// expects.  If we do so then we have to clean up after that transform.
	// (That cleanup could be just a Free_Series(), but an artifact of
	// implementation forces us to use managed series hence SAVE/UNSAVE)
	//
	REBSER *input_ser = NULL;
	REBSER *output_ser = NULL;
	REBSER *err_ser = NULL;

	// Pointers to the string data buffers corresponding to input/output/err,
	// which may be the data of the expanded path series, the data inside
	// of a STRING!, or NULL if NONE! or default of INHERIT_TYPE
	//
	char *os_input = NULL;
	char *os_output = NULL;
	char *os_err = NULL;

	int input_type = INHERIT_TYPE;
	int output_type = INHERIT_TYPE;
	int err_type = INHERIT_TYPE;

	REBCNT input_len = 0;
	REBCNT output_len = 0;
	REBCNT err_len = 0;

	// Parameter usage may require WAIT mode even if not explicitly requested
	// !!! /WAIT should be default, with /ASYNC (or otherwise) as exception!
	//
	REBOOL flag_wait = FALSE;
	REBOOL flag_console = FALSE;
	REBOOL flag_shell = FALSE;
	REBOOL flag_info = FALSE;

	int exit_code = 0;

	Check_Security(SYM_CALL, POL_EXEC, arg);

	if (D_REF(2)) flag_wait = TRUE;
	if (D_REF(3)) flag_console = TRUE;
	if (D_REF(4)) flag_shell = TRUE;
	if (D_REF(5)) flag_info = TRUE;

	// If input_ser is set, it will be both managed and saved
	if (D_REF(6)) {
		REBVAL *param = D_ARG(7);
		input = param;
		if (IS_STRING(param)) {
			input_type = STRING_TYPE;
			os_input = cast(char*, Val_Str_To_OS_Managed(&input_ser, param));
			SAVE_SERIES(input_ser);
			input_len = VAL_LEN(param);
		}
		else if (IS_BINARY(param)) {
			input_type = BINARY_TYPE;
			os_input = s_cast(VAL_BIN_DATA(param));
			input_len = VAL_LEN(param);
		}
		else if (IS_FILE(param)) {
			input_type = FILE_TYPE;
			input_ser = Value_To_OS_Path(param, FALSE);
			MANAGE_SERIES(input_ser);
			SAVE_SERIES(input_ser);
			os_input = s_cast(SERIES_DATA(input_ser));
			input_len = SERIES_TAIL(input_ser);
		}
		else if (IS_NONE(param)) {
			input_type = NONE_TYPE;
		}
		else {
			Trap_Arg_DEAD_END(param);
		}
	}

	// Note that os_output is actually treated as an *input* parameter in the
	// case of a FILE! by OS_CREATE_PROCESS.  (In the other cases it is a
	// pointer of the returned data, which will need to be freed with
	// OS_FREE().)  Hence the case for FILE! is handled specially, where the
	// output_ser must be unsaved instead of OS_FREE()d.
	//
	if (D_REF(8)) {
		REBVAL *param = D_ARG(9);
		output = param;
		if (IS_STRING(param)) {
			output_type = STRING_TYPE;
		}
		else if (IS_BINARY(param)) {
			output_type = BINARY_TYPE;
		}
		else if (IS_FILE(param)) {
			output_type = FILE_TYPE;
			output_ser = Value_To_OS_Path(param, FALSE);
			MANAGE_SERIES(output_ser);
			SAVE_SERIES(output_ser);
			os_output = s_cast(SERIES_DATA(output_ser));
			output_len = SERIES_TAIL(output_ser);
		}
		else if (IS_NONE(param)) {
			output_type = NONE_TYPE;
		}
		else {
			Trap_Arg_DEAD_END(param);
		}
	}

	(void)input; // suppress unused warning but keep variable

	// Error case...same note about FILE! case as with Output case above
	if (D_REF(10)) {
		REBVAL *param = D_ARG(11);
		err = param;
		if (IS_STRING(param)) {
			err_type = STRING_TYPE;
		}
		else if (IS_BINARY(param)) {
			err_type = BINARY_TYPE;
		}
		else if (IS_FILE(param)) {
			err_type = FILE_TYPE;
			err_ser = Value_To_OS_Path(param, FALSE);
			MANAGE_SERIES(err_ser);
			SAVE_SERIES(err_ser);
			os_err = s_cast(SERIES_DATA(err_ser));
			err_len = SERIES_TAIL(err_ser);
		}
		else if (IS_NONE(param)) {
			err_type = NONE_TYPE;
		}
		else {
			Trap_Arg_DEAD_END(param);
		}
	}

	/* I/O redirection implies /wait */
	if (input_type == STRING_TYPE
		|| input_type == BINARY_TYPE
		|| output_type == STRING_TYPE
		|| output_type == BINARY_TYPE
		|| err_type == STRING_TYPE
		|| err_type == BINARY_TYPE) {
		flag_wait = TRUE;
	}

	if (flag_wait) flags |= FLAG_WAIT;
	if (flag_console) flags |= FLAG_CONSOLE;
	if (flag_shell) flags |= FLAG_SHELL;
	if (flag_info) flags |= FLAG_INFO;

	// Translate the first parameter into an `argc` and a pointer array for
	// `argv[]`.  The pointer array is backed by `argv_series` which must
	// be freed after we are done using it.
	//
	if (IS_STRING(arg)) {
		// `call {foo bar}` => execute %"foo bar"

		// !!! Interpreting string case as an invocation of %foo with argument
		// "bar" has been requested and seems more suitable.  Question is
		// whether it should go through the shell parsing to do so.

		cmd = Val_Str_To_OS_Managed(&cmd_ser, arg);
		SAVE_SERIES(cmd_ser);

		argc = 1;
		argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
		argv = cast(const REBCHR**, SERIES_DATA(argv_ser));

		argv[0] = cmd;
		// Already implicitly SAVEd by cmd_ser, no need for argv_saved_sers

		argv[argc] = NULL;
	}
	else if (IS_BLOCK(arg)) {
		// `call ["foo" "bar"]` => execute %foo with arg "bar"

		int i;

		cmd = NULL;
		argc = VAL_LEN(arg);
		if (argc <= 0) {
			Trap_DEAD_END(RE_TOO_SHORT);
		}
		argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
		argv_saved_sers = Make_Series(argc, sizeof(REBSER*), MKS_NONE);
		argv = cast(const REBCHR**, SERIES_DATA(argv_ser));
		for (i = 0; i < argc; i ++) {
			REBVAL *param = VAL_BLK_SKIP(arg, i);
			if (IS_STRING(param)) {
				REBSER *ser;
				argv[i] = Val_Str_To_OS_Managed(&ser, param);
				SAVE_SERIES(ser);
				cast(REBSER**, SERIES_DATA(argv_saved_sers))[i] = ser;
			}
			else if (IS_FILE(param)) {
				REBSER *path = Value_To_OS_Path(param, FALSE);
				argv[i] = cast(REBCHR*, SERIES_DATA(path));

				MANAGE_SERIES(path);
				SAVE_SERIES(path);
				cast(REBSER**, SERIES_DATA(argv_saved_sers))[i] = path;
			}
			else {
				Trap_Arg_DEAD_END(param);
			}
		}
		argv[argc] = NULL;
	}
	else if (IS_FILE(arg)) {
		// `call %"foo bar"` => execute %"foo bar"

		REBSER *path = Value_To_OS_Path(arg, FALSE);

		cmd = NULL;
		argc = 1;
		argv_ser = Make_Series(argc + 1, sizeof(REBCHR*), MKS_NONE);
		argv_saved_sers = Make_Series(argc, sizeof(REBSER*), MKS_NONE);

		argv = cast(const REBCHR**, SERIES_DATA(argv_ser));

		argv[0] = cast(REBCHR*, SERIES_DATA(path));
		SAVE_SERIES(path);
		cast(REBSER**, SERIES_DATA(argv_saved_sers))[0] = path;

		argv[argc] = NULL;
	}
	else {
		Trap_Arg_DEAD_END(arg);
	}

	r = OS_CREATE_PROCESS(
		cmd, argc, argv,
		flags, &pid, &exit_code,
		input_type, os_input, input_len,
		output_type, &os_output, &output_len,
		err_type, &os_err, &err_len
	);

	// Call may not succeed if r != 0, but we still have to run cleanup
	// before reporting any error...
	//
	if (argv_saved_sers) {
		int i = argc;
		assert(argc > 0);
		do {
			// Count down: must unsave the most recently saved series first!
			UNSAVE_SERIES(cast(REBSER**, SERIES_DATA(argv_saved_sers))[i - 1]);
			--i;
		} while (i != 0);
		Free_Series(argv_saved_sers);
	}
	if (cmd_ser) UNSAVE_SERIES(cmd_ser);
	Free_Series(argv_ser); // Unmanaged, so we can free it

	if (output_type == STRING_TYPE) {
		if (output != NULL
			&& output_len > 0) {
			// !!! Somewhat inefficient: should there be Append_OS_Str?
			REBSER *ser = Copy_OS_Str(os_output, output_len);
			Append_String(VAL_SERIES(output), ser, 0, SERIES_TAIL(ser));
			OS_FREE(os_output);
			Free_Series(ser);
		}
	} else if (output_type == BINARY_TYPE) {
		if (output != NULL
			&& output_len > 0) {
			Append_Unencoded_Len(VAL_SERIES(output), os_output, output_len);
			OS_FREE(os_output);
		}
	}

	if (err_type == STRING_TYPE) {
		if (err != NULL
			&& err_len > 0) {
			// !!! Somewhat inefficient: should there be Append_OS_Str?
			REBSER *ser = Copy_OS_Str(os_err, err_len);
			Append_String(VAL_SERIES(err), ser, 0, SERIES_TAIL(ser));
			OS_FREE(os_err);
			Free_Series(ser);
		}
	} else if (err_type == BINARY_TYPE) {
		if (err != NULL
			&& err_len > 0) {
			Append_Unencoded_Len(VAL_SERIES(err), os_err, err_len);
			OS_FREE(os_err);
		}
	}

	// If we used (and possibly created) a series for input/output/err, then
	// that series was managed and saved from GC.  Unsave them now.  Note
	// backwardsness: must unsave the most recently saved series first!!
	//
	if (err_ser) UNSAVE_SERIES(err_ser);
	if (output_ser) UNSAVE_SERIES(output_ser);
	if (input_ser) UNSAVE_SERIES(input_ser);

	if (flag_info) {
		REBSER *obj = Make_Frame(2, TRUE);
		REBVAL *val = Append_Frame(obj, NULL, SYM_ID);
		SET_INTEGER(val, pid);

		if (flag_wait) {
			val = Append_Frame(obj, NULL, SYM_EXIT_CODE);
			SET_INTEGER(val, exit_code);
		}

		Val_Init_Object(D_OUT, obj);
		return R_OUT;
	}

	if (r != 0) {
		Make_OS_Error(D_OUT, r);
		Trap1_DEAD_END(RE_CALL_FAIL, D_OUT);
	}

	// We may have waited even if they didn't ask us to explicitly, but
	// we only return a process ID if /WAIT was not explicitly used
	if (flag_wait)
		SET_INTEGER(D_OUT, exit_code);
	else
		SET_INTEGER(D_OUT, pid);

	return R_OUT;
}


//
//  String_List_To_Block: C
//  
//      Convert a series of null terminated strings to
//      a block of strings separated with '='.
//

static REBSER *String_List_To_Block(REBCHR *str)
{
	REBCNT n;
	REBCNT len = 0;
	REBCHR *start = str;
	REBCHR *eq;
	REBSER *blk;

	while ((n = OS_STRLEN(str))) {
		len++;
		str += n + 1; // next
	}

	blk = Make_Array(len * 2);

	str = start;
	while ((eq = OS_STRCHR(str+1, '=')) && (n = OS_STRLEN(str))) {
		Val_Init_String(Alloc_Tail_Array(blk), Copy_OS_Str(str, eq - str));
		Val_Init_String(
			Alloc_Tail_Array(blk), Copy_OS_Str(eq + 1, n - (eq - str) - 1)
		);
		str += n + 1; // next
	}

	Block_As_Map(blk);

	return blk;
}


//
//  Block_To_String_List: C
//  
//      Convert block of values to a string that holds
//      a series of null terminated strings, followed
//      by a final terminating string.
//

REBSER *Block_To_String_List(REBVAL *blk)
{
	REBVAL *value;

	REB_MOLD mo;
	CLEARS(&mo);
	Reset_Mold(&mo);

	for (value = VAL_BLK_DATA(blk); NOT_END(value); value++) {
		Mold_Value(&mo, value, 0);
		Append_Codepoint_Raw(mo.series, 0);
	}
	Append_Codepoint_Raw(mo.series, 0);

	return Copy_Sequence(mo.series); // Unicode
}


//
//  File_List_To_Block: C
//  
//      Convert file directory and file name list to block.
//

static REBSER *File_List_To_Block(const REBCHR *str)
{
	REBCNT n;
	REBCNT len = 0;
	const REBCHR *start = str;
	REBSER *blk;
	REBSER *dir;

	while ((n = OS_STRLEN(str))) {
		len++;
		str += n + 1; // next
	}

	blk = Make_Array(len);

	// First is a dir path or full file path:
	str = start;
	n = OS_STRLEN(str);

	if (len == 1) {  // First is full file path
		dir = To_REBOL_Path(str, n, OS_WIDE, 0);
		Val_Init_File(Alloc_Tail_Array(blk), dir);
	}
	else {  // First is dir path for the rest of the files
#ifdef TO_WINDOWS /* directory followed by files */
		assert(sizeof(wchar_t) == sizeof(REBCHR));
		dir = To_REBOL_Path(str, n, -1, TRUE);
		str += n + 1; // next
		len = dir->tail;
        while ((n = OS_STRLEN(str))) {
			dir->tail = len;
			Append_Uni_Uni(dir, cast(const REBUNI*, str), n);
			Val_Init_File(Alloc_Tail_Array(blk), Copy_String(dir, 0, -1));
			str += n + 1; // next
		}
#else /* absolute pathes already */
		str += n + 1;
		while ((n = OS_STRLEN(str))) {
			dir = To_REBOL_Path(str, n, OS_WIDE, FALSE);
			Val_Init_File(Alloc_Tail_Array(blk), Copy_String(dir, 0, -1));
			str += n + 1; // next
		}
#endif
	}

	return blk;
}


//
//  request-file: native [
//      {Asks user to select a file and returns full file path (or block of paths).}
//      /save "File save mode"
//      /multi {Allows multiple file selection, returned as a block}
//      /file name [file!] "Default file name or directory"
//      /title text [string!] "Window title"
//      /filter list [block!] "Block of filters (filter-name filter)"
//  ]
//

REBNATIVE(request_file)
{
	REBSER *ser;
	REBINT n;

	// !!! This routine used to have an ENABLE_GC and DISABLE_GC
	// reference.  It is not clear what that was protecting, but
	// this code should be reviewed with GC "torture mode", and
	// if any values are being created which cannot be GC'd then
	// they should be created without handing them over to GC with
	// MANAGE_SERIES() instead.

	REBRFR fr;
	CLEARS(&fr);
	fr.files = OS_ALLOC_ARRAY(REBCHR, MAX_FILE_REQ_BUF);
	fr.len = MAX_FILE_REQ_BUF/sizeof(REBCHR) - 2;
	fr.files[0] = OS_MAKE_CH('\0');

	if (D_REF(ARG_REQUEST_FILE_SAVE)) SET_FLAG(fr.flags, FRF_SAVE);
	if (D_REF(ARG_REQUEST_FILE_MULTI)) SET_FLAG(fr.flags, FRF_MULTI);

	if (D_REF(ARG_REQUEST_FILE_FILE)) {
		ser = Value_To_OS_Path(D_ARG(ARG_REQUEST_FILE_NAME), TRUE);
		fr.dir = cast(REBCHR*, ser->data);
		n = ser->tail;
		if (OS_CH_VALUE(fr.dir[n-1]) != OS_DIR_SEP) {
			if (n+2 > fr.len) n = fr.len - 2;
			OS_STRNCPY(cast(REBCHR*, fr.files), cast(REBCHR*, ser->data), n);
			fr.files[n] = OS_MAKE_CH('\0');
		}
	}

	if (D_REF(ARG_REQUEST_FILE_FILTER)) {
		ser = Block_To_String_List(D_ARG(ARG_REQUEST_FILE_LIST));
		fr.filter = cast(REBCHR*, ser->data);
	}

	if (D_REF(ARG_REQUEST_FILE_TITLE)) {
		// !!! By passing NULL we don't get backing series to protect!
		fr.title = Val_Str_To_OS_Managed(NULL, D_ARG(ARG_REQUEST_FILE_TEXT));
	}

	if (OS_REQUEST_FILE(&fr)) {
		if (GET_FLAG(fr.flags, FRF_MULTI)) {
			ser = File_List_To_Block(fr.files);
			Val_Init_Block(D_OUT, ser);
		}
		else {
			ser = To_REBOL_Path(fr.files, OS_STRLEN(fr.files), OS_WIDE, 0);
			Val_Init_File(D_OUT, ser);
		}
	} else
		ser = 0;

	OS_FREE(fr.files);

	return ser ? R_OUT : R_NONE;
}


//
//  get-env: native [
//      {Returns the value of an OS environment variable (for current process).}
//      var [any-string! any-word!]
//  ]
//

REBNATIVE(get_env)
{
	REBCHR *cmd;
	REBINT lenplus;
	REBCHR *buf;
	REBVAL *arg = D_ARG(1);

	Check_Security(SYM_ENVR, POL_READ, arg);

	if (ANY_WORD(arg)) Val_Init_String(arg, Copy_Form_Value(arg, 0));

	// !!! By passing NULL we don't get backing series to protect!
	cmd = Val_Str_To_OS_Managed(NULL, arg);

	lenplus = OS_GET_ENV(cmd, NULL, 0);
	if (lenplus == 0) return R_NONE;
	if (lenplus < 0) return R_UNSET;

	// Two copies...is there a better way?
	buf = ALLOC_ARRAY(REBCHR, lenplus);
	OS_GET_ENV(cmd, buf, lenplus);
	Val_Init_String(D_OUT, Copy_OS_Str(buf, lenplus - 1));
	FREE_ARRAY(REBCHR, lenplus, buf);

	return R_OUT;
}


//
//  set-env: native [
//      {Sets the value of an operating system environment variable (for current process).}
//      var [any-string! any-word!] "Variable to set"
//      value [string! none!] "Value to set, or NONE to unset it"
//  ]
//

REBNATIVE(set_env)
{
	REBCHR *cmd;
	REBVAL *arg1 = D_ARG(1);
	REBVAL *arg2 = D_ARG(2);
	REBOOL success;

	Check_Security(SYM_ENVR, POL_WRITE, arg1);

	if (ANY_WORD(arg1)) Val_Init_String(arg1, Copy_Form_Value(arg1, 0));

	// !!! By passing NULL we don't get backing series to protect!
	cmd = Val_Str_To_OS_Managed(NULL, arg1);

	if (ANY_STR(arg2)) {
		// !!! By passing NULL we don't get backing series to protect!
		REBCHR *value = Val_Str_To_OS_Managed(NULL, arg2);
		success = OS_SET_ENV(cmd, value);
		if (success) {
			// What function could reuse arg2 as-is?
			Val_Init_String(D_OUT, Copy_OS_Str(value, OS_STRLEN(value)));
			return R_OUT;
		}
		return R_UNSET;
	}

	if (IS_NONE(arg2)) {
		success = OS_SET_ENV(cmd, 0);
		if (success)
			return R_NONE;
		return R_UNSET;
	}

	// is there any checking that native interface has not changed
	// out from under the expectations of the code?

	return R_UNSET;
}


//
//  list-env: native [
//      {Returns a map of OS environment variables (for current process).}
//  ]
//

REBNATIVE(list_env)
{
	REBCHR *result = OS_LIST_ENV();

	Val_Init_Map(D_OUT, String_List_To_Block(result));

	return R_OUT;
}

/***********************************************************************
**
*/	REBNATIVE(access_os)
/*
 *  access-os word value
 * /set
 */
{
#define OS_ENA	 -1
#define OS_EINVAL -2
#define OS_EPERM -3
#define OS_ESRCH -4

	REBVAL *field = D_ARG(1);
	REBOOL set = D_REF(2);
	REBVAL *val = D_ARG(3);

	switch (VAL_WORD_CANON(field)) {
		case SYM_UID:
			if (set) {
				if (IS_INTEGER(val)) {
					REBINT ret = OS_SET_UID(VAL_INT32(val));
					if (ret < 0) {
						switch (ret) {
							case OS_ENA:
								return R_NONE;
							case OS_EPERM:
								Trap_DEAD_END(RE_PERMISSION_DENIED);
								break;
							case OS_EINVAL:
								Trap_Arg_DEAD_END(val);
								break;
							default:
								Trap_Arg_DEAD_END(val);
								break;
						}
					} else {
						SET_INTEGER(D_OUT, ret);
						return R_OUT;
					}
				} else {
					Trap_Arg_DEAD_END(val);
				}
			} else {
				REBINT ret = OS_GET_UID();
				if (ret < 0) {
					return R_NONE;
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			}
			break;
		case SYM_GID:
			if (set) {
				if (IS_INTEGER(val)) {
					REBINT ret = OS_SET_GID(VAL_INT32(val));
					if (ret < 0) {
						switch (ret) {
							case OS_ENA:
								return R_NONE;
							case OS_EPERM:
								Trap_DEAD_END(RE_PERMISSION_DENIED);
								break;
							case OS_EINVAL:
								Trap_Arg_DEAD_END(val);
								break;
							default:
								Trap_Arg_DEAD_END(val);
								break;
						}
					} else {
						SET_INTEGER(D_OUT, ret);
						return R_OUT;
					}
				} else {
					Trap_Arg_DEAD_END(val);
				}
			} else {
				REBINT ret = OS_GET_GID();
				if (ret < 0) {
					return R_NONE;
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			}
			break;
		case SYM_EUID:
			if (set) {
				if (IS_INTEGER(val)) {
					REBINT ret = OS_SET_EUID(VAL_INT32(val));
					if (ret < 0) {
						switch (ret) {
							case OS_ENA:
								return R_NONE;
							case OS_EPERM:
								Trap_DEAD_END(RE_PERMISSION_DENIED);
								break;
							case OS_EINVAL:
								Trap_Arg_DEAD_END(val);
								break;
							default:
								Trap_Arg_DEAD_END(val);
								break;
						}
					} else {
						SET_INTEGER(D_OUT, ret);
						return R_OUT;
					}
				} else {
					Trap_Arg_DEAD_END(val);
				}
			} else {
				REBINT ret = OS_GET_EUID();
				if (ret < 0) {
					return R_NONE;
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			}
			break;
		case SYM_EGID:
			if (set) {
				if (IS_INTEGER(val)) {
					REBINT ret = OS_SET_EGID(VAL_INT32(val));
					if (ret < 0) {
						switch (ret) {
							case OS_ENA:
								return R_NONE;
							case OS_EPERM:
								Trap_DEAD_END(RE_PERMISSION_DENIED);
								break;
							case OS_EINVAL:
								Trap_Arg_DEAD_END(val);
								break;
							default:
								Trap_Arg_DEAD_END(val);
								break;
						}
					} else {
						SET_INTEGER(D_OUT, ret);
						return R_OUT;
					}
				} else {
					Trap_Arg_DEAD_END(val);
				}
			} else {
				REBINT ret = OS_GET_EGID();
				if (ret < 0) {
					return R_NONE;
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			}
			break;
		case SYM_PID:
			if (set) {
				REBINT ret = 0;
				REBVAL *pid = val;
				REBVAL *arg = val;
				if (IS_INTEGER(val)) {
					ret = OS_KILL(VAL_INT32(pid));
				} else if (IS_BLOCK(val)) {
					REBVAL *sig = NULL;

					if (VAL_LEN(val) != 2) {
						Trap_Arg_DEAD_END(val);
					}
					pid = VAL_BLK_SKIP(val, 0);
					sig = VAL_BLK_SKIP(val, 1);
					if (!IS_INTEGER(pid)) {
						Trap_Arg_DEAD_END(pid);
					}
					if (!IS_INTEGER(sig)) {
						Trap_Arg_DEAD_END(sig);
					}
					ret = OS_SEND_SIGNAL(VAL_INT32(pid), VAL_INT32(sig));
					arg = sig;
				} else {
					Trap_Arg_DEAD_END(val);
				}

				if (ret < 0) {
					switch (ret) {
						case OS_ENA:
							return R_NONE;
						case OS_EPERM:
							Trap_DEAD_END(RE_PERMISSION_DENIED);
							break;
						case OS_EINVAL:
							Trap_Arg_DEAD_END(arg);
							break;
						case OS_ESRCH:
							Trap1_DEAD_END(RE_PROCESS_NOT_FOUND, pid);
							break;
						default:
							Trap_Arg_DEAD_END(val);
							break;
					}
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			} else {
				REBINT ret = OS_GET_PID();
				if (ret < 0) {
					return R_NONE;
				} else {
					SET_INTEGER(D_OUT, ret);
					return R_OUT;
				}
			}
			break;
		default:
			Trap_Arg_DEAD_END(field);
	}
}
