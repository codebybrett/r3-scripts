REBOL [
	Title: "call-server"
	File: %call-server.r
	Author: "Brett Handley"
	Date: 22-Jun-2013
	Purpose: "Provides a way to capture output from console program and send input to interactive console programs."
	License: {

		Copyright 2013 Brett Handley

		Licensed under the Apache License, Version 2.0 (the "License");
		you may not use this file except in compliance with the License.
		You may obtain a copy of the License at

			http://www.apache.org/licenses/LICENSE-2.0

		Unless required by applicable law or agreed to in writing, software
		distributed under the License is distributed on an "AS IS" BASIS,
		WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
		See the License for the specific language governing permissions and
		limitations under the License.
	}
	History: [
		1.2.0 [22-Jun-2013 "Renamed (was interactive-cmd-server.r3.r), fix /nosysinput." "Brett Handley"]
		1.1.0 [21-Jun-2013 "Significant changes. Now useable." "Brett Handley"]
		1.0.0 [13-Jun-2013 "Initial unfinished REBOL 3 Alpha version." "Brett Handley"]
	]
]

; ---------------------------------------------------------------------------------------------------------------------
;
; Purpose/Usage:
;
; ---------------------------------------------------------------------------------------------------------------------
;
;
;	(1) Call a program and return it's output.
;
;		Example: Returning a result from PING:
;	
;			call-output {ping rebol.net}
;
;
;	(2) Allow calling interactive console programs (those that prompt for and process keystrokes) using REBOL.
;
;		Using it is fairly straight forward, make the command server then
;		call /startup /send /get-reponse /shutdown.
;
;		If you need to use /receive then test the connection with /connection?
;		before calling /receive.
;
;		Example: Controlling PSFTP.exe:
;
;			tell-psftp: func [
;				{Sends command to utility, returns response.}
;				cmd [char! string! block!] {Command to send - must be a single command no newline.}
;				/expect prompt [string! block!] {Prompt to expect following a response from the command. Default is "psftp> ".}
;				/local response
;			] [
;				if not expect [prompt: {psftp> }]
;				if block? cmd [cmd: rejoin cmd]
;				server/send join cmd newline
;				response: server/get-response prompt
;				if 'closed = response/status [
;					do make error! rejoin [{The command has exited with message: } mold trim copy response/string]
;				]
;				response/string
;			]
;		
;			server: make-cmd-server {"C:\Program Files (x86)\PuTTY\psftp.exe"}
;			server/startup
;			server/get-response {psftp> }
;			print tell-psftp {help}
;			server/send {quit^/}
;			server/shutdown
;
;		I hope that Call in REBOL 3 will be upgraded enough to make this script redundant.
;
;
;	Requirements:
;
;		* Windows.
;		* This Rebol 3 program currently requires Rebol 2 to run.
;
;
;	Comments/Warnings:
;
;		I couldn't find a simpler approach so this is what I came up with:
;
;			Two helper REBOL processes are created along with the command. The command server
;			object sends commands to the first REBOL process which just	emits them to its
;			output, which is then piped to the command, which in turns has it's output piped
;			to the second REBOL process, which finally sends the output back to the Command
;			server object.
;
;		It may or may not work for you - I found it works for PING and PSFTP (Putty) but not properly
;		for FTP (part of windows).
;
;		No checking is done on the connections, so you could have problems (security?) if some other
;		network program tries to connect to these processes, or you run more than one instance of this
;		without changing the network port.
;
;		I have included many comments as much for myself as anyone else as this is my first Rebol 3
;		networking script.
;
;
;	Rebol 3 Alpha - problems.
;
;		At the time of writing this, I can't get any sort of redirection working in REBOL 3 Alpha (Saphirion build),
;		so I have decided that this REBOL 3 version will call REBOL 2 for the helper processes.
;
;		To remove the need for Rebol 2, you would need to use a Rebol 3 version that support redirection for
;		console programs and you need to rewrite the helper networking code.
;
;
;	To Do (maybe):
;
;		o Replace use of Does, Has and Func with Funct.
;
;		o Add handshaking to prevent collisions and add connection security.
;			- To allow port range for listeners - a component to manage this resource.
;			- To check that helper is legit.
;			- To allow multiple simultaneous command calls.
;
; ---------------------------------------------------------------------------------------------------------------------
;
; Script Summary
;
; ---------------------------------------------------------------------------------------------------------------------
;
;	call-output
;
;		This function calls a command and returns the output from it.
;
;
;	make-cmd-server
;
;		This function creates a command server object that will manage the command.
;
;
;	Command Server Object Functions:
;
;		startup
;
;			Use this function to setup the processes and start the command.
;
;		send
;
;			Send some data to the running command.
;
;		receive
;
;			Receives a response from the command - this is a low level function. 
;
;			Normally you should use get-response which will buffer the response
;			until it finds a specifc prompt.
;
;
;		wait-response
;
;			Waits until something is received from called program, or times out.
;
;
;		get-response
;
;			Specify expected prompt or alternative prompts, it buffers input until
;			one of the prompts is found or the connection is closed. If none is
;			specified it returns entire output entire connections is closed.
;			A function may be specified, but must follow the result profile
;			of tokenise-response.
;
;			The return value is that of tokenise-response.
;
;
;		tokenise-response
;
;			Low-level function used to find a prompt within a response.
;
;			When the function cannot find a delimiter it returns none.
;
;			When the function finds a delimiter it returns a block of key/value pairs:
;
;				string [string!]
;
;					- This is the buffered response up to the prompt or close of connection.
;
;				status [string! word!]
;
;					- A string! indicates which prompt terminates the response.
;
;					- The word CLOSED indicates the connection was closed. This should be treated
;					  as an error. If syserr is being redirected, which is the default, the response
;					  string may contain the error message from the command.
;
;
;
; ---------------------------------------------------------------------------------------------------------------------


