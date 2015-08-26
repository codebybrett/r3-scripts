REBOL []

do %requirements.reb

do %apropos.reb

ctx1: context [

	c1: 1
	value: 1

	ctx2: context [

		c2: 2
		value: 2

		ctx3: context [

			c3: 3
			value: 3
		]
	]
]

print mold requirements 'apropos [

	[1 = apropos ctx1 [value]]

	[1 = apropos [ctx1] [value]]

	[1 = apropos (to path! 'ctx1) [value]]

	[2 = apropos 'ctx1/ctx2 [value]]

	[[1 2] = apropos 'ctx1/ctx2 [reduce [c1 value]]]

	[throws-error [id = 'no-value arg1 = 'c1] [apropos/only 'ctx1/ctx2 [reduce [c1 value]]]]

	[[2 3] = apropos [ctx1/ctx2/ctx3 ctx1/ctx2] [reduce [value c3]]]

]
