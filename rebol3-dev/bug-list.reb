REBOL [
	purpose: {Track some bugs I have encountered}.
]


requirements 'known-issues [


	[{Get path of length 1.}

		use [x] [x: context [v: 1] get to path! 'x]
	]

]
