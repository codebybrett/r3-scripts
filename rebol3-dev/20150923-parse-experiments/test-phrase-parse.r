REBOL []

do %c-pp-tokeniser.reb
do %token.kit.reb


; Tokenise the C into PP tokens (includes whitespace).
;
; The phrase structure ignore whitespace.
; - What are it's tokens?
; - How do C PP tokens become C tokens?


c-phrase-parser: context [

	grammar-tokens: [
		identifier
		pp-number
		character-constant
		string-literal
		header-name
		punctuator
		other-pp-token
	]

	grammar: make c.structure/grammar []
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
text: read %../github-repos/ren-c/src/core/n-system.c
text: read %../github-repos/temporary.201508-source-format-change/src/core/n-system.c
text: read %../github-repos/temporary.201508-source-format-change/src/core/c-frame.c

r: tokenise/shared get in c-pp-parser 'token text
parse r/1 [some [into [x: (new-line/all x false) 'eol skip] x: (new-line x true) | skip]]

terms: words-of c-src-parser/grammar
remove-each x terms [parse/all form x [[thru {not-} | thru {is-}] to end]]
delta-time [t: get-parse/terminal [parse r/1 c-src-parser/grammar/rule] terms bind [function.id] c-src-parser/grammar]

visualise-parse r/1 c-src-parser/grammar [t: get-parse/terminal [parse r/1 c-src-parser/grammar/rule] terms bind [function.id] c-src-parser/grammar]


prettify-tree t
using-tree-content t
wc t

}