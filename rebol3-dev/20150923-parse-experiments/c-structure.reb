REBOL [
	Title: "C Programming Language Structure Definitions"
	Rights: {
		Copyright 2015 Brett Handley
	}
	License: {
		Licensed under the Apache License, Version 2.0
		See: http://www.apache.org/licenses/LICENSE-2.0
	}
	Author: "Brett Handley"
	Purpose: {Parse C source structure.}
]

;
; Based upon N1570 Committee Draft — April 12, 2011 ISO/IEC 9899:201x
;
; Do not put any actions in this file.
;
; To use these rules, copy them, call them from your own rules or
; use rule injection to dynamically add emit actions.
;

c.structure: context [

	grammar: context bind [

		;
		; -- A.2.1 Expressions

		primary-expression: [
			identifier
			| constant
			| string-literal
			| #"(" expression #")"
			| generic-selection
		]

		generic-selection: [
			{_Generic} #"(" assignment-expression #"," generic-assoc-list #")"
		]

		generic-assoc-list: [
			generic-association any [#"," generic-association]
		]

		generic-association: [
			{default} #":" assignment-expression
			| type-name #":" assignment-expression
		]

		postfix-expression: [
			primary-expression
			| postfix-expression [
				#"[" expression #"]"
				| #"(" opt argument-expression-list #")"
				| [#"." | {->}] identifier
				| [{++} | {--}]
			]
			| #"(" type-name #")" #"{" initializer-list opt #"," #"}"
		]

		argument-expression-list: [
			assignment-expression any [#"," assignment-expression]
		]

		unary-expression: [
			| postfix-expression
			| {++} unary-expression
			| {--} unary-expression
			| unary-operator cast-expression
			| {sizeof} #"(" type-name #")"
			| {sizeof} unary-expression
			| {_Alignof} #"(" type-name #")"
		]

		unary-operator: [#"&" | #"*" | #"+" | #"-" | #"~" | #"!"]

		cast-expression: [
			unary-expression
			| #"(" type-name #")" cast-expression
		]

		multiplicative-expression: [
			cast-expression
			| multiplicative-expression [#"*" | #"/" | #"%"] cast-expression
		]

		additive-expression: [
			multiplicative-expression
			| additive-expression [#"+" | #"-"] multiplicative-expression
		]

		shift-expression: [
			additive-expression
			| shift-expression [{<<} | {>>}] additive-expression
		]

		relational-expression: [
			shift-expression
			| relational-expression [{<=} | {>=} | #"<" | #">"] shift-expression
		]

		equality-expression: [
			relational-expression
			| equality-expression [{==} | {!=}] relational-expression
		]

		AND-expression: [
			equality-expression
			| AND-expression #"&" equality-expression
		]

		exclusive-OR-expression: [
			AND-expression
			| exclusive-OR-expression #"^^" AND-expression
		]

		inclusive-OR-expression: [
			exclusive-OR-expression
			| inclusive-OR-expression #"|" exclusive-OR-expression
		]

		logical-AND-expression: [
			inclusive-OR-expression
			| logical-AND-expression "&&" inclusive-OR-expression
		]

		logical-OR-expression: [
			logical-AND-expression
			| logical-OR-expression "||" logical-AND-expression
		]

		conditional-expression: [
			logical-OR-expression opt [#"?" expression #":" conditional-expression]
		]

		constant-expression: [conditional-expression]

		;
		; -- A.2.2 Declarations

		declaration: [
			declaration-specifiers opt init-declarator-list #";"
			| static_assert-declaration
		]

		declaration-specifiers: [
			declaration-specifier any declaration-specifier
		]

		declaration-specifier: [
			storage-class-specifier
			| type-specifier
			| type-qualifier
			| function-specifier
			| alignment-specifier
		]

		init-declarator-list: [
			init-declarator any [#"," init-declarator]
		]

		init-declarator: [
			declarator opt [#"=" initializer]
		]

		storage-class-specifier: [
			"typedef"
			| "extern"
			| "static"
			| "_Thread_local"
			| "auto"
			| "register"
		]

		type-specifier: [
			"void"
			| "char"
			| "short"
			| "int"
			| "long"
			| "float"
			| "double"
			| "signed"
			| "unsigned"
			| "_Bool"
			| "_Complex"
			| atomic-type-specifier
			| struct-or-union-specifier
			| enum-specifier
			| typedef-name
		]

		struct-or-union-specifier: [
			struct-or-union opt identifier #"{" struct-declaration-list #"}"
			| struct-or-union identifier
		]

		struct-or-union: [
			"struct"
			| "union"
		]

		struct-declaration-list: [
			some struct-declaration
		]

		struct-declaration: [
			specifier-qualifier-list opt struct-declarator-list #";"
			| static_assert-declaration
		]

		specifier-qualifier-list: [
			specifier-qualifier any specifier-qualifier
		]

		specifier-qualifier: [
			type-specifier
			| type-qualifier
		]

		struct-declarator-list: [
			struct-declarator any [#"," struct-declarator]
		]

		struct-declarator: [
			declarator opt declarator #":" constant-expression
		]

		enum-specifier: [
			"enum" opt identifier #"{" enumerator-list opt #"," #"}"
			| "enum" identifier
		]

		enumerator-list: [
			enumerator any [#"," enumerator]
		]

		enumerator: [
			enumeration-constant opt [#"=" constant-expression]
		]

		atomic-type-specifier: [
			"_Atomic" #"(" type-name #")"
		]

		type-qualifier: [
			"const"
			| "restrict"
			| "volatile"
			| "_Atomic"
		]

		function-specifier: [
			"inline"
			| "_Noreturn"
		]

		alignment-specifier: [
			"_Alignas" #"(" [type-name | constant-expression] #")"
		]

		declarator: [
			opt pointer direct-declarator
		]

		direct-declarator: [
			[identifier | #"(" declarator #")"]
			[
				#"[" [
					opt type-qualifier-list opt assignment-expression
					| "static" opt type-qualifier-list assignment-expression
					| type-qualifier-list "static" assignment-expression
					| opt type-qualifier-list #"*"
				] #"]"
				| #"(" [parameter-type-list | opt identifier-list] #")"
			]
		]

		pointer: [
			#"*" opt type-qualifier-list any ["*" opt type-qualifier-list]
		]

		type-qualifier-list: [
			type-qualifier any type-qualifier
		]

		parameter-type-list: [
			parameter-list opt [#"," "..."]
		]

		parameter-list: [
			parameter-list any [#"," parameter-declaration]
		]

		parameter-declaration: [
			declaration-specifiers [
				declarator | opt abstract-declarator
			]
		]

		identifier-list: [
			identifier any [#"," identifier]
		]

		type-name: [
			specifier-qualifier-list opt abstract-declarator
		]

		abstract-declarator: [
			opt pointer direct-abstract-declarator
			| pointer
		]

		direct-abstract-declarator: [
			#"(" abstract-declarator #")"
			opt direct-abstract-declarator #"[" opt type-qualifier-list opt assignment-expression #"]"
			opt direct-abstract-declarator #"[" "static" opt type-qualifier-list assignment-expression #"]"
			opt direct-abstract-declarator #"[" type-qualifier-list "static" assignment-expression #"]"
			opt direct-abstract-declarator #"[" #"*" #"]"
			opt direct-abstract-declarator #"(" opt parameter-type-list #")"
		]

		typedef-name: [identifier]

		initializer: [
			assignment-expression
			| #"{" initializer-list opt #"," #"}"
		]

		initializer-list: [
			opt designation initializer any [#"," opt designation initializer]
		]

		designation: [
			designator-list #"="
		]

		designator-list: [
			designator any designator
		]

		designator: [
			#"[" constant-expression #"]"
			| #"." identifier
		]

		static_assert-declaration: [
			"_Static_assert" #"(" constant-expression #"," string-literal #")" #";"
		]

		;
		; -- A.2.3 Statements

		statement: [
			labeled-statement
			compound-statement
			expression-statement
			selection-statement
			iteration-statement
			jump-statement
		]

		labeled-statement: [
			identifier #":" statement
			{case} constant-expression #":" statement
			{default} #":" statement
		]

		compound-statement: [#"{" opt block-item-list #"}"]

		block-item-list: [some block-item]

		block-item: [declaration | statement]

		expression-statement: [opt expression #";"]

		selection-statement: [
			{if} #"(" expression #")" statement opt [{else} statement]
			| {switch} #"(" expression #")" statement
		]

		iteration-statement: [
			{while} #"(" expression #")" statement
			| {do} statement {while} #"(" expression #")" #";"
			| {for} #"(" [declaration | opt expression #";"] opt expression #")" statement
			| {for} #"(" opt expression #";" opt expression #")" statement
		]

		jump-statement: [
			{goto} identifier #";"
			| {continue} #";"
			| {break} #";"
			| {return} opt expression #";"
		]

		;
		; -- A.2.4 External definitions

		translation-unit: [some external-declaration]

		external-declaration: [function-definition | declaration]

		function-definition: [
			declaration-specifiers declarator opt declaration-list compound-statement
		]

		declaration-list: [some declaration]

	] c.lexical/grammar

]