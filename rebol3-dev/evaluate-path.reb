REBOL [
	purpose: {Evaluate a path. Useful during transition from Rebol 2 to Rebol 3.}
]

either system/version > 2.100.0 [; Rebol3

	either error? try [
		; This should work normally.
		use [x] [x: context [v: 1] get to path! 'x]
	] [
		; Workaround bug.
		evaluate-path: func [path] [
			if 1 = length? :path [path: first :path]
			get :path
		]
	] [
		evaluate-path: func [path] [
			get :path
		]
	]

] [; Rebol2

	evaluate-path: func [path] [do :path
	]

]
