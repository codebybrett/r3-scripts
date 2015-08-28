REBOL [
	purpose: {Automatic source code modifications.}
	author: {Brett Handley}
	license: {Apache 2.0}
]

; --------------------------------------------------------------
; Change the CONFIG section below as necessary.
; Debugging mode writes old and new files to subfolders of current.
;
; INITIAL CONVERSION of old format to new
;
;	Run it once for the conversion to the new format.
;
;
; NORMAL USAGE:
;
;	source-tool/update/all
;
; AD HOC USAGE:
;
;	source-tool/init ; Need this to load and index data.
;
;	help source-tool/list
;
; Interrogate the source definition of CONTEXT (including c source filename):
;
;	source-tool/init
;	print mold source-tool/c-source/rebnative-index/context
;
; OBJECTIVES:
;
;	* Write function specification into c source file comments.
;	* Index native function arguments.
;	* Not write files unless necessary.
;	* Not overwrite any existing notes in the comments.
;	* Be robust in the face of introduced parse rule bugs.
;
; NOTE:
;	Some c identifiers are different to the words they define.
;	- See ID-TO-WORD for the mapping.
;
; --------------------------------------------------------------

do %comment-blocks.reb

do %apropos.reb
do %parse-kit.reb
do %trees.reb

source-tool: context [

	; --- Config 

	boot.natives.file: %../github-repos/ren-c/src/boot/natives.r
	core.source.folder: %../github-repos/ren-c/src/core/

	log: func [message] [print mold new-line/all compose/only message false]
	debug: func [message] [print mold new-line/all compose/only message false] ; use none to turn off.
	timing: funct [code /local result] [
		started: now/precise
		log [started (started) (code)]
		set/any 'result do code
		finished: now/precise
		log [finished (difference finished started) (code)]
		get/any 'result
	]

	; --- End Config

	stats: context [
		parsed: none
		not-parsed: none
		comments-updated: none
		files-written: none
	]


	init: func [{Load and index the data.}] [

		reset ; Start fresh.

		timing [r-source/process]
		timing [c-source/process]

		log [words-missing-specs (new-line/all list/missing/specs false)]
		log [words-missing-rebnatives (new-line/all list/missing/rebnatives false)]
	]

	list: context [

		missing: context [

			rebnatives: func [] [
				exclude r-source/native/names c-source/rebnatives
			]

			specs: func [] [
				exclude c-source/rebnatives r-source/native/names
			]

		]
	]

	native: context [

		meta-of: func [
			{Return function metadata.}
			name
			/local c-info r-info summary notes data
		] [

			c-info: c-source/rebnative-index/:name
			r-info: attempt [r-source/native/cache/:name]

			if r-info [
				summary: if string? first r-info [first r-info]
				data: synopsis r-info
			]

			pretty-spec compose/only [
				Name: (form name)
				Summary: (summary)
				Details: (c-info/meta/details)
				Spec: (data)
			]
		]

		synopsis: funct [spec /local block] [

			block: pretty-spec collect [

				count: 0

				foreach x spec [
					if any [word? x refinement? x] [
						count: count + 1
						keep to-tag count
						keep :x
					]
				]
			]

			if not empty? block [block]
		]
	]

	reset: func [{Clear caches.}] [

		r-source/reset
		c-source/reset

		set stats none
	]

	update: context [

		all: func [] [
			init
			comments
			files

			log [stats (body-of stats)]

			reset ; Allow caches to be garbage collected.
		]

		comments: func [{Update source comments (in-memory).}] [

			debug [update-comments]

			apropos c-source [

				foreach id identifiers [comment/update id]
			]

			exit
		]

		files: func [{Write changes to source files.}] [

			debug [update-files]

			apropos c-source [
				foreach name list/changed [file/update name]
			]

			exit
		]

	]

	c-source: context [

		decl-list: none
		rebnative-index: none

		comment: context [

			format: func [
				spec
				/local text bol
			] [

				rejoin [
					{/*} line* width newline
					mold-comment spec
					line* width {*/} newline newline
				]
			]

			line*: func [{Return a line of *.} count] [

				head insert/dup copy {} #"*" count
			]

			load: func [string /local lines] [

				if none? string [return none]

				parse/all string [
					{/*} 20 200 #"*" newline
					copy lines some [{**} [newline | #" " thru newline]]
					20 200 #"*" #"/" newline
					to end
				]

				if lines [load-comment lines]
			]

			update: func [name [word!] /local c-info] [

				log [update-comment (:name)]

				rebnative-index/(:name)/intro: format native/meta-of name
				stats/comments-updated: 1 + any [stats/comments-updated 0]
			]

			width: 78
		]

		file: context [

			cache: none

			declarations: funct [name] [
	
				result: make block! []
	
				tree: attempt [cache/:name/tree]
				if not tree [return result]
	
				position: at tree 4
	
				forskip position 2 [

					pattern: first position

					if all [
						word: in text/declaration-parsers pattern/1
						def: do get word position
					] [
						insert def reduce ['file name]
						append/only result def
					]
				]

				result	
			]

			list: func [/local ugly-tmp-var] [

				remove-each name ugly-tmp-var: read core.source.folder [
					not parse/all name [thru %.c]
				]

				ugly-tmp-var
			]

			process: func [file /local source tree] [

				log [process-c (file)]

				if not cache [cache: make block! 200]

				source: read/string core.source.folder/:file
				tree: text/parse-source source

				either tree [
					stats/parsed: 1 + any [stats/parsed 0]
				] [
					log [not-parsed (file)]
					stats/not-parsed: 1 + any [stats/not-parsed 0]
					return none
				]

				debug [tokenised-c (file)]

				if not equal? source text/regenerate tree [
					do make error! reform [{Tree for} mold file {does not represent the source file.}]
				]

				debug [check-regenerated-c (file)]

				if not find cache file [append cache reduce [file none]]
				cache/(file): compose/only [
					source (source)
					tree (tree)
				]

				debug [cached (file)]
			]

			respecified?: func [file] [

				not equal? cache/(file)/source source-for file
			]

			source-for: func [file] [

				text/regenerate cache/(file)/tree
			]

			update: func [name /local old new] [

				old: cache/(name)/source
				new: source-for name

				if not equal? old new [

					write core.output.folder/:name new
					stats/files-written: 1 + any [stats/files-written 0]
					log [wrote (:name)]

					cache/(name)/source: new
				]

				exit
			]

		]

		id-to-word: func [{Translate C identifier to Rebol word.} identifier] [

			id: copy identifier
			replace/all id #"_" #"-"
			if #"q" = last id [change back tail id #"?"]

			to word! id
		]

		rebnatives: func [] [

			if none? rebnative-index [do make error! {No source comments loaded. Use /init.}]
			extract rebnative-index 2
		]

		rebnative?: funct [def][

			if def/name = "REBNATIVE" [
				if not def/single-param [
					?? def
					do make error! reform [{Expected REBNATIVE to have single parameter.}]
				]
				true
			]
		]

		indexing: func [/local id] [

			foreach name file/list [
				debug [indexing-file (name)]
				index-file name
			]

			rebnative-index: collect [

				foreach def decl-list [

					if rebnative? def [

						keep id: id-to-word def/param

						keep/only compose/only [
							c.id (def/param)
							def (def)
						]
					]
				]
			]

			sort/skip rebnative-index 2
			new-line/all/skip rebnative-index true 2
		]

		index-file: func [name][

			if not decl-list [decl-list: make block! []]

			append decl-list file/declarations name
		]

		list: context [

			files: func [{Cached files.}] [

				extract file/cache 2]

			changed: func [{Files that have had their contents changed.}] [

				exclude files unchanged]

			unchanged: func [{Unchanged files.} /local ugly-tmp-var] [

				remove-each name ugly-tmp-var: files [file/respecified? name]
				ugly-tmp-var
			]
		]

		parsing: func [] [

			foreach name file/list [file/process name]
		]

		process: func [] [

			reset
			timing [parsing]
			timing [indexing]
			exit
		]

		reset: func [] [

			file/cache: none
			decl-list: none
			rebnative-index: none
		]

		text: context [

			declaration-parsers: context [
	
				; TODO: Is there a simpler way to get info from tree while checking assumptions?
	
				assert-node: funct [
					{Check node at child slot position.}
					condition [word! block!] {Check condition for NODE, POSITION.}
					position
				][
	
					if word? condition [condition: compose [(to lit-word! condition) = node/1]]
	
					if not attempt [
						node: position/1
						do bind bind/copy condition 'node 'position
					][
						?? position
						do make error! reform [{Node does not satisfy} mold condition]
					]
	
					node
				]
	
				old-style-decl: funct [ref /structure] [
	
					node: ref/1
					string: node/3/content
	
					apropos text/parser/grammar [
	
						tree: get-parse [parse/all string old-style-decl] [
							decl.words decl.args.single decl.args.multi c.id comment.notes
						]
					]
	
					using-tree-content tree
	
					if structure [return tree] ; Used for debugging.
	
					position: at tree 4
	
					if 'comment.notes = position/1/1 [
						pre-notes: position/1/3/content
						position: next position
					]
	
					decl.words: assert-node 'decl.words position
					childslot: at decl.words 4
					decl.words: collect [
						forall childslot [
							c.id: assert-node 'c.id childslot
							keep c.id/3/content
						]
					]
	
					name: last decl.words
					clear back tail decl.words
	
					position: next position
					decl.args: position/1
					either single-param: equal? 'decl.args.single decl.args/1 [
						c.id: assert-node 'c.id at decl.args 4
						param: c.id/3/content
					][
						assert-node 'decl.args.multi position
						param: decl.args/3/content
					]
	
					position: next position
					if all [
						not tail? position
						position/1/1 = 'comment.notes
					][
						post-notes: position/1/3/content
					]
	
					compose/only [
						name (name)
						keywords (decl.words)
						single-param (single-param)
						param (param)
						pre-notes (pre-notes)
						post-notes (post-notes)
						token (index? ref)
					]
				]
			]
	
			parser: context [

				guard: pos: none

				charsets: context [

					id.nondigit: charset [#"_" #"a" - #"z" #"A" - #"Z"]
					id.digit: charset {0123456789}
					id.rest: union id.nondigit id.digit
				]

				grammar: context bind [

					rule: [opt file-comment some pattern rest]
					file-comment: [comment.multline]
					pattern: [old-style-decl | new-style-decl | to-next]
					old-style-decl: [
						[comment.decorative | comment.multline]
						wsp decl
						[comment.decorative | comment.multline]
						any newline #"{"
					]
					new-style-decl: [
						comment.banner
						decl
						any newline #"{"
					]
					comment: [comment.multline | comment.decorative]
					to-next: [to {^//*} newline]
					rest: [to end]

					decl: [decl.words #"(" decl.args #")" opt wsp newline]
					decl.words: [c.id any [wsp c.id]]
					decl.args: [decl.args.single | decl.args.multi]
					decl.args.single: [c.id pos: #")" :pos]
					decl.args.multi: [to #")"]
					c.id: [id.nondigit any id.rest]

					comment.banner: [{/*} stars newline opt comment.notes stars {*/}]
					comment.decorative: [{/*} some [stars | wsp | newline] {*/}]
					comment.multline: [{/*} opt stars newline opt comment.notes opt stars {*/}]
					comment.notes: [some comment.note.line]
					comment.note.line: [[{**} comment.note.text | stars] newline]
					comment.note.text: [some [not-eoc skip]]

					stars: [#"*" some #"*" opt [pos: #"/" (pos: back pos) :pos]]

					not-eoc: either system/version > 2.100.0 [; Rebol3
						[not [stars #"/" | newline]]
					] [; Rebol2
						[(guard: [none]) [opt [[stars | newline] (guard: [end skip])] guard]]
					]

					wsp: compose [some (charset { ^-})]

				] charsets
				; Processed using action injection

			]

			parse-source: func [
				{Parse tree structure from the source.}
				string
				/local parsed result terms
			] [

				terms: bind [
					file-comment
					old-style-decl
					new-style-decl
					to-next rest
				] parser/grammar

				result: get-parse [parsed: parse/all string parser/grammar/rule] terms
				if not parsed [return none]

				prettify-tree using-tree-content result
			]

			regenerate: funct [
				{Generate source text from source.}
				block [block!] {As returned from parse-source.}
			] [
				children: at block 4
				either empty? children [block/3/content] [
					rejoin map-each node children [regenerate node]
				]
			]

		]

	]

	pretty-spec: func [block] [

		new-line/all block false
		new-line/all/skip block true 2
	]

	r-source: context [

		native: context [

			cache: none

			names: func [] [

				if none? cache [do make error! {No specifications loaded. Use /init.}]
				extract cache 2
			]

			processing: func [
				/local block errors name spec position cache-item file
			] [

				file: boot.natives.file
				log [process-natives (file)]

				if not cache [cache: make block! 200]

				block: load file

				cache-item: func [] [
					name: to word! :name
					if not find cache name [append cache reduce [:name none]]
					cache/(:name): spec
				]

				if not parse block [
					some [position:
						set name set-word! 'native set spec block! (cache-item)
					]
				] [
					do make error! reform [{File} mold file {has unexpected format at position.} index? position]
				]

				sort/skip cache 2
				spec
			]

		]

		process: func [] [

			reset
			native/processing
		]

		reset: func [] [

			native/cache: none
		]
	]

]
