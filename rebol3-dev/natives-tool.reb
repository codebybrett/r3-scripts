REBOL [
	purpose: {Compare and maintain function specs with C code comments.}
	author: {Brett Handley}
	license: {Apache 2.0}
]

; --------------------------------------------------------------
; Change the CONFIG section below as necessary.
; Debugging mode writes old and new files to subfolders of current.
;
; NORMAL USAGE:
;
;	natives-tool/update/all
;
; AD HOC USAGE:
;
;	natives-tool/init ; Need this to load and index data.
;
;	help natives-tool/list
;
; Interrogate the source definition of CONTEXT (shows notes and file):
;
;	print mold natives-tool/c-source/index/context
;
; Objectives:
;
;	* Write function specification into c source file comments.
;	* Index the arguments.
;	* Not write files unless necessary.
;	* Not overwrite any existing notes in the comments.
;	  - Here I used a bar "|" to identify auto-inserted specs.
;	  - Perhaps another scheme can be used if it's considered ugly.
;	* Be robust in the face of introduced parse rule bugs.
;
; Note:
;	Some c identifiers are different to the words they define.
;	- See ID-TO-WORD for the translation.
;
; --------------------------------------------------------------

do %comment-blocks.reb

natives-tool: context [

	; --- Config 

	source-folder: %../github-repos/ren-c/src/
	; path to src/

	log: func [message] [print mold new-line/all compose/only message false]

	debugging: true ; Writes files to %old/ and %new/ folder in the current directory.


	; --- End Config

	boot-folder: source-folder/(%boot/)
	core-folder: source-folder/(%core/)


	init: func [{Load and index the data.}] [

		r-source/process-files
		c-source/process-files

		log [words-missing-specs (new-line/all list/missing/specs false)]
		log [words-missing-comments (new-line/all list/missing/comments false)]
	]

	list: context [

		missing: context [

			comments: func [] [
				exclude r-source/words c-source/comment/words
			]

			specs: func [] [
				exclude c-source/comment/words r-source/words
			]

		]

		notes: func [{Words that have notes in the comments.} /local ugly-tmp-var][

			remove-each [word def] ugly-tmp-var: copy c-source/index [none? def/notes]
			map-each [word def] ugly-tmp-var [word]
		]


		paired-words: func [{Words found in rebol spec (r-source) and c sources (c-source)}] [
			intersect r-source/words c-source/comment/words
		]

	]

	reset: func [{Clear caches.}] [

		r-source/reset
		c-source/reset
	]

	update: context [

		all: func [] [
			init
			comments
			files
		]

		comments: func [{Update source comments (in-memory).}] [

			foreach name list/paired-words [
				c-source/comment/update name r-source/cache/(name)
			]

			exit
		]

		files: func [{Write changes to source files.}] [

			foreach name c-source/list/changed [
				c-source/file/update name
			]

			reset ; Allow caches to be garbage collected.
			exit
		]

	]

	c-source: context [

		index: none

		comment: context [

			generate: func [
				spec
				/local text bol
			] [

				comments: mold spec

				{<TODO>}
			]

			line*: func [] [
				head insert/dup copy {} #"*" 71
			]

			specification: func [
				{Return block specification of native.}
				spec
				/local fn words description summary
			] [

				pad: func [string] [head insert/dup copy string { } 3 - length? string]

				fn: func spec []
				words: reflect :fn 'words

				description: collect [

					if string? first spec [keep first spec]

					for i 1 length? words 1 [
						keep to-tag i
						keep words/:i
					]
				]

				new-line description true
				new-line/all/skip next description true 2

				description

			]

			update: func [name block] [

				log [update-comment (:name)]

				index/(:name)/spec: generate specification block
			]

			words: func [] [

				if none? index [do make error! {No source comments loaded. Use /init.}]
				extract index 2
			]

		]

		file: context [

			cache: none

			list: func [/local ugly-tmp-var] [

				remove-each name ugly-tmp-var: read core-folder [
					not parse/all name [thru %.c]
				]

				ugly-tmp-var
			]

			process: func [file /local source tokens] [

				if not cache [cache: make block! 200]

				source: read/string core-folder/:file
				tokens: text/tokenise source

				if not equal? source text/regenerate tokens [
					do make error! reform [{Tokens for} mold file {do not represent the source file correctly.}]
				]

				if parse tokens [string!] [; No keywords found.
					return none
				]

				if not find cache file [append cache reduce [file none]]
				cache/(file): compose/only [
					source (source)
					tokens (tokens)
				]
			]

			respecified?: func [file] [

				not equal? cache/(file)/source source-for file
			]

			source-for: func [file] [

				text/regenerate cache/(file)/tokens
			]

			update: func [file /local old new] [

				old: cache/(file)/source
				new: source-for file

				if not equal? old new [

					either debugging [
						write join %old/ file old
						write join %new/ file new
					] [
						write core-folder/:file new
					]

					log [updated (:file)]
					cache/(file)/source: new
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

		indexing: func [] [

			index: make block! 100

			use [rebnative name] [

				rebnative: func [def] [

					use [id] [

						id: id-to-word def/id

						if find index id [
							do make error! reform [{Expected} mold def/identifer {to be declared only once.}]
						]

						insert def compose [file (name)]

						append index reduce [id def]
					]

				]

				foreach [file definition] file/cache [
					name: file
					do bind copy definition/tokens 'rebnative
				]

			]

			sort/skip index 2
			new-line/all/skip index true 2
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

		process-files: func [] [


			reset

			foreach name file/list [
				file/process name
			]

			indexing

			exit
		]

		reset: func [] [

			file/cache: none
			index: none
		]

		text: context [

			; It is too hard to parse C source properly (even for just comments).
			; So here we just recognise our own conventions.

			bmrk: position: result: segment: text: this: none
			wsp: charset {^- }

			grammar: context [

				rule: [bmrk: some segment to end emit]
				segment: [thru-keyword identifier comment bmrk:]
				thru-keyword: [to {REBNATIVE} emit (native)]
				identifier: [thru #"(" bmrk: to #")" dup (this/id: text) skip newline]
				comment: [bmrk: any newline {/*} thru newline opt notes bmrk: thru {*/} emit]
				notes: [bmrk: some [any #"*" some wsp thru newline] dup (this/notes: text)]
				dup: [position: (text: copy/part bmrk position)]
				emit: [dup (append result text)]

			] ; Easier to debug than a monolithic rule.

			native: func [/local txt intro chars] [
				chars: charset {/* ^-^/}
				if txt: find/last last result {/*} [
					if parse/all txt compose [some chars] [
						intro: copy txt
						clear txt
					]
				]
				append result reduce [
					'rebnative
					this: compose [
						id none
						intro (any [intro {}])
						spec ({})
						notes ({})
					]
				]
			]

			rebnative: func [source /local post] [

				post: either source/notes [
					join {/*^/} source/notes
				][{}]

				rejoin [
					source/intro {REBNATIVE} "(" source/id ")^/" post
				]
			]

			regenerate: func [
				{Generate source text from tokens.}
				block [block!] {As returned from tokenise.}
			] [
				rejoin block
			]

			tokenise: func [
				{Tokenise the source into a block.}
				string
			] [
				result: make block! 100
				if not parse/all string grammar/rule [
					result: reduce [string]
				]
				result
			]

		]

	]

	r-source: context [

		cache: none

		file: context [

			list: [%natives.r]

			process: func [
				file
				/local block errors name spec position cache-item
			] [

				if not cache [cache: make block! 200]

				block: load boot-folder/:file

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

				spec
			]

		]

		process-files: func [] [

			reset

			foreach name file/list [
				file/process name
			]

			sort/skip cache 2
		]

		reset: func [] [cache: none]

		words: func [] [

			if none? cache [do make error! {No specifications loaded. Use /init.}]
			extract cache 2
		]


	]

]
