REBOL []

do %source-tool.reb
do %comment-blocks.test.reb

; ---------------------------------------------------------------------------
; USAGE
;
;	test.source-tool <path-to-you-core-folder> <path-to-your-output-folder>
;
;     Will write log and output subfolders to that folder.
;
; ---------------------------------------------------------------------------

test.source-tool: func [

	src-folder {Specify path to /src}
	test-folder {Specify test output folder for output.}
][

	?? src-folder
	?? test-folder

	core.folder: src-folder/%core/
	conversion.folder: test-folder/%src.conversion/
	roundtrip.folder: test-folder/%src.roundtrip/

	?? core.folder
	?? conversion.folder
	?? roundtrip.folder


	apropos source-tool [

		; --- Common config.

		boot.natives.file: src-folder/boot/natives.r
		logfile: clean-path test-folder/source-tool.log.txt
		debug: :log

		attempt [delete logfile]

		; --- Run conversion.

		make-dir/deep conversion.folder

		foreach file read conversion.folder [delete conversion.folder/:file]
		foreach file read core.folder [write conversion.folder/:file read core.folder/:file]

		core.source.folder: core.folder
		core.output.folder: conversion.folder
		update/all

		; --- Run roundtrip.

		make-dir/deep roundtrip.folder

		foreach file read roundtrip.folder [delete roundtrip.folder/:file]
		foreach file read conversion.folder [write roundtrip.folder/:file read conversion.folder/:file]

		core.source.folder: conversion.folder
		core.output.folder: roundtrip.folder
		update/all
	]

	requirements 'source-tool [

		[{Conversion folder should have same file names as original.}
			equal? read conversion.folder read core.folder
		]

		[{Roundtrip folder sould have same file names as conversion folder.}
			equal? read conversion.folder read roundtrip.folder
		]

		[{Roundtrip files should not have changed from conversion.}
			all map-each file read conversion.folder [
				equal? read conversion.folder/:file read roundtrip.folder/:file
			]
		]
	]
]
