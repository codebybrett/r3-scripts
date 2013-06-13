REBOL [
	Title: "interactive-cmd-server"
	File: %interactive-cmd-server.r3.r
	Author: "Brett Handley"
	Date: 13-Jun-2013
	Purpose: "Provides a way to interact with an interactive shell command."
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
		1.0.0 [13-Jun-2013 "Initial unfinished REBOL 3 Alpha version." "Brett Handley"]
	]
]

; ---------------------------------------------------------------------------------------------------------------------
;
;
;	Purpose:
;
;		Allow calling interactive console programs (those that prompt for and process keystrokes) using REBOL.
;
;		I couldn't find a simpler approach so this is what I came up with:
;
;			Two helper REBOL processes are created along with the command. The command server
;			object sends commands to the first REBOL process which just	emits them to its
;			output, which is then piped to the command, which in turns has it's output piped
;			to the second REBOL process, which finally sends the output back to the Command
;			server object.
;
;		Using it is fairly straight forward, make the command server then
;		call /startup /send /get-next-reponse /shutdown.
;
;
;	Rebol 3 Alpha - problems.
;
;		At the time of writing this, I can't get any sort of redirection working in REBOL 3 Alpha,
;		so I have decided that this REBOL 3 version will call REBOL 2 for the helper processes.
;		I hope that Call in REBOL 3 will be upgraded to make this script redundant.
;
;
;	Note:
;
;		It may or may not work for you - I found it works for PSFTP (Putty) but not fully for
;		FTP (part of windows).
;
;		The assumption is you will be using a command that waits for user input from sysinput.
;
;		No checking is done on the connections, so you could have problems (security?) if some other
;		network program tries to connect to these processes, or you run more than one instance of this
;		without changing the network port.
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
;			Normally you should use get-next-response which will buffer the response
;			until it finds a specifc prompt.
;
;
;		get-next-response
;
;			Give expected prompt or alternative prompts, it buffers input until
;			one of the prompts is found or the connection is closed.
;
;			The function returns a block of key/value pairs:
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
;	Example:
;
;		Controlling PSFTP.exe:
;
;		tell-psftp: func [
;			{Sends command to utility, returns response.}
;			cmd [char! string! block!] {Command to send - must be a single command no newline.}
;			/expect prompt [string! block!] {Prompt to expect following a response from the command. Default is "psftp> ".}
;			/local response
;		] [
;			if not expect [prompt: {psftp> }]
;			if block? cmd [cmd: rejoin cmd]
;			cmd-server/send join cmd newline
;			response: cmd-server/get-next-response prompt
;			if 'closed = response/status [
;				make error! rejoin [{The command has exited with message: } mold trim copy response/string]
;			]
;			response
;		]
;
;		cmd-server: make-cmd-server {"C:\Program Files (x86)\PuTTY\psftp.exe"}
;		cmd-server/startup
;		cmd-server/get-next-response {psftp> }
;		tell-psftp {help}
;		cmd-server/send {quit^/}
;		cmd-server/shutdown
;
;
; ---------------------------------------------------------------------------------------------------------------------



