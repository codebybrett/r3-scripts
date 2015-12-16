REBOL [
	Title: "C Preprocessing Tokens"
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

do %c-lexicals.complete.reb

c-pp-tokeniser: context [

	name: none

	grammar: make c.lexical/grammar []

	terms: exclude union grammar/white-space grammar/preprocessing-token [|]

	foreach term terms [

		grammar/:term: compose/only [
			(get term)
			(to paren! compose [name: (to lit-word! term)])
		]
	] 

	token: funct [
		{Return C Preprocessing token name and end position.}
		input {Should be positioned at a token.}
	] [

		parse/all/case input [grammar/c-pp-token rest:]

		if rest [
			reduce [c-pp-tokeniser/name rest]
		]
	]
]

