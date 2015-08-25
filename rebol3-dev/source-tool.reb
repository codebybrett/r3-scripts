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
;	print mold source-tool/c-source/index/context
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

source-tool: context [

	; --- Config 

	boot.natives.file: %../github-repos/ren-c/src/boot/natives.r
	core.source.folder: %../github-repos/ren-c/src/core/

	log: func [message] [print mold new-line/all compose/only message false]

	; --- End Config

	stats: context [
		comments-updated: none
		files-written: none
	]


	init: func [{Load and index the data.}] [

		reset ; Start fresh.

		r-source/process
		c-source/process

		log [words-missing-specs (new-line/all list/missing/specs false)]
		log [words-missing-comments (new-line/all list/missing/comments false)]
	]

	list: context [

		missing: context [

			comments: func [] [
				exclude r-source/native/names c-source/identifiers
			]

			specs: func [] [
				exclude c-source/identifiers r-source/native/names
			]

		]

		details: func [{Words that have details in the comments.} /local ugly-tmp-var] [

			if none? c-source/index [do make error! {No source comments loaded. Use /init.}]
			remove-each [word def] ugly-tmp-var: copy c-source/index [none? def/meta/details]
			map-each [word def] ugly-tmp-var [word]
		]


		paired-words: func [{Words found in rebol spec (r-source) and c sources (c-source)}] [
			intersect r-source/native/names c-source/identifiers
		]

	]

	native: context [

		meta-of: func [
			{Return function metadata.}
			name
			/local c-info r-info summary notes data
		] [

			c-info: c-source/index/:name
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

			apropos c-source [

				foreach id identifiers [comment/update id]
			]

			exit
		]

		files: func [{Write changes to source files.}] [

			apropos c-source [

				foreach name list/changed [file/update name]
			]

			exit
		]

	]

	c-source: context [

		index: none

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

			load: func [string /local lines][

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

				index/(:name)/intro: format native/meta-of name
				stats/comments-updated: 1 + any [stats/comments-updated 0]
			]

			width: 78
		]

		file: context [

			cache: none

			list: func [/local ugly-tmp-var] [

				remove-each name ugly-tmp-var: read core.source.folder [
					not parse/all name [thru %.c]
				]

				ugly-tmp-var
			]

			process: func [file /local source tokens] [

				if not cache [cache: make block! 200]

				source: read/string core.source.folder/:file
				tokens: text/tokenise source

				if not equal? source text/regenerate tokens [
					do make error! reform [{Tokens for} mold file {do not represent the source file.}]
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

		identifiers: func [] [

			if none? index [do make error! {No source comments loaded. Use /init.}]
			extract index 2
		]


		indexing: func [/local oneoff-note-conversion] [


			oneoff-note-conversion: funct [name [word!] /local c-info] [

				; --------------------------------------------------------
				; Convert from original format for comments to new format.
				; TODO: Once done, this function will become obsolete.
				; --------------------------------------------------------

				notes: attempt [copy index/(:name)/post-decl]

				if not notes [exit]

;				remove/part notes find notes newline
;				trim/tail notes

				replace/all notes tab {  }

				if empty? notes [notes: none]

				c-info: index/(:name)
				if none? c-info/meta [
					c-info/meta: compose [details: none]
				]
				c-info/meta/details: notes
				c-info/post-decl: none ; Obliterate the following comment.

				log [notes-converted (:name)]

				exit
			]


			if none? file/cache [
				do make error! {Indexing requires files to be loaded in the file cache. Use /init, check core folder.}
			]

			index: make block! 100

			use [rebnative name] [

				rebnative: func [def /local meta] [

					use [id] [

						id: id-to-word def/id

						if find index id [
							do make error! reform [{Expected} mold def/identifer {to be declared only once.}]
						]

						append index reduce [id def]

						insert def compose [file (name)]
						append def compose/only [meta (none)]

						either def/is-new-format [
							def/meta: construct comment/load def/intro
						][
							def/meta: context [Details: none]
							oneoff-note-conversion id
						]
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

		process: func [] [

			reset
			foreach name file/list [file/process name]
			indexing
			exit
		]

		reset: func [] [

			file/cache: none
			index: none
		]

		text: context [

			bmrk: pos: result: segment: text: this: guard: none
			wsp: charset {^- }

			grammar: context [

				rule: [bmrk: some segment to end emit]
				segment: [thru-keyword identifier opt post-decl bmrk:]
				thru-keyword: [to {REBNATIVE} emit (native)]
				identifier: [thru #"(" bmrk: to #")" dup (this/id: text) skip newline]
				post-decl: [bmrk: any newline {/*} thru newline opt post-decl.lines thru {*/} dup (this/post-decl: text)]
				post-decl.lines: [some [any #"*" some wsp thru newline]]

				dup: [pos: (text: copy/part bmrk pos)]
				emit: [dup (append result text)]

			] ; Easier to debug than a monolithic rule.

			native: func [/local txt intro line not-eol new-format pos] [

				if txt: find/last last result {/*} [

					; Logic for new format... compatible with Rebol 2 for the moment.
					not-eol: complement charset {^/}
					line: [
						"**" any not-eol
						opt [pos: (if #"/" = first back pos [pos: back pos]) :pos]
						newline
					]
					either all [
						parse/all txt [
							{/*} 20 200 #"*" newline
							some line
							20 200 #"*" #"/" newline
							any newline
						]
					][
						new-format: true
						intro: copy txt
						clear txt
					][

						; Logic for old format.... TODO: eventually to be removed.
						; Grab comment before declaration.
						; Need to be sure it's our comment and not some earlier comment.
						use [chars][
							chars: charset {/* ^-^/}
							if parse/all txt compose [some chars] [
								intro: copy txt
								clear txt
							]
						]
					]
				]

				append result reduce [
					'rebnative
					this: compose [
						id (none)
						intro (any [intro {}])
						post-decl (none)
						is-new-format (new-format)
						meta (none)
					]
				]
			]

			rebnative: func [source /local post-decl] [

				post-decl: any [source/post-decl copy {}]

				rejoin [
					source/intro {REBNATIVE} "(" source/id ")^/" post-decl
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
				/local block errors name spec position cache-item
			] [

				if not cache [cache: make block! 200]

				block: load boot.natives.file

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
