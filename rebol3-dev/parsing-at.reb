REBOL [
	author: {Brett Handley}
	license: {Apache 2.0}
]


parsing-at: func [
	{Defines a rule which evaluates a block for the next input position, fails otherwise.}
	'word [word!] {Word set to input position (will be local).}
	block [block!] {Block to evaluate. Return next input position, or none/false.}
	/end {No default tail check (allows evaluation at the tail).}
] [

	use [result position][
		block: to paren! block
		if not end [
			block: compose/deep/only [all [not tail? (word) (block)]]
		]
		block: compose/deep [result: either position: (block) [[:position]][[end skip]]]
		use compose [(word)] compose/deep [
			[(to set-word! :word) (to paren! block) result]
		]
	]
]
