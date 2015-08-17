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
		1.3.0 [23-Jun-2013 "Added shared secrets to reduce risk of collision/interception. Changed behaviours. Bug fixes." "Brett Handley"]
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
;			server: make-call-server {"C:\Program Files (x86)\PuTTY\psftp.exe"}
;			server/startup
;			print mold server/get-response {psftp> }
;			server/send {help^/}
;			print mold server/get-response {psftp> }
;			server/send {quit^/}
;			server/shutdown
;
;		The listener port is tied up for the duration of /startup. After that it can be reused
;		by another instance. So to use multiple simulataneous command calls, start them one
;		after the other with /startup, or you could you could set them up on different
;		listener ports by setting /listen.
;
;
;	Requirements:
;
;		* This Rebol 3 program currently requires Rebol 2 for the sender and receiver processes.
;
;		* As written, needs Windows, but see call-and-pipe below, perhaps
;		  a simple change will get it working on other platforms.
;
;
;	Comments:
;
;		It's my hope that Call in REBOL 3 will be upgraded enough to make this script redundant.
;
;		I couldn't find a simpler approach so this is what I came up with:
;
;			Two helper REBOL processes are created along with the command. The command server
;			object sends commands to the first REBOL process which just	emits them to its
;			output, which is then piped to the command, which in turns has it's output piped
;			to the second REBOL process, which finally sends the output back to the Command
;			server object. In addition there is a cmd.exe process that ties them together.
;
;		It may or may not work for you. On windows, I found it works for PING and PSFTP (Putty)
;		but not properly for FTP.
;
;		To avoid the risk of collisions/interception shared secrets are passed and checked
;		between the processes. This script checks the secret sent by the sender and the receiver
;		and the sender checks the secret this script sends it. I wanted have the receiver
;		check a secret this script sends it, but I had a problem I could not fix, probably not important.
;
;		I have included many comments as much for myself as anyone else as this is my first
;		significant Rebol 3 script.
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
;	To Do:
;
;		o Take advantage of Rebol 3 code structuring including using Funct instead of Does, Has, Func.
;
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
;	make-call-server
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
;			Returns response, or none if a timeout or connection is closed.
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
;			one of the prompts is found, response-timeout is exceeded or the connection
;			is closed. If none is specified it returns entire output until the connection
;			is closed.	A function may be specified, but must follow the result profile
;			of tokenise-response.
;
;			The return value is like that of tokenise-response. When STATUS is CLOSED,
;			an additional key/value pair (CLOSED-BY) describes why the connection was closed.
;
;				- A STATUS of the word CLOSED indicates the connection was closed.
;				  This should be treated as an error when your prompt is not none,
;				  because the response is not properly completed.
;				  If syserr is being redirected, which is the default, the response
;				  string may contain an error message from the command. The connection may be
;				  closed by the called program (CLOSED-BY CMD-EXIT), or by get-response
;				  when a timeout occurs (CLOSED-BY TIMEOUT) while waiting for prompt.
;
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
;				data [string! binary!]
;
;					- This is the buffered response up to the prompt or close of connection.
;
;				status [string! word!]
;
;					- A string! indicates which prompt terminates the response.
;
;
;
;
; ---------------------------------------------------------------------------------------------------------------------


call-output: func [
	{Calls Command and returns output.}
	command [string!] {The command to Call in CMD.EXE.}
	/local server result
][
	server: make-call-server/nosysinput command
	result: try [
		server/startup
		server/get-response none
	]
	server/shutdown
	if error? result [do result]
	result/data
]


