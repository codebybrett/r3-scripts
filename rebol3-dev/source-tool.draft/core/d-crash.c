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
**  Module:  d-crash.c
**  Summary: low level crash output
**  Section: debug
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#define	PANIC_BUF_SIZE 512	// space for crash print string

enum Panic_Msg_Nums {
	// Must align with Panic_Msgs[] array.
	CM_ERROR,
	CM_BOOT,
	CM_INTERNAL,
	CM_DATATYPE,
	CM_DEBUG,
	CM_CONTACT
};


//
//  Panic_Core: C
//  
//      Print a failure message and abort.
//  
//      LATIN1 ONLY!! (For now)
//  
//      The error is identified by id number, which can reference an
//      error message string in the boot strings block.
//  
//      Note that lower level error messages should not attempt to
//      use the %r (mold value) format (uses higher level functions).
//  
//      See panics.h for list of crash errors.
//

/***
** coverity[+kill]
*/
void Panic_Core(REBINT id, ...)
{
	va_list args;
	char buf[PANIC_BUF_SIZE];
	const char *msg;
	REBINT n = 0;

	va_start(args, id);

	// We are crashing so something is internally wrong...a legitimate
	// time to be disabling the garbage collector.
	//
	// !!! But should we be doing FORMing etc. to make a message from
	// varargs or just spitting out the crash in a less risky way?  Is
	// Panic overdesigned for its purpose?
	GC_Disabled++;

	if (Reb_Opts->crash_dump) {
		Dump_Info();
		Dump_Stack(0, 0);
	}

	// "REBOL PANIC #nnn:"
	strncpy(buf, Panic_Msgs[CM_ERROR], PANIC_BUF_SIZE);
	buf[PANIC_BUF_SIZE - 1] = '\0';
	strncat(buf, " #", PANIC_BUF_SIZE);
	Form_Int(b_cast(buf + strlen(buf)), id);
	strncat(buf, ": ", PANIC_BUF_SIZE);

	// "REBOL PANIC #nnn: put error message here"
	// The first few error types only print general error message.
	// Those errors > RP_STR_BASE have specific error messages (from boot.r).
	if      (id < RP_BOOT_DATA) n = CM_DEBUG;
	else if (id < RP_INTERNAL) n = CM_BOOT;
	else if (id < RP_DATATYPE)  n = CM_INTERNAL;
	else if (id < RP_STR_BASE) n = CM_DATATYPE;
	else if (id > RP_STR_BASE + RS_MAX - RS_ERROR) n = CM_DEBUG;

	// Use the above string or the boot string for the error (in boot.r):
	msg = n >= 0 ? Panic_Msgs[n] : cs_cast(BOOT_STR(RS_ERROR, id - RP_STR_BASE - 1));
	Form_Var_Args(
		b_cast(buf + strlen(buf)), PANIC_BUF_SIZE - 1 - strlen(buf), msg, &args
	);

	va_end(args);

	strncat(buf, Panic_Msgs[CM_CONTACT], PANIC_BUF_SIZE - 1);

	// Convert to OS-specific char-type:
#ifdef disable_for_now //OS_WIDE_CHAR   /// win98 does not support it
	{
		REBCHR s1[512];
		REBCHR s2[2000];

		n = OS_STRNCPY(s1, Panic_Msgs[CM_ERROR], LEN_BYTES(Panic_Msgs[CM_ERROR]));
		if (n > 0) s1[n] = 0; // terminate
		else OS_EXIT(200); // bad conversion

		n = OS_STRNCPY(s2, buf, LEN_BYTES(buf));
		if (n > 0) s2[n] = 0;
		else OS_EXIT(200);

		OS_CRASH(s1, s2);
	}
#else
	OS_CRASH(cb_cast(Panic_Msgs[CM_ERROR]), cb_cast(buf));
#endif
}

//
//  NA: C
//  
//      Feature not available.
//

void NA(void)
{
	Panic(RP_NA);
}
