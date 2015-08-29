REBOL [
	purpose: {Encode and Decode comment encoded Rebol blocks.}
]

;
; Note: Newline are encoded with {^/**  }
;


grammar: context [
	p1: p2: text: init-nl: none

	emit: [p2: (append text copy/part p1 p2)]
	wsp: compose ["  "]
	clean-line: [{**} opt wsp newline]
	line: [{**} wsp p1: thru newline emit]
	rule: [ (init-nl: false)
		opt [clean-line (init-nl: true)]
		any line
		opt clean-line
	]
]

load-comment: func [
	{Load next block from comment lines.}
	string [string!]
	/local p1 p2 text clean-line emit wsp block
] [

	grammar/text: clear copy string

	if parse/all string grammar/rule [
		block: load/all grammar/text
		if grammar/init-nl [new-line block true]
		block
	]
]

mold-comment: func [
	{Mold block into comment lines.}
	block [block!]
	/local text bol pos
] [

	; Note: Preserves newline formatting of the block.

	bol: {**} indent: {  }

	string: mold-contents block

	replace/all string newline rejoin [newline bol indent]

	pos: insert string bol
	if not equal? newline pos/1 [insert pos indent]

	if indent = pos: skip tail string 0 - length? indent [clear pos]
	append string newline

	string
]

mold-contents: func [
	{Mold block without the outer brakets (a little different to MOLD/ONLY).}
	block [block! paren!]
	/local string bol
][

	string: mold block

	either parse/all string [
		skip copy bol [newline some #" "] to end
	][
		replace/all string bol newline
	][
	]
	remove string
	take/last string

	string
]

