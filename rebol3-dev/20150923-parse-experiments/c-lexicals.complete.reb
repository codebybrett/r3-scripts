REBOL [
	Title: "C Programming Language Lexical Definitions"
	Version: 1.0.0
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Parse C source text into tokens.}
]

;
; Based upon N1570 Committee Draft — April 12, 2011 ISO/IEC 9899:201x
;
; Trigraphs are not implemented.
;
; Do not put any actions in this file.
;
; To use these rules, copy them, call them from your own rules or
; use rule injection to dynamically add emit actions.
;
;
; Source text can be parsed (lexically) into preprocessing tokens using c-pp-token.
;
; c-token represents the tokens used by the phrase structure grammar.
;

c.lexical: context [

	grammar: [

		text: [some c-pp-token]

		c-pp-token: [
			white-space
			| preprocessing-token
		]

		c-token: [
			keyword
			| identifier
			| constant
			| string-literal
			| punctuator
		]

		white-space: [
			nl
			| eol
			| wsp
			| span-comment
			| line-comment
		]

		;
		; -- A.1.1 Lexical Elements
		; Order is significant

		preprocessing-token: [

			pp-number
			| character-constant
			| identifier
			| string-literal
			| header-name
			| punctuator
			| other-pp-token
		]

		other-pp-token: not-wsp

		;
		; -- A.1.2 Keywords

		keyword: [
			"auto" | "break" | "case" | "char" | "const"
			| "continue" | "default" | "do" | "double" | "else"
			| "enum" | "extern" | "float" | "for" | "goto"
			| "if" | "inline" | "int" | "long" | "register"
			| "restrict" | "return" | "short" | "signed" | "sizeof"
			| "static" | "struct" | "switch" | "typedef" | "union"
			| "unsigned" | "void" | "volatile" | "while" | "_Alignas"
			| "_Alignof" | "_Atomic" | "_Bool" | "_Complex" | "_Generic"
			| "_Imaginary" | "_Noreturn" | "_Static_assert" | "_Thread_local"
		]

		;
		; -- A.1.3 Identifiers

		identifier: [id.nondigit any id.char]
		id.nondigit: [nondigit | universal-character-name]

		;
		; -- A.1.4 Universal character names

		universal-character-name: [{\U} 2 hex-quad | {\u} hex-quad]
		hex-quad: [4 hexadecimal-digit]

		;
		; -- A.1.5 Constants

		constant: [
			| integer-constant
			| floating-constant
			| enumeration-constant
			| character-constant
		]

		integer-constant: [
			| decimal-constant opt integer-suffix
			| octal-constant opt integer-suffix
			| hexadecimal-constant opt integer-suffix
		]

		decimal-constant: [
			nonzero-digit any digit
		]

		octal-constant: [
			#"0" any octal-digit
		]

		hexidecimal-constant: [
			hexadecimal-prefix some hexadecimal-digit
		]

		hexadecimal-prefix: [{0x} | {0X}]

		integer-suffix: [
			unsigned-suffix long-long-suffix
			| unsigned-suffix opt long-suffix
			| long-long-suffix opt unsigned-suffix
			| long-suffix opt unsigned-suffix
		]

		unsigned-suffix: [#"u" | #"U"]

		long-suffix: [#"l" | #"L"]

		long-long-suffix: ["ll" | "LL"]

		floating-constant: [
			decimal-floating-constant
			| hexadecimal-floating-constant
		]

		decimal-floating-constant: [
			fractional-constant opt exponent-part opt floating-suffix
			| digit-sequence exponent-part opt floating-suffix
		]

		hexadecimal-floating-constant: [
			hexadecimal-prefix hexadecimal-fractional-constant
			| binary-exponent-part opt floating-suffix
			| hexadecimal-prefix hexadecimal-digit-sequence
			| binary-exponent-part opt floating-suffix
		]

		fractional-constant: [
			opt digit-sequence #"." digit-sequence
			| digit-sequence #"."
		]

		exponent-part: [
			[#"e" | #"E"] opt sign digit-sequence
		]

		digit-sequence: [digit some digit]

		hexadecimal-fractional-constant: [
			opt hexadecimal-digit-sequence #"." hexadecimal-digit-sequence
			| hexadecimal-digit-sequence #"."
		]

		binary-exponent-part: [
			[#"p" | #"P"] signopt digit-sequence
		]

		hexadecimal-digit-sequence: [
			hexadecimal-digit any hexadecimal-digit
		]

		floating-suffix: [#"f" | #"l" | #"F" | #"L"]

		enumeration-constant: [identifier]

		character-constant: [
			#"'" some c-char #"'"
			| {L'} some c-char #"'"
			| {u'} some c-char #"'"
			| {U'} some c-char #"'"
		]

		escape-sequence: [
			simple-escape-sequence
			| octal-escape-sequence
			| hexadecimal-escape-sequence
			| universal-character-name
		]

		simple-escape-sequence: [
			{\'} | {\"} | {\?} | {\\}
			| {\a} | {\b} | {\f} | {\n} | {\r} | {\t} | {\v}
		]

		hexadecimal-escape-sequence: [{\x} hexadecimal-digit any hexadecimal-digit]

		octal-escape-sequence: [#"\" 1 3 octal-digit]

		;
		; -- A.1.6 String literals

		string-literal: [
			opt encoding-prefix #"^"" any s-char #"^""
		]
		encoding-prefix: [{u8} | #"L" | #"u" | #"U"]
		s-char: [s-char.cs | escape-sequence]

		;
		; -- A.1.7 Punctuators

		punctuator: [
			{->} | {++} | {--} | {<<} | {>>} | {<=} | {>=} | {==} | {!=}
			| {&&} | {||} | {...} | {*=} | {/=} | {%=} | {+=} | {<<=} | {>>=}
			| {&=} | {^^=} | {|=} | {##} | {<:} | {:>} | {<%} | {%>}
			| {%:%:} | {%:}
			| p-char
		]

		;
		; -- A.1.8 Header names

		header-name: [#"<" some h-char #">" | #"^"" some q-char #"^""]

		;
		; -- A.1.9 Preprocessing numbers

		pp-number: [
			[digit | #"." digit]
			any [
				digit
				| id.nondigit
				| #"."
				| [#"e" | #"p" | #"E" | #"P"] sign
			]
		]

		;
		; -- Whitespace

		nl: {\^/} ; Line break in logical line.
		eol: newline ; End of logical line.
		wsp: [some ws-char]
		span-comment: [{/*} thru {*/}]
		line-comment: [{//} to newline]

	]

	charsets: context [

		; Header name
		h-char: complement charset {^/<}
		q-char: complement charset {^/"}

		; Identifier
		nondigit: charset [#"_" #"a" - #"z" #"A" - #"Z"]
		digit: charset {0123456789}
		nonzero-digit: charset {123456789}
		octal-digit: charset {01234567}
		id.char: union nondigit digit
		hexadecimal-digit: charset [#"0" - #"9" #"a" - #"f" #"A" - #"F"]

		; pp-number
		sign: charset {+-}

		; character-constant
		c-char: complement charset {'\^/}

		; string-literal
		s-char.cs: complement charset {"\^/}

		; punctuator
		p-char: charset "[](){}.&*+-~!/%<>^^|?:;=,#"

		; whitespace
		ws-char: charset { ^-^/^K^L}
		not-wsp: complement ws-char
	]

	grammar: context bind grammar charsets
	; Grammar defined first in file.
]