make-cmd-server: func [
	{Returns an object that can send a receive messages to a shell command.}
	command [string!] {The command to Call in a shell.}
	/nosyserr {Prevents append of redirection operator "2>&1" to command.}
	/trace-send {Print data sent the command.}
	/trace-receive {Print data received from the command.}
] [

	context [

		; Port to listen on.
		listen: 8000 ; This needs to be configurable.

		; Path to REBOL 2 - used for helper processes.
		rebol.exe: {REBOL.EXE}

		; Timeout time. The only reason we'd have to wait this long is if the
		; process wasn't created at all due to an error in the command.
		timeout: 1

		; The interactive command. It will be bookended in a pipe by sender and receiver REBOL processes.
		cmdstr: either nosyserr [command] [rejoin [command { 2>&1}]]

		; Connection to sender process
		sender: none

		; Connection receiver process
		receiver: none

		; Response buffer - accumulates responses.
		response-buffer: none

		; Trace switches.
		trace-sending: trace-send
		trace-receiving: trace-receive

		; Startup
		startup: func [
			/local listener
		] [

			; Initialise response buffer.
			response-buffer: copy {}

			; Open listen port
			listener: open rejoin [tcp://: listen]
			listener/awake: func [event /local port] [
;;;				print ["listener: " :event/type]
				if event/type = 'accept [
					port: first event/port
					if none? sender [
;;;						print "got sender"
						sender: port
						sender/awake: func [event] [
;;;							print ["sender: " :event/type]
							switch event/type [
								close [
									close event/port
									return true
								]
							]
							false
						]
						return true ; Return from wait.
					]

;;;					print "got receiver"
					receiver: port
					receiver/awake: func [event] [
						switch event/type [
;;;							print ["receiver: " :event/type]
							read [
;;;								append any [response response: copy {}] event/data
;;;								clear event/port/data ; Remove processed data from buffer.
;; Will let /receive do the read.								read event/port
								return true ; Return from Wait.
							]
							close [
								close event/port
								return true
							]
						]
						false
					]
					return true ; Return from wait.

				]
				false
			]

			; Get input from control script server (this script) send to system output which is then piped to command.
			sender-cmd: rejoin [
				rebol.exe { -w --do "wait svr: open/direct/no-wait tcp://localhost:} listen
				{ x: copy svr while [not none? x][prin x wait svr x: copy svr] close svr"}
			]

			; Get input which comes from piped command and send to the control script server (this script).
			; Note that CRs are removed from the command output to fit REBOL's newline line-termination.

			receiver-cmd: rejoin [
				rebol.exe { -w --do "s: open/direct tcp://localhost:} listen
				{ set-modes system/ports/input [lines: false] b: make string! n: 1024 forever [r: read-io system/ports/input clear b n if r < 0 [break] insert s replace/all b CR {}] close svr"}
			]

			; Call command;
			;;; R2 ; call/shell rejoin [sender-cmd { | } cmdstr { | } receiver-cmd]
			call rejoin [{cmd /c } sender-cmd { | } cmdstr { | } receiver-cmd]

			; Wait for connection from sender REBOL process. Timeout indicates failure of the command.
;;;			print "wait for connection from sender"
;;; Note that timeout must be last in the wait list in R3 why?! Bug?
			if none? wait [listener timeout] [
				close listener
				closeports
				do make error! probe rejoin [{The following shell command appears to have failed: } cmdstr]
			]

			; Wait for connection from receiver REBOL process. Shouldn't have a timeout here - but check anyway.
;;;			print "wait for connection from receiver"
;;; Note that timeout must be last in the wait list in R3 why?! Bug?
			if none? wait [listener timeout] [
				close listener
				closeports
				do make error! probe rejoin [{Could not setup pipeline to the shell command: } cmdstr]
			]

			; Close listener, no longer need it.
;;;			print "closing listner"
			close listener
;;;			print "listener closed."
		]

		send: func [
			{Send the command some input.}
			data [string! char!]
		] [
			if none? :sender [make error! rejoin [{Sender connection to } mold cmdstr { is closed.}]]
			if trace-sending [print ["sending: " mold :data]]
			write sender to binary! data
			wait [sender 0.01] ; Need a wait for the write to occur.
		]

		receive: func [
			{Get's next response from receiver. Returns empty string if nothing new. Returns none if connection closed.}
			/local response
		] [
			if none? :receiver [make error! rejoin [{Receiver connection to } mold cmdstr { is closed.}]]
			read receiver
			wait [receiver 0.1]
			while [not empty? receiver/data] [
				append any [response response: copy {}] to string! receiver/data
				clear receiver/data ; Remove processed data from buffer.
				read receiver
				wait [receiver 0.1]
			]
			if trace-receiving [print ["received: " mold :response]]
			if none? response [shutdown]
			response
		]

		wait-response: func [] [wait receiver]

		;
		; Need buffering because:
		; - you can't guarantee that receive will return everything at once.
		; - it is useful to wait until the next prompt.

		get-next-response: func [
			{Buffers response up to the specified prompt or end of connection. Returns block - status can be a string or 'exited}
			prompt [string! block!] {The prompt to wait for, or block of alternative prompts.}
			/local result find-prompt
		] [
			if none? response-buffer [return none]
			prompt: compose [(prompt)]
			find-prompt: has [a b] [
				foreach string prompt [
					if parse/all response-buffer [to string a: string b: to end] [
						result: compose [string (copy/part response-buffer a) status (string)]
						remove/part response-buffer b
						return true
					]
				]
				none
			]
			while [not find-prompt] [
				wait 0.01 ; Need a small delay for output to be captured.
				wait receiver ; Wait for something to happen on the port.
				resp: receive
				either none? resp [
					result: compose [string (response-buffer) status closed] ; Command must have exited.
					response-buffer: none
					break
				] [
					append response-buffer resp
				]
			]
			result
		]

		connection?: func [] [
			all [found? sender found? receiver]
		]

		closeports: does [
			if sender [close sender sender: none]
			if receiver [close receiver receiver: none]
		]

		shutdown: does [
			closeports
		]

	]

]
