REBOL [
	Title: "Token Kit"
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
;;	https://raw.githubusercontent.com/codebybrett/grammars/master/C/c-lexicals.reb
	%parse-kit.reb
	%rewrite.reb
]

tokenise: funct [
	{Tokenise input using match function.}
	match [function!] {Takes input, returns [token-word end-of-token-position] or none.}
	input
	/shared {Return tokens as shared blocks and index of tokens.}
] [

	either shared [

		tokens: make block! []

		map-fn: funct [word [word!] start end] [

			token: reduce [word copy/part start end]
			if not pos: find tokens word [
				insert pos: tail tokens reduce [word make block! []]
			]
			if not idx: find/only instances: pos/2 token [
				insert/only idx: tail instances token
			]
			idx/1
		]
	] [

		map-fn: funct [word start end] [reduce [word copy/part start end]]
	]

	result: make block! []

	while [not tail? input] [

		lexeme: match input
		if none? lexeme [
			do make error! reform [{Could not tokenise at position} index? input]
		]

		token: map-fn lexeme/1 input lexeme/2
		append/only result token

		input: lexeme/2
	]

	either shared [

		new-line/all/skip tokens true 2
		foreach [name list] tokens [new-line/all list true]

		reduce [result tokens]
	][
		result
	]
]

join-tokens: funct [
	{Join tokens into a single string.}
	tokens [block!]
][
	rejoin map-each token tokens [token/2]
]

token-matching: funct [
	{Rewrite token matching pattern as parse rule.}
	tokens [block!] {Token names.}
	rule [block!] {Parse rule (with token matching extensions).}
	/post post-token {Rule to match after every token.}
][

	if not post [post-token: []]

	either system/version > 2.100.0 [; Rebol3
		match-token: funct [word value][compose/deep [into [(:word) (:value)]] (post-token)]
	][; Rebol2
		match-token: funct [word value][compose/deep [[into [(:word) (:value)] (post-token)]]]
		; Rebol 2 has problem with [... any into ...]
	]

	token-id: parsing-at position [
		if all [word? word: position/1 find tokens word][next position]
	]

	not-marker: parsing-unless ['~]

	; Order and separate rewrites is important.
	rewrite rule [[x: string! not-marker] [(x/1) ~]]
	rewrite rule [[into [x: token-id string! '~]] [(match-token to lit-word! x/1 x/2)]]
	rewrite rule [[x: string! '~] [(match-token 'skip x/1)]]
	rewrite rule [[x: token-id] [(match-token to lit-word! x/1 'skip)]]

	rule
]

use [parser][
	c-token: funct [
		{Return token name and end position.}
		input
	] compose [
		(
			parser: context [
				name: none
				grammar: make c.lexical/grammar []
				terms: exclude union grammar/white-space grammar/preprocessing-token [|]
				foreach term terms [
					grammar/:term: compose/only [(get term) (to paren! compose [name: (to lit-word! term)])]
				]
			]
			'parser
		)
		parse/all/case input [parser/grammar/c-pp-token rest:]
		if rest [reduce [parser/name rest]]
	]
]

c-src: context [

	grammar: context [

		rule: [some segment]

		segment: [
			function-section
			| line-comment
			| other-section
		]

		function-section: [
			opt intro-comment
			function.decl
			function.body
		]

		to-function: [any [not-function-section segment]]
		not-function-section: parsing-unless function-section

		intro-comment: [some line-comment] ; Does not check for whitespace because it is ... into ['line-comment skip]

		not-intro: parsing-unless intro-comment

		other-section: [some [not-intro skip]]

		function.decl: [
			function.words function.args
			is-lbrace
		]

		function.words: [function.id any function.id opt [function.star function.id]]
		function.args: ["(" any [function.id | not-rparen punctuator] ")"]
		function.id: [identifier]
		function.star: "*"

		function.body: [braced]

		braced: [
			is-lbrace skip
			some [
				not-rbrace [braced | skip]
			]
			"}"
		]

		is-lbrace: parsing-when [punctuator "{"]
		not-rbrace: parsing-unless [punctuator "}"]
		not-rparen: parsing-unless [punctuator ")"]
	]

]

prs: context [

	grammar-tokens: [
		identifier
		pp-number
		character-constant
		string-literal
		header-name
		punctuator
		other-pp-token
	]

	grammar: make c-src/grammar []
	terms: words-of grammar

	; Rewrite terms to recognise token blocks.

	white-space: [eol | nl | wsp | span-comment | line-comment]

	whitespace-tokens: exclude white-space [|]
	tokens: union grammar-tokens whitespace-tokens

	use [rule][
		token-matching whitespace-tokens white-space
		foreach term terms [
			rule: copy/deep compose [(get term)]
			token-matching/post tokens rule [any white-space]
			grammar/:term: rule
		]
	]
]

comment {
text: read %../github-repos/ren-c/src/core/n-system.c
text: read %../github-repos/temporary.201508-source-format-change/src/core/n-system.c
r: tokenise/shared :c-token text
parse r/1 [some [into [x: (new-line/all x false) 'eol skip] x: (new-line x true) | skip]]

terms: words-of prs/grammar
remove-each x terms [parse/all form x [[thru {not-} | thru {is-}] to end]]
visualise-parse r/1 prs/grammar [t: get-parse/terminal [parse r/1 prs/grammar/rule] terms bind [function.id] prs/grammar]


prettify-tree t
using-tree-content t
wc t

}