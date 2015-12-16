REBOL [
	Title: "Token Kit - Tests"
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
	%token-kit.reb
]

either system/version > 2.100.0 [; Rebol3

	token-matching-test: requirements 'token-matching [

		[[] = token-matching [] []]
		[[] = token-matching [t] []]

		[{Token word match.}
			[into ['t skip]] = token-matching [t] [t]
		]

		[{Token string match.}
			[into [skip {test}]] = token-matching [] [{test}]
			[into [skip {test}]] = token-matching [t] [{test}]
		]

		[{Token word and string match.}
			[into ['t {test}]] = token-matching [t] [[t {test}]]
		]

		[{Complex parse rule.}
			equal? [into ['t skip] | into [skip {test}] | into ['t {test}]]
			token-matching [t] [t | {test} | [t {test}]]
		]
	]

] [; Rebol2

	token-matching-test: requirements 'token-matching [

		[[] = token-matching [] []]
		[[] = token-matching [t] []]

		[{Token word match.}
			[[into ['t skip]]] = token-matching [t] [t]
		]

		[{Token string match.}
			[[into [skip {test}]]] = token-matching [] [{test}]
			[[into [skip {test}]]] = token-matching [t] [{test}]
		]

		[{Token word and string match.}
			[[into ['t {test}]]] = token-matching [t] [[t {test}]]
		]

		[{Complex parse rule.} ; Could be optimised.
			equal? [[into ['t skip]] | [into [skip "test"]] | [into ['t "test"]]]
			token-matching [t] [t | {test} | [t {test}]]
		]
	]

]

tokenise-test: requirements 'tokenise [

	[
		token-fn: funct [input] [
			if not head? input [return none]
			reduce ['x next input]
		]

		user-error [thru {Could not tokenise at position} to end] [
			tokenise :token-fn {x }
		]
	]

	[
		token-fn: funct [input] [
			parse/all input [[to " " | to end] position:]
			reduce ['name position]
		]

		[] = tokenise :token-fn []
	]

]


requirements %token-kit.reb [

	['passed = last token-matching-test]
	['passed = last tokenise-test]
]