make-call-server: func [
	{Returns an object that can send and receive messages to/from a command.}
	command [string!] {The command to Call in CMD.EXE.}
	/nosysinput {No input will be passed to called program. Saves starting input helper process.}
	/nosyserr {Prevents append of redirection operator "2>&1" to command.}
	/trace-send {Print data sent the command.}
	/trace-receive {Print data received from the command.}
] [

	context [

		log: none
		;;; log: func [x][write/append %cmd-test.log.txt rejoin [newline now {: } reform :x]]

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

		; Remove CR?
		remove-CR: ('windows = system/platform/1)

		; As string!
		string-data: true

		; Need input helper?
		need-input: not nosysinput

		; Need error redirection?
		need-errors: not nosyserr

		; Maximum number of bytes that receiver will send by TCP.
		max-receiver-packet: 10240

		; The interactive command. It will be bookended in a pipe by sender and receiver REBOL processes.
		command-string: command

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

		; This does the call and setups up piping between sender, program and receiver.
		; I have broken it out here in case someone can get it to work on other platforms.
		call-and-pipe: func [
			/local cmdstr
		][
			cmdstr: either need-errors [rejoin [command-string { 2>&1}]] [command-string]
			call rejoin [{cmd /c } sender-cmd cmdstr receiver-cmd]
		]

		; Startup
		startup: func [
			/local listener
			chk-l chk-s chk-r
		] [

			;
			; Initialise.

			receive-buffer: copy #{}
			response-buffer: copy either string-data [{}][#{}]

			foreach var [chk-l chk-s chk-r] [set :var form random/secure 9999999]

			if not integer? startup-timeout [do make error! {startup-timeout must be an integer!}]
			if not integer? response-timeout [do make error! {response-timeout must be an integer!}]

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
							switch event/type [
								read [return true]
								close [
									closeports
									return true
								]
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
								append receive-buffer receiver/data
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
					{ if not equal? copy svr form } chk-l { [quit]}
					{ insert svr } chk-s
					{ x: copy svr while [not none? x][prin x wait svr x: copy svr] close svr"}
					{ | }
				]
			][{}]

			;
			; Call Rebol with a generated command line script which will connect to the listener and send it
			; everything it receives on sysinput (which happens to come from the command via piping).

			receiver-cmd: rejoin [
				{ | }
				rebol.exe { -w --do "s: open/direct tcp://localhost:} listen
				{ set-modes system/ports/input [lines: false]}
				{ insert s } chk-r
				{ b: make string! n: } max-receiver-packet
				{ forever [r: read-io system/ports/input clear b n if r < 0 [break] insert s b] close svr"}
			]

			;
			; Call command piping input from sender and piping its output to receiver.

			call-and-pipe
		
			;
			; Wait for connection from sender REBOL process. Timeout indicates failure of the command.

			if need-input [
				log "wait for connection from sender"
				if none? wait [listener startup-timeout] [
					close listener
					closeports
					do make error! rejoin [{Could not establish connection to sender, command may have failed: } command-string]
				]
			]

			;
			; Wait for connection from receiver REBOL process. Shouldn't have a timeout here - but check anyway.

			log "wait for connection from receiver"
			if none? wait [listener startup-timeout] [
				close listener
				closeports
				do make error! rejoin [{Could not establish connection with receiver for the command: } command-string]
			]

			;
			; Close listener, no longer need it.

			close listener
			log "listener closed."

			if need-input [
				log "send sender secret"
				if none? :sender [do make error! rejoin [{Sender terminated connection unexpectedly: } command-string]]
				write sender chk-l wait 0.01 ; Send secret.

				log "check sender secret"
				if none? :sender [do make error! rejoin [{Sender terminated connection unexpectedly: } command-string]]
				read sender wait [sender 0.1] ; Wait for secret or close.
				if not connection? [
					do make error! rejoin [{Sender did not accept secret for command: } command-string]
				]
				if not parse sender/data [remove chk-s to end] [
					closeports
					do make error! rejoin [{Unknown process tried connect as sender for command: } command-string]
				]
			]

			log "check receiver secret"
			read receiver wait [receiver 0.1] ; Wait for secret from receiver.
			if not parse receive-buffer [remove chk-r to end] [
				closeports
				do make error! rejoin [{Unknown process tried to connect as receiver for command: } command-string]
			]

			log "startup completed."
			exit

		]

		send: func [
			{Send the command some input.}
			data [string! char!]
		] [
			log "Send."
			if none? :sender [do make error! rejoin [{Sender connection to } mold command-string { is closed.}]]
			if trace-sending [log ["sending: " mold :data]]
			write sender to binary! data
			wait 0.01 ; Need a wait for the write to occur.
		]


		;
		;
		; Receive reads the next data from the receiver port, returns it as string.
		; Returns none when response-timeout occures or connection has been closed.
		;
		; Accumulating data here until short timeout so as accumulate packets of data
		; into more fully formed responses by the called program, not necessarily a
		; complete response. Packet size is determined by the helper program and the amount
		; emitted by called program.


		receive: func [
			{Get's next response from receiver. Returns none if timeout or connection closed.}
			/local response
		] [
			log "Receive."
			if none? :receiver [do make error! rejoin [{Receiver connection to } mold command-string { is closed.}]]
			log "read receiver"
			read receiver
			wait-response
			while [not empty? receive-buffer][
				append any [response response: copy #{}] receive-buffer
				clear receive-buffer
				if found? :receiver [ ; Connection still open.
					log "read receiver"
					read receiver
					wait [receiver 0.1] ; Wait for a short time to see if more packets are coming.
				]
			]
			if found? response [
				if remove-cr [replace/all response CR {}]
				if string-data [response: to string! response]
			]
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
					result: compose [data (copy/part response-buffer a) status (string)]
					remove/part response-buffer b
					return result
				]
			]
			none
		]

		;
		; get-response accumulates output until a condition is met that indicates the
		; response has been completely received, or the connection is closed.
		; It returns string and status when the response is complete, status indicates
		; if the connection was closed at the end of the response.
		; It will return none if there is no unprocessed response and the connection is closed.
		; If a function is specified, it should use the same result profile as tokenise-response.

		get-response: func [
			{Buffers response up to the specified delimiters or end of connection. Returns block - status can be a string or 'exited}
			delimiter [none! string! block! function!] {Prompt, or block of prompts, or function that will tokenise the response. None = all output until connection closed.}
			/local result resp unfinished start-time timeout-time
		] [
			if none? response-buffer [return none] ; Not connected.
			timeout-time: now/precise + to time! reduce [0 0 response-timeout]
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
				if all [connection? resp: receive] [
					append response-buffer resp
					continue
				]
				if all [
					connection? ; Loss of connection indicates command exited.
					lesser? now/precise timeout-time ; A timeout, indicates a problem with client logic.
				][continue]
				result: compose [data (response-buffer) status closed closed-by (either connection? ['timeout]['cmd-exit])]
				response-buffer: none ; Nothing left and connection closed.
				shutdown
				break
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
