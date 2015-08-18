REBOL []

either system/version > 2.100.0 [;Rebol3
	user-error: funct [match [string! block!] test [block!]] [
		if string? match [match: compose [(match) to end]]
		all [
			error? err: try test
			parse err/arg1 match
		]
	]
] [
	user-error: funct [match [string! block!] test [block!]] [
		if string? match [match: compose [(match) to end]]
		all compose [
			error? err: try test
			err: disarm err
			parse/all err/arg1 match
		]
	]
]

requirements: funct [
	{Test requirements.}
	about
	block [block!] {Series of test blocks. A textual requirement begins the block (optional).}
] [
	results: new-line/all/skip collect [
		foreach test block [
			value: none
			error? set/any 'value try bind test 'user-error
			keep all [
				value? 'value
				logic? value
				value
			]
			keep/only either string? test/1 [test/1] [test]
		]
	] true 2

	remove-each [passed id] results [passed]
	either empty? results [
		compose/only [
			(:about) Passed
		]
	] [
		new-line compose/only [
			(:about) TODO
			(new-line/all extract next results 2 true)
		] true
	]
]

do %comment-block-encoding.reb

encode-decode: func [block] [block = decode-comment-block encode-comment-block block]
decode-encode: func [string] [string = encode-comment-block decode-comment-block string]

print mold requirements 'mold-contents [

	[quote {} = mold-contents []]

	[quote {1} = mold-contents [1]]

	[quote {^/1^/} = mold-contents new-line [1] true]

	[quote {1^/    2 3^/} = mold-contents [1
		2 3]
	] ; Fails R2 because R2 puts a space in front first newline.
]

print mold requirements 'comment-encoding [

	[quote "**^/" = encode-comment-block []]

	[quote "**  1^/" = encode-comment-block [1]]

	[quote "**^/**  1^/**^/" = encode-comment-block new-line [1] true]

	[quote "**  1^/**      2 3^/**^/" = encode-comment-block [1
		2 3]
	] ; Fails R2 because R2 puts a space in front first newline.

	[{Blocks should decode from their encoded comment accurately.}

		all [
			encode-decode []
			encode-decode [1]
			encode-decode [[]]
			encode-decode [[1]]
			encode-decode [
				Key1: Value1
				Key2: {Value2...}
				Key3: [
					Value4
				]
				...
			]
		]
	]

	[{Comments should encode from block accurately.}

		all [
			decode-encode "**^/"
			decode-encode "**  1^/"
			decode-encode "**^/**  1^/**^/"
		]
	]

]

comment {forms} {

[]
***********
**
***********


[1]
***********
**  1
***********

[
	1
	2
]
***********
**
**  1
**  2
**
***********

[1
	2 3
]
***********
**
**  1
**    2 3
**
***********
}