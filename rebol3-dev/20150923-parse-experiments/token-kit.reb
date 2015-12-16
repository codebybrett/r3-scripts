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

; -------------------------------------------------------------------------------
;
; tokenise
;
;	Tokenise input into tokens.
;
;	A token matching function is passed in which returns the token name
;	and the input position following the token.
;
;	A /shared refinement is used to save on memory usage by indexing the
;	tokens into dictionaries indexed by token name.
;
; join-tokens
;
;	Simple function to regenerate the original input from the tokens.
;
; token-matching
;
;	Rewrites token match patterns with parse rules.
;
;	Tokens are encodeded in the input as blocks of word and string. The idea
;	is to have simiplified patterns that are rewritten into parse rules to
;	match these token blocks. By using simplified token match patterns,
;	parse rules for non-terminals can be simplified and reused, uncomplicated
;	by the mechanics of token matching.
;
;	To identify tokens, as opposed to non-terminals, a list of all tokens
;	is given to the function.
;
;	Three match patterns are supported.
;
;	word! - matches by token name
;	string! - match by token string
;	[word! string!] - match both by token name and token string
;
; -------------------------------------------------------------------------------

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
	{Rewrite abbreviated token matching patterns as parse rule.}
	tokens [block!] {Token names.}
	rule [block!] {Parse rule (with token matching extensions).}
	/pre pre-token {Rule to match before every token.}
	/post post-token {Rule to match after every token.}
][

	if not pre [pre-token: []]
	if not post [post-token: []]

	either system/version > 2.100.0 [; Rebol3
		match-token: funct [word value][compose/deep [(pre-token) into [(:word) (:value)] (post-token)]]
	][; Rebol2
		match-token: funct [word value][compose/deep [[(pre-token) into [(:word) (:value)] (post-token)]]]
		; Rebol 2 has problem with [... any into ...]
	]

	token-id: parsing-at position [
		if all [word? word: position/1 find tokens word][next position]
	]

	not-marker: parsing-unless ['~]

	; Separating rewrites and ordering them is important.
	rewrite rule [[x: string! not-marker] [(x/1) ~]]
	rewrite rule [[into [x: token-id string! '~]] [(match-token to lit-word! x/1 x/2)]]
	rewrite rule [[x: string! '~] [(match-token 'skip x/1)]]
	rewrite rule [[x: token-id] [(match-token to lit-word! x/1 'skip)]]

	rule
]
