REBOL [
	{Bind and evaluate expressions relative to contexts.}
]

do %evaluate-path.reb

apropos: func [
	{Bind and evaluate block using one or more context references.}
	reference [object! word! block! path!] {Represents context.}
	body [block! paren!]
	/binding {Just bind, do not evaluate the block}
	/only {Evaluate the path only, not each segment of the path.}
] [

	switch/default type?/word :reference [

		object! [
			bind body reference
		]

		block! [
			foreach context reference [apropos/binding context body]
		]

		path! [
			if not only [
				for i 1 (subtract length? reference 1) 1 [
					bind body evaluate-path copy/part reference i
				]
			]
			bind body evaluate-path reference
		]

		word! [bind body do reference]

	] [do make error! {APROPOS only accepts simple references to contexts.}]

	either binding [body] [do body]
]
