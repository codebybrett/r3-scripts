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
**  Module:  p-dir.c
**  Summary: file directory port interface
**  Section: ports
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

// Special policy: Win32 does not wanting tail slash for dir info
#define REMOVE_TAIL_SLASH (1<<10)


//
//  Read_Dir: C
//  
//      Provide option to get file info too.
//      Provide option to prepend dir path.
//      Provide option to use wildcards.
//

static int Read_Dir(REBREQ *dir, REBSER *files)
{
	REBINT result;
	REBCNT len;
	REBSER *fname;
	REBSER *name;
	REBREQ file;

	RESET_TAIL(files);
	CLEARS(&file);

	// Temporary filename storage:
	fname = BUF_OS_STR;
	file.special.file.path = cast(REBCHR*, Reset_Buffer(fname, MAX_FILE_NAME));

	SET_FLAG(dir->modes, RFM_DIR);

	dir->common.data = cast(REBYTE*, &file);

	while ((result = OS_DO_DEVICE(dir, RDC_READ)) == 0 && !GET_FLAG(dir->flags, RRF_DONE)) {
		len = OS_STRLEN(file.special.file.path);
		if (GET_FLAG(file.modes, RFM_DIR)) len++;
		name = Copy_OS_Str(file.special.file.path, len);
		if (GET_FLAG(file.modes, RFM_DIR))
			SET_ANY_CHAR(name, name->tail-1, '/');
		Val_Init_File(Alloc_Tail_Array(files), name);
	}

	if (result < 0 && dir->error != -RFE_OPEN_FAIL
		&& (
			OS_STRCHR(dir->special.file.path, '*')
			|| OS_STRCHR(dir->special.file.path, '?')
		)
	) {
		result = 0;  // no matches found, but not an error
	}

	return result;
}


//
//  Init_Dir_Path: C
//  
//      Convert REBOL dir path to file system path.
//      On Windows, we will also need to append a * if necessary.
//  
//  ARGS:
//      Wild:
//          0 - no wild cards, path must end in / else error
//          1 - accept wild cards * and ?, and * if need
//         -1 - not wild, if path does not end in /, add it
//

static void Init_Dir_Path(REBREQ *dir, REBVAL *path, REBINT wild, REBCNT policy)
{
	REBINT len;
	REBSER *ser;
	//REBYTE *flags;

	SET_FLAG(dir->modes, RFM_DIR);

	// We depend on To_Local_Path giving us 2 extra chars for / and *
	ser = Value_To_OS_Path(path, TRUE);
	len = ser->tail;
	dir->special.file.path = cast(REBCHR*, ser->data);

	Secure_Port(SYM_FILE, dir, path, ser);

	if (len == 1 && OS_CH_EQUAL(dir->special.file.path[0], '.')) {
		if (wild > 0) {
			dir->special.file.path[0] = OS_MAKE_CH('*');
			dir->special.file.path[1] = OS_MAKE_CH('\0');
		}
	}
	else if (
		len == 2
		&& OS_CH_EQUAL(dir->special.file.path[0], '.')
		&& OS_CH_EQUAL(dir->special.file.path[1], '.')
	) {
		// Insert * if needed:
		if (wild > 0) {
			dir->special.file.path[len++] = OS_MAKE_CH('/');
			dir->special.file.path[len++] = OS_MAKE_CH('*');
			dir->special.file.path[len] = OS_MAKE_CH('\0');
		}
	}
	else if (
		OS_CH_EQUAL(dir->special.file.path[len-1], '/')
		|| OS_CH_EQUAL(dir->special.file.path[len-1], '\\')
	) {
		if (policy & REMOVE_TAIL_SLASH) {
			dir->special.file.path[len-1] = OS_MAKE_CH('\0');
		}
		else {
			// Insert * if needed:
			if (wild > 0) {
				dir->special.file.path[len++] = OS_MAKE_CH('*');
				dir->special.file.path[len] = OS_MAKE_CH('\0');
			}
		}
	} else {
		// Path did not end with /, so we better be wild:
		if (wild == 0) {
			///OS_FREE(dir->special.file.path);
			Trap1(RE_BAD_FILE_PATH, path);
		}
		else if (wild < 0) {
			dir->special.file.path[len++] = OS_MAKE_CH(OS_DIR_SEP);
			dir->special.file.path[len] = OS_MAKE_CH('\0');
		}
	}
}


//
//  Dir_Actor: C
//  
//      Internal port handler for file directories.
//

