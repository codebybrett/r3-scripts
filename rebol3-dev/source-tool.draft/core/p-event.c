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
**  Module:  p-event.c
**  Summary: event port interface
**  Section: ports
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/
/*
  Basics:

	  Ports use requests to control devices.
	  Devices do their best, and return when no more is possible.
	  Progs call WAIT to check if devices have changed.
	  If devices changed, modifies request, and sends event.
	  If no devices changed, timeout happens.
	  On REBOL side, we scan event queue.
	  If we find an event, we call its port/awake function.

	  Different cases exist:

	  1. wait for time only

	  2. wait for ports and time.  Need a master wait list to
		 merge with the list provided this function.

	  3. wait for windows to close - check each time we process
		 a close event.

	  4. what to do on console ESCAPE interrupt? Can use catch it?

	  5. how dow we relate events back to their ports?

	  6. async callbacks
*/

#include "sys-core.h"

REBREQ *req;		//!!! move this global

#define EVENTS_LIMIT 0xFFFF //64k
#define EVENTS_CHUNK 128

//
//  Append_Event: C
//  
//      Append an event to the end of the current event port queue.
//      Return a pointer to the event value.
//  
//      Note: this function may be called from out of environment,
//      so do NOT extend the event queue here. If it does not have
//      space, return 0. (Should it overwrite or wrap???)
//

REBVAL *Append_Event(void)
{
	REBVAL *port;
	REBVAL *value;
	REBVAL *state;

	port = Get_System(SYS_PORTS, PORTS_SYSTEM);
	if (!IS_PORT(port)) return 0; // verify it is a port object

	// Get queue block:
	state = VAL_OBJ_VALUE(port, STD_PORT_STATE);
	if (!IS_BLOCK(state)) return 0;

	// Append to tail if room:
	if (SERIES_FULL(VAL_SERIES(state))) {
		if (VAL_TAIL(state) > EVENTS_LIMIT) {
			Panic_DEAD_END(RP_MAX_EVENTS);
		} else {
			Extend_Series(VAL_SERIES(state), EVENTS_CHUNK);
			//RL_Print("event queue increased to :%d\n", SERIES_REST(VAL_SERIES(state)));
		}
	}
	VAL_TAIL(state)++;
	value = VAL_BLK_TAIL(state);
	SET_END(value);
	value--;
	SET_NONE(value);

	//Dump_Series(VAL_SERIES(state), "state");
	//Print("Tail: %d %d", VAL_TAIL(state), nn++);

	return value;
}
/***********************************************************************
**
*/	REBVAL *Find_Last_Event (REBINT model, REBINT type)
/*
**		Find the last event in the queue by the model
**		Check its type, if it matches, then return the event or NULL
**
**
***********************************************************************/
{
	REBVAL *port;
	REBVAL *value;
	REBVAL *state;

	port = Get_System(SYS_PORTS, PORTS_SYSTEM);
	if (!IS_PORT(port)) return NULL; // verify it is a port object

	// Get queue block:
	state = VAL_OBJ_VALUE(port, STD_PORT_STATE);
	if (!IS_BLOCK(state)) return NULL;

	value = VAL_BLK_TAIL(state) - 1;
	for (; value >= VAL_BLK_HEAD(state); --value) {
		if (VAL_EVENT_MODEL(value) == model) {
			if (VAL_EVENT_TYPE(value) == type) {
				return value;
			} else {
				return NULL;
			}
		}
	}

	return NULL;
}

//
//  Event_Actor: C
//  
//      Internal port handler for events.
//

static REB_R Event_Actor(struct Reb_Call *call_, REBSER *port, REBCNT action)
{
	REBVAL *spec;
	REBVAL *state;
	REB_R result;
	REBVAL *arg;
	REBVAL save_port;

	Validate_Port(port, action);

	arg = DS_ARGC > 1 ? D_ARG(2) : NULL;
	*D_OUT = *D_ARG(1);

	// Validate and fetch relevant PORT fields:
	state = BLK_SKIP(port, STD_PORT_STATE);
	spec  = BLK_SKIP(port, STD_PORT_SPEC);
	if (!IS_OBJECT(spec)) Trap1_DEAD_END(RE_INVALID_SPEC, spec);

	// Get or setup internal state data:
	if (!IS_BLOCK(state)) Val_Init_Block(state, Make_Array(EVENTS_CHUNK - 1));

	switch (action) {

	case A_UPDATE:
		return R_NONE;

	// Normal block actions done on events:
	case A_POKE:
		if (!IS_EVENT(D_ARG(3))) Trap_Arg_DEAD_END(D_ARG(3));
		goto act_blk;
	case A_INSERT:
	case A_APPEND:
	//case A_PATH:		// not allowed: port/foo is port object field access
	//case A_PATH_SET:	// not allowed: above
		if (!IS_EVENT(arg)) Trap_Arg_DEAD_END(arg);
	case A_PICK:
act_blk:
		save_port = *D_ARG(1); // save for return
		*D_ARG(1) = *state;
		result = T_Array(call_, action);
		SET_SIGNAL(SIG_EVENT_PORT);
		if (action == A_INSERT || action == A_APPEND || action == A_REMOVE) {
			*D_OUT = save_port;
			break;
		}
		return result; // return condition

	case A_CLEAR:
		VAL_TAIL(state) = 0;
		VAL_BLK_TERM(state);
		CLR_SIGNAL(SIG_EVENT_PORT);
		break;

	case A_LENGTH:
		SET_INTEGER(D_OUT, VAL_TAIL(state));
		break;

	case A_OPEN:
		if (!req) { //!!!
			req = OS_MAKE_DEVREQ(RDI_EVENT);
			if (req) {
				SET_OPEN(req);
				OS_DO_DEVICE(req, RDC_CONNECT);		// stays queued
			}
		}
		break;

	case A_CLOSE:
		OS_ABORT_DEVICE(req);
		OS_DO_DEVICE(req, RDC_CLOSE);
		// free req!!!
		SET_CLOSED(req);
		req = 0;
		break;

	case A_FIND: // add it

	default:
		Trap_Action_DEAD_END(REB_PORT, action);
	}

	return R_OUT;
}


//
//  Init_Event_Scheme: C
//

void Init_Event_Scheme(void)
{
	req = 0; // move to port struct
	Register_Scheme(SYM_SYSTEM, 0, Event_Actor);
	Register_Scheme(SYM_EVENT, 0, Event_Actor);
	Register_Scheme(SYM_CALLBACK, 0, Event_Actor);
}