call-output: func [
	{Calls Command and returns output.}
	command [string!] {The command to Call in CMD.EXE.}
	/local server result
][
	server: make-cmd-server/nosysinput command
	attempt [
		server/startup
		result: server/get-response none
	]
	server/shutdown
	result/string
]


make-cmd-server: func [
	{Returns an object that can send and receive messages to/from a command.}
	command [string!] {The command to Call in CMD.EXE.}
	/nosysinput {No input will be passed to called program. Saves starting input helper process.}
	/nosyserr {Prevents append of redirection operator "2>&1" to command.}
	/trace-send {Print data sent the command.}
	/trace-receive {Print data received from the command.}
] [

	context [

		;;; log: func [x][write/append %cmd-test.log.txt rejoin [newline now {: } reform :x]]
		log: none


		; Port to listen on.
		listen: 8000 ; This needs to be configurable.

		; Path to REBOL 2 - used for helper processes.
		rebol.exe: {REBOL.EXE}

		; Startup-timeout time. The only reason we'd have to wait this long is if the
		; process wasn't created at all due to an error in the command.
		startup-timeout: 1

		; Response-timeout time. Used by get-response.
		; Amount of time to wait for data to be emitted by the called program.
		response-timeout: 1

		; Need input helper?
		need-input: nosysinput

		; Maximum number of bytes that receiver will send by TCP.
		max-receiver-packet: 10240

		; The interactive command. It will be bookended in a pipe by sender and receiver REBOL processes.
		cmdstr: either nosyserr [command] [rejoin [command { 2>&1}]]

		; Connection to sender process
		sender: none

		; Connection receiver process
		receiver: none

		; Buffer to accummulate data until timeout of 0.1 seconds occurs.
		receive-buffer: none

		; Response buffer - accumulates responses.
		response-buffer: none

		; Trace switches.
		trace-sending: trace-send
		trace-receiving: trace-receive

		; Startup
		startup: func [
			/local listener
		] [

			;
			; Initialise.

			receive-buffer: copy {}
			response-buffer: copy {}

			;
			; Setup listener.

			listener: open rejoin [tcp://: listen]
			listener/awake: func [event /local port] [
				log ["listener: " :event/type]
				if event/type = 'accept [

					port: first event/port

					; First connection is from sender.
					if all [need-input none? sender] [
						log "Accepted connection from sender."
						sender: port
						sender/awake: func [event] [
							log ["sender: " :event/type]
							if 'close = event/type [
								closeports
								return true
							]
							false
						]
						return true ; Return from wait.
					]

					; Second connection is from receiver.
					log "Accepted connection from receiver."
					receiver: port
					receiver/awake: func [event] [
						log ["receiver: " :event/type]
						switch event/type [
							read [
								if trace-receiving [log ["received: " mold to string! receiver/data]]
								append receive-buffer to string! receiver/data
								clear receiver/data ; Remove processed data from port buffer.
								return true ; Return from Wait.
							]
							close [
								closeports
								return true
							]
						]
						false
					]
					return true ; Return from wait.

				]
				false
			]

			;
			; Call Rebol with a generated command line script which will connect to the listener and output to
			; sysoutput everything it sent.

			sender-cmd: either need-input [
				rejoin [
					rebol.exe { -w --do "wait svr: open/direct/no-wait tcp://localhost:} listen
					{ x: copy svr while [not none? x][prin x wait svr x: copy svr] close svr" | }
				]
			][{}]

			;
			; Call Rebol with a generated command line script which will connect to the listener and send it
			; everything it receives on sysinput (which happens to come from the command via piping).

			receiver-cmd: rejoin [
				{ | } rebol.exe { -w --do "s: open/direct tcp://localhost:} listen
				{ set-modes system/ports/input [lines: false] b: make string! n: } max-receiver-packet
				{ forever [r: read-io system/ports/input clear b n if r < 0 [break] insert s replace/all b CR {}] close svr"}
			]

			;
			; Call command piping input from sender and piping its output to receiver.

			call rejoin [{cmd /c } sender-cmd cmdstr receiver-cmd]

			;
			; Wait for connection from sender REBOL process. Timeout indicates failure of the command.

			if need-input [
				log "wait for connection from sender"
				if none? wait [listener startup-timeout] [
					close listener
					closeports
					do make error! probe rejoin [{The following command appears to have failed: } cmdstr]
				]
			]

			;
			; Wait for connection from receiver REBOL process. Shouldn't have a timeout here - but check anyway.

			log "wait for connection from receiver"
			if none? wait [listener startup-timeout] [
				close listener
				closeports
				do make error! probe rejoin [{Could not setup pipeline to the command: } cmdstr]
			]

			;
			; Close listener, no longer need it.

			close listener
			log "listener closed."

			exit

		]

		send: func [
			{Send the command some input.}
			data [string! char!]
		] [
			if none? :sender [do make error! rejoin [{Sender connection to } mold cmdstr { is closed.}]]
			if trace-sending [log ["sending: " mold :data]]
			write sender to binary! data
			wait 0.01 ; Need a wait for the write to occur.
		]


		;
		;
		; Receive reads the next data from the receiver port, returns it as string.
		; Returns none when connection has been closed.
		;
		; Accumulating data here until short timeout so as accumulate packets of data
		; into more fully formed responses by the called program, not necessarily a
		; complete response. Packet size is determined by the helper program and the amount
		; emitted by called program.


		receive: func [
			{Get's next response from receiver. Returns empty string if nothing new. Returns none if connection closed.}
			/local response
		] [
			if none? :receiver [do make error! rejoin [{Receiver connection to } mold cmdstr { is closed.}]]
			log "read receiver"
			read receiver
			wait-response
			while [not empty? receive-buffer][
				append any [response response: copy {}] receive-buffer
				clear receive-buffer
				read receiver
				wait 0.1 ; Wait for a short time to see if more packets are coming.
			]
			if none? response [shutdown]
			response
		]

		;
		;

		wait-response: func [
			/timeout wait-time
		] [
			if not timeout [wait-time: response-timeout]
			wait [receiver wait-time]
		]

		;
		; tokenise-response is used to determine is a complete
		; response has been received. It takes a block of strings
		; which are treated as delimiters that delimit
		; the complete response. Typically these will be prompt or
		; an error message from the interactive program.
		; It should return none if no delimiter is found.

		tokenise-response: func [delimiters /local a b result] [
			foreach string delimiters [
				if parse/all response-buffer [to string a: string b: to end] [
					result: compose [string (copy/part response-buffer a) status (string)]
					remove/part response-buffer b
					return result
				]
			]
			none
		]

		;
		; get-response accumulates output until a condition is met that indicates the
		; response has been completely received or the connection is closed.
		; It returns string and status when the response is complete, status indicates
		; if the connection was closed at the end of the response.
		; It will return none if there is no unprocessed response and the connection is closed.
		; If a function is specified, it should use the same result profile as tokenise-response.

		get-response: func [
			{Buffers response up to the specified delimiters or end of connection. Returns block - status can be a string or 'exited}
			delimiter [none! string! block! function!] {Prompt, or block of prompts, or function that will tokenise the response. None = all output until connection closed.}
			/local result resp unfinished
		] [
			if none? response-buffer [return none] ; Not connected.
			switch type?/word :delimiter [
				none! [
					unfinished: [true]
				]
				function! [
					unfinished: compose [not result: (:delimiter)]
				]
				string! block! [
					unfinished: compose/deep [not result: (:tokenise-response) [(delimiter)]]
				]
			]
			while unfinished [
				either all [connection? resp: receive][
					append response-buffer resp
				] [
					result: compose [string (response-buffer) status closed] ; Command must have exited.
					response-buffer: none ; Nothing left and connection closed.
					break
				]
			]
			result
		]

		connection?: func [] [
			all [
				any [not need-input found? sender]
				found? receiver
			]
		]

		closeports: does [
			log "Closing ports."
			if sender [close sender sender: none]
			if receiver [close receiver receiver: none]
		]

		shutdown: does [
			closeports
		]

	]

]
