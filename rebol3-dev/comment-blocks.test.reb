REBOL []

do %requirements.reb

do %comment-blocks.reb

print mold requirements 'encode-lines [

	[quote {**^/} = encode-lines {} {**} {  }]
	[quote {**  x^/} = encode-lines {x} {**} {  }]
	[quote {**  x^/**^/} = encode-lines {x^/} {**} {  }]
	[quote {**^/**  x^/} = encode-lines {^/x} {**} {  }]
	[quote {**^/**  x^/**^/} = encode-lines {^/x^/} {**} {  }]
	[quote {**  x^/**    y^/**      z^/} = encode-lines {x^/  y^/    z} {**} {  }]
]

print mold requirements 'decode-lines [

	[quote {} = decode-lines {**^/} {**} {  } ]
	[quote {x} = decode-lines {**  x^/} {**} {  } ]
 	[quote {x^/} = decode-lines {**  x^/**^/} {**} {  } ]
	[quote {^/x} = decode-lines {**^/**  x^/} {**} {  } ]
	[quote {^/x^/} = decode-lines {**^/**  x^/**^/} {**} {  } ]
	[quote {x^/  y^/    z} = decode-lines {**  x^/**    y^/**      z^/} {**} {  } ]
]

print mold requirements 'load-until-blank [

	[none? load-until-blank {}]
	[[[1 [2]] ""] = load-until-blank {1 [2]^/}]
	[[[1 [2]] "^/rest"] = load-until-blank "1 [2]^/^/rest"]
]

print mold requirements 'mold-contents [

	[quote {} = mold-contents []]

	[quote {1} = mold-contents [1]]

	[quote {^/1^/} = mold-contents new-line [1] true]

	[quote {1^/    2 3^/} = mold-contents [1
		2 3]
	] ; Fails in Rebol2.
]
