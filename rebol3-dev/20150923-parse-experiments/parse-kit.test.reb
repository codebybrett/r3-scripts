REBOL [
	Title: "Parse Kit - Tests"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
]

script-needs [
	%requirements.reb
	%../parse-kit.reb
]

comment [
text: read %../../ren-c/src/core/n-system.c
lits: bind [nl eol] c.lexical/grammar
terms: exclude exclude union c.lexical/grammar/white-space c.lexical/grammar/preprocessing-token [|] lits
tokenise [parse/all/case text c.lexical/grammar/text] lits terms



HALT
]


requirements 'tokenise [
	[true]
]

