REBOL []

do %c-pp-tokeniser.reb
do %token-kit.reb

c-src: context [

	grammar: context [

		rule: [some section]

		section: [
			function-section
			| other-section
		]

		function-section: [function.decl function.body]

		other-section: [some [not-function.decl skip]]

		function.decl: [
			function.words function.args
			is-lbrace
		]

		not-function.decl: parsing-unless function.decl

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

c-src-parser: context [

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
	not-eol: parsing-unless [eol]

	whitespace-tokens: exclude white-space [|]
	tokens: union grammar-tokens whitespace-tokens
	any-eols: token-matching whitespace-tokens [any eol]

	use [rule][
		token-matching whitespace-tokens white-space
		token-matching whitespace-tokens not-eol
		foreach term terms [
			rule: copy/deep compose [(get term)]
;;			token-matching/pre/post tokens rule [any white-space] [any [not-eol white-space] any-eols]
;;			token-matching/post tokens rule [any white-space]
			either parse/all form term [[thru {not-} | thru {is-}] to end] [
				token-matching tokens rule
			][
				token-matching/pre tokens rule [any white-space]
			]
			grammar/:term: rule
		]
	]
]

comment {
text: read %../GitHub/ren-c/src/core/n-system.c
text: read %../GitHub/temporary.201508-source-format-change/src/core/n-system.c
text: read %../GitHub/temporary.201508-source-format-change/src/core/c-frame.c

r: tokenise/shared get in c-pp-tokeniser 'token text
parse r/1 [some [into [x: (new-line/all x false) 'eol skip] x: (new-line x true) | skip]]

terms: words-of c-src-parser/grammar
remove-each x terms [parse/all form x [[thru {not-} | thru {is-}] to end]]
delta-time [t: get-parse/terminal [parse r/1 c-src-parser/grammar/rule] terms bind [function.id] c-src-parser/grammar]

visualise-parse r/1 c-src-parser/grammar [t: get-parse/terminal [parse r/1 c-src-parser/grammar/rule] terms bind [function.id] c-src-parser/grammar]


prettify-tree t
using-tree-content t
wc t

}