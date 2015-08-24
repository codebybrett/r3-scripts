REBOL []

do %requirements.reb

do %comment-blocks.reb

encode-decode: func [block] [block = load-comment mold-comment block]
decode-encode: func [string] [string = mold-comment load-comment string]

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