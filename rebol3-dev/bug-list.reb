REBOL [
	purpose: {Track status of some bugs I have encountered}.
]

; NOTE: Some of these may be fixed in some intepreters.

requirements 'problems-encountered [


	[{Get path of length 1.}

		1 = get in use [x] [x: context [v: 1] get to path! 'x] 'v
	]

	[{Mold should handle blocks with recursive references.}

		do mold-recursive-block-bug.reb
	]

]
