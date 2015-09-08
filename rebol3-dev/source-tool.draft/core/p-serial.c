/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2013 REBOL Technologies
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
**  Module:  p-serial.c
**  Summary: serial port interface
**  Section: ports
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "reb-net.h"
#include "reb-evtypes.h"

#define MAX_SERIAL_DEV_PATH 128

//
//  Serial_Actor: C
//
static REB_R Serial_Actor(struct Reb_Call *call_, REBSER *port, REBCNT action)
{
	REBREQ *req;	// IO request
	REBVAL *spec;	// port spec
	REBVAL *arg;	// action argument value
	REBVAL *val;	// e.g. port number value
	REBINT result;	// IO result
	REBCNT refs;	// refinement argument flags
	REBCNT len;		// generic length
	REBSER *ser;	// simplifier
	REBVAL *path;

	Validate_Port(port, action);

	*D_OUT = *D_ARG(1);

	// Validate PORT fields:
	spec = OFV(port, STD_PORT_SPEC);
	if (!IS_OBJECT(spec)) Trap_DEAD_END(RE_INVALID_PORT);
	path = Obj_Value(spec, STD_PORT_SPEC_HEAD_REF);
	if (!path) Trap1_DEAD_END(RE_INVALID_SPEC, spec);

	//if (!IS_FILE(path)) Trap1_DEAD_END(RE_INVALID_SPEC, path);

	req = cast(REBREQ*, Use_Port_State(port, RDI_SERIAL, sizeof(*req)));

	// Actions for an unopened serial port:
	if (!IS_OPEN(req)) {

		switch (action) {

		case A_OPEN:
			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PATH);  //Should Obj_Value really return a char* ?
			if (! (IS_FILE(arg) || IS_STRING(arg) || IS_BINARY(arg))) {
				Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
			}
			req->special.serial.path = ALLOC_ARRAY(REBCHR, MAX_SERIAL_DEV_PATH);
			OS_STRNCPY(
				req->special.serial.path,
				// !!! This is assuming VAL_DATA contains native chars.
				// Should it? (2 bytes on windows, 1 byte on linux/mac)
				cast(REBCHR*, VAL_DATA(arg)),
				MAX_SERIAL_DEV_PATH
			);
			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_SPEED);
			if (! IS_INTEGER(arg)) {
				Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
			}
			req->special.serial.baud = VAL_INT32(arg);
			//Secure_Port(SYM_SERIAL, ???, path, ser);
			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_DATA_SIZE);
			if (!IS_INTEGER(arg)
				|| VAL_INT64(arg) < 5
				|| VAL_INT64(arg) > 8) {
				Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
			}
			req->special.serial.data_bits = VAL_INT32(arg);

			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_STOP_BITS);
			if (!IS_INTEGER(arg)
				|| VAL_INT64(arg) < 1
				|| VAL_INT64(arg) > 2) {
				Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
			}
			req->special.serial.stop_bits = VAL_INT32(arg);

			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_PARITY);
			if (IS_NONE(arg)) {
				req->special.serial.parity = SERIAL_PARITY_NONE;
			} else {
				if (!IS_WORD(arg)) {
					Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
				}
				switch (VAL_WORD_CANON(arg)) {
					case SYM_ODD:
						req->special.serial.parity = SERIAL_PARITY_ODD;
						break;
					case SYM_EVEN:
						req->special.serial.parity = SERIAL_PARITY_EVEN;
						break;
					default:
						Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
				}
			}

			arg = Obj_Value(spec, STD_PORT_SPEC_SERIAL_FLOW_CONTROL);
			if (IS_NONE(arg)) {
				req->special.serial.flow_control = SERIAL_FLOW_CONTROL_NONE;
			} else {
				if (!IS_WORD(arg)) {
					Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
				}
				switch (VAL_WORD_CANON(arg)) {
					case SYM_HARDWARE:
						req->special.serial.flow_control = SERIAL_FLOW_CONTROL_HARDWARE;
						break;
					case SYM_SOFTWARE:
						req->special.serial.flow_control = SERIAL_FLOW_CONTROL_SOFTWARE;
						break;
					default:
						Trap1_DEAD_END(RE_INVALID_PORT_ARG, arg);
				}
			}

			if (OS_DO_DEVICE(req, RDC_OPEN)) Trap_Port_DEAD_END(RE_CANNOT_OPEN, port, -12);
			SET_OPEN(req);
			return R_OUT;

		case A_CLOSE:
			return R_OUT;

		case A_OPENQ:
			return R_FALSE;

		default:
			Trap_Port_DEAD_END(RE_NOT_OPEN, port, -12);
		}
	}

	// Actions for an open socket:
	switch (action) {

	case A_READ:
		refs = Find_Refines(call_, ALL_READ_REFS);

		// Setup the read buffer (allocate a buffer if needed):
		arg = OFV(port, STD_PORT_DATA);
		if (!IS_STRING(arg) && !IS_BINARY(arg)) {
			Val_Init_Binary(arg, Make_Binary(32000));
		}
		ser = VAL_SERIES(arg);
		req->length = SERIES_AVAIL(ser); // space available
		if (req->length < 32000/2) Extend_Series(ser, 32000);
		req->length = SERIES_AVAIL(ser);
		req->common.data = STR_TAIL(ser); // write at tail
		//if (SERIES_TAIL(ser) == 0)
		req->actual = 0;  // Actual for THIS read, not for total.
#ifdef DEBUG_SERIAL
		printf("(max read length %d)", req->length);
#endif
		result = OS_DO_DEVICE(req, RDC_READ); // recv can happen immediately
		if (result < 0) Trap_Port_DEAD_END(RE_READ_ERROR, port, req->error);
#ifdef DEBUG_SERIAL
		for (len = 0; len < req->actual; len++) {
			if (len % 16 == 0) printf("\n");
			printf("%02x ", req->common.data[len]);
		}
		printf("\n");
#endif
		*D_OUT = *arg;
		return R_OUT;

	case A_WRITE:
		refs = Find_Refines(call_, ALL_WRITE_REFS);

		// Determine length. Clip /PART to size of string if needed.
		spec = D_ARG(2);
		len = VAL_LEN(spec);
		if (refs & AM_WRITE_PART) {
			REBCNT n = Int32s(D_ARG(ARG_WRITE_LIMIT), 0);
			if (n <= len) len = n;
		}

		// Setup the write:
		*OFV(port, STD_PORT_DATA) = *spec;	// keep it GC safe
		req->length = len;
		req->common.data = VAL_BIN_DATA(spec);
		req->actual = 0;

		//Print("(write length %d)", len);
		result = OS_DO_DEVICE(req, RDC_WRITE); // send can happen immediately
		if (result < 0) Trap_Port_DEAD_END(RE_WRITE_ERROR, port, req->error);
		break;
	case A_UPDATE:
		// Update the port object after a READ or WRITE operation.
		// This is normally called by the WAKE-UP function.
		arg = OFV(port, STD_PORT_DATA);
		if (req->command == RDC_READ) {
			if (ANY_BINSTR(arg)) VAL_TAIL(arg) += req->actual;
		}
		else if (req->command == RDC_WRITE) {
			SET_NONE(arg);  // Write is done.
		}
		return R_NONE;
	case A_OPENQ:
		return R_TRUE;

	case A_CLOSE:
		if (IS_OPEN(req)) {
			OS_DO_DEVICE(req, RDC_CLOSE);
			SET_CLOSED(req);
		}
		break;

	default:
		Trap_Action_DEAD_END(REB_PORT, action);
	}

	return R_OUT;
}


//
//  Init_Serial_Scheme: C
//
void Init_Serial_Scheme(void)
{
	Register_Scheme(SYM_SERIAL, 0, Serial_Actor);
}
