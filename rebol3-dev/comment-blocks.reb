REBOL [
	purpose: {Encode and Decode comments encoded with Rebol blocks.}
]


either system/version > 2.100.0 [; Rebol3

	load-next: funct [
		{Load the next value. Return block with value and new position.}
		string [string!]
	] [
		out: transcode/next to binary! string
		out/2: skip string subtract length? string length? to string! out/2
		out
	] ; by @rgchris.

] [; Rebol2

	load-next: funct [
		{Load the next value. Return block with value and new position.}
		string [string!]
	] [
		load/next string
	]
]

decode-lines: funct [
	{Decode string from prefixed lines (e.g. comments).}
	line-prefix [string!] {Usually "**" or "//".}
	indent [string!] {Usually "  ".}
	string [string!]
] [
	if not parse/all string [any [line-prefix thru newline]][
		do make error! reform [{decode-lines expects each line to begin with} mold line-prefix { and finish with a newline.}]
	]
	string: copy string
	insert string newline
	replace/all string {^/**} newline
	if not empty? indent [
		replace/all string join newline indent newline
	]
	remove string
	remove back tail string
	string
]

encode-lines: func [
	{Encode block into prefixed lines (e.g. comments).}
	line-prefix [string!] {Usually "**" or "//".}
	indent [string!] {Usually "  ".}
	string [string!]
	/local text bol pos
] [

	; Note: Preserves newline formatting of the block.

	; Encode newlines.
	replace/all string newline rejoin [newline line-prefix indent]

	; Indent head if original string did not start with a newline.
	pos: insert string line-prefix
	if not equal? newline pos/1 [insert pos indent]

	; Clear indent from tail if present.
	if indent = pos: skip tail string 0 - length? indent [clear pos]
	append string newline

	string
]

load-until-blank: funct [
	{Load rebol values from string until double newline.}
	string [string!]
	/next {Return values and next position.}
][

	wsp: compose [some (charset { ^-})]

	rebol-value: parsing-at x [
		res: any [attempt [load-next x] []]
		if not empty? res [second res]
	]

	terminator: [opt wsp newline opt wsp newline]

	not-terminator: parsing-unless terminator
	; Could be replaced with Not in Rebol 3 parse.

	rule: [
		some [pos: not-terminator rebol-value]
		opt newline position: to end
	]

	if parse/all string rule [
		values: load copy/part string position
		reduce [values position]
	]
]

mold-contents: func [
	{Mold block without the outer brackets (a little different to MOLD/ONLY).}
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

