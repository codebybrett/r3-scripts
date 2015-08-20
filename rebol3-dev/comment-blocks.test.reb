REBOL []

either system/version > 2.100.0 [;Rebol3

	user-error: funct [match [string! block!] test [block!]] [
		if string? match [match: compose [(match) to end]]
		all [
			error? err: try test
			parse err/arg1 match
		]
	]
] [ ;Rebol2

	print {Some tests will fail in Rebol2 due to behaviour changes.}

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

do %comment-blocks.reb

encode-decode: func [block] [block = load-comment mold-comment block]
decode-encode: func [string] [string = mold-comment load-comment string]

print mold requirements {Outstanding} [

	[{Use mold/only for mold-contents.}]

]

print mold requirements 'mold-contents [

	[quote {} = mold-contents []]

	[quote {1} = mold-contents [1]]

	[quote {^/1^/} = mold-contents new-line [1] true]

	[quote {1^/    2 3^/} = mold-contents [1
		2 3]
	]
]

print mold requirements 'comment-blocks [

	[quote "**^/" = mold-comment []]

	[quote "**  1^/" = mold-comment [1]]

	[quote "**^/**  1^/**^/" = mold-comment new-line [1] true]

	[quote "**  1^/**      2 3^/**^/" = mold-comment [1
		2 3]
	]

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