static REB_R Dir_Actor(struct Reb_Call *call_, REBSER *port, REBCNT action)
{
	REBVAL *spec;
	REBVAL *path;
	REBVAL *state;
	REBREQ dir;
	REBCNT args = 0;
	REBINT result;
	REBCNT len;
	//REBYTE *flags;

	Validate_Port(port, action);

	*D_OUT = *D_ARG(1);
	CLEARS(&dir);

	// Validate and fetch relevant PORT fields:
	spec  = BLK_SKIP(port, STD_PORT_SPEC);
	if (!IS_OBJECT(spec)) Trap1_DEAD_END(RE_INVALID_SPEC, spec);
	path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
	if (!path) Trap1_DEAD_END(RE_INVALID_SPEC, spec);

	if (IS_URL(path)) path = Obj_Value(spec, STD_PORT_SPEC_HEAD_PATH);
	else if (!IS_FILE(path)) Trap1_DEAD_END(RE_INVALID_SPEC, path);

	state = BLK_SKIP(port, STD_PORT_STATE); // if block, then port is open.

	//flags = Security_Policy(SYM_FILE, path);

	// Get or setup internal state data:
	dir.port = port;
	dir.device = RDI_FILE;

	switch (action) {

	case A_READ:
		//Trap_Security(flags[POL_READ], POL_READ, path);
		args = Find_Refines(call_, ALL_READ_REFS);
		if (!IS_BLOCK(state)) {		// !!! ignores /SKIP and /PART, for now
			Init_Dir_Path(&dir, path, 1, POL_READ);
			Val_Init_Block(state, Make_Array(7)); // initial guess
			result = Read_Dir(&dir, VAL_SERIES(state));
			///OS_FREE(dir.file.path);
			if (result < 0) Trap_Port_DEAD_END(RE_CANNOT_OPEN, port, dir.error);
			*D_OUT = *state;
			SET_NONE(state);
		}
		else {
			// !!! This copies the strings in the block, shallowly.  What is
			// the purpose of doing this?  Why copy at all?
			Val_Init_Block(
				D_OUT,
				Copy_Array_Core_Managed(
					VAL_SERIES(state),
					0,
					VAL_BLK_LEN(state),
					FALSE, // !deep
					TS_STRING
				)
			);
		}
		break;

	case A_CREATE:
		//Trap_Security(flags[POL_WRITE], POL_WRITE, path);
		if (IS_BLOCK(state)) Trap1_DEAD_END(RE_ALREADY_OPEN, path); // already open
create:
		Init_Dir_Path(&dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH); // Sets RFM_DIR too
		result = OS_DO_DEVICE(&dir, RDC_CREATE);
		///OS_FREE(dir.file.path);
		if (result < 0) Trap1_DEAD_END(RE_NO_CREATE, path);
		if (action == A_CREATE) {
			// !!! Used to return R_ARG2, but create is single arity.  :-/
			return R_ARG1;
		}
		SET_NONE(state);
		break;

	case A_RENAME:
		if (IS_BLOCK(state)) Trap1_DEAD_END(RE_ALREADY_OPEN, path); // already open
		else {
			REBSER *target;

			Init_Dir_Path(&dir, path, 0, POL_WRITE | REMOVE_TAIL_SLASH); // Sets RFM_DIR too
			// Convert file name to OS format:
			if (!(target = Value_To_OS_Path(D_ARG(2), TRUE))) Trap1_DEAD_END(RE_BAD_FILE_PATH, D_ARG(2));
			dir.common.data = BIN_DATA(target);
			OS_DO_DEVICE(&dir, RDC_RENAME);
			Free_Series(target);
			if (dir.error) Trap1_DEAD_END(RE_NO_RENAME, path);
		}
		break;

	case A_DELETE:
		//Trap_Security(flags[POL_WRITE], POL_WRITE, path);
		SET_NONE(state);
		Init_Dir_Path(&dir, path, 0, POL_WRITE);
		// !!! add *.r deletion
		// !!! add recursive delete (?)
		result = OS_DO_DEVICE(&dir, RDC_DELETE);
		///OS_FREE(dir.file.path);
		if (result < 0) Trap1_DEAD_END(RE_NO_DELETE, path);
		// !!! Returned R_ARG2 before, but there is no second argument :-/
		return R_ARG1;

	case A_OPEN:
		// !! If open fails, what if user does a READ w/o checking for error?
		if (IS_BLOCK(state)) Trap1_DEAD_END(RE_ALREADY_OPEN, path); // already open
		//Trap_Security(flags[POL_READ], POL_READ, path);
		args = Find_Refines(call_, ALL_OPEN_REFS);
		if (args & AM_OPEN_NEW) goto create;
		//if (args & ~AM_OPEN_READ) Trap1_DEAD_END(RE_INVALID_SPEC, path);
		Val_Init_Block(state, Make_Array(7));
		Init_Dir_Path(&dir, path, 1, POL_READ);
		result = Read_Dir(&dir, VAL_SERIES(state));
		///OS_FREE(dir.file.path);
		if (result < 0) Trap_Port_DEAD_END(RE_CANNOT_OPEN, port, dir.error);
		break;

	case A_OPENQ:
		if (IS_BLOCK(state)) return R_TRUE;
		return R_FALSE;

	case A_CLOSE:
		SET_NONE(state);
		break;

	case A_QUERY:
		//Trap_Security(flags[POL_READ], POL_READ, path);
		SET_NONE(state);
		Init_Dir_Path(&dir, path, -1, REMOVE_TAIL_SLASH | POL_READ);
		if (OS_DO_DEVICE(&dir, RDC_QUERY) < 0) return R_NONE;
		Ret_Query_File(port, &dir, D_OUT);
		///OS_FREE(dir.file.path);
		break;

	//-- Port Series Actions (only called if opened as a port)

	case A_LENGTH:
		len = IS_BLOCK(state) ? VAL_BLK_LEN(state) : 0;
		SET_INTEGER(D_OUT, len);
		break;

	default:
		Trap_Action_DEAD_END(REB_PORT, action);
	}

	return R_OUT;
}


//
//  Init_Dir_Scheme: C
//

void Init_Dir_Scheme(void)
{
	Register_Scheme(SYM_DIR, 0, Dir_Actor);
}
