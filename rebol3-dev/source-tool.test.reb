REBOL []

do %source-tool.reb
do %comment-blocks.test.reb

; -- CONFIG --------------------------------------

original.folder: source-tool/core.source.folder
conversion.folder: %test/new/
roundtrip.folder: %test/new.roundtrip/

; ------------------------------------------------

print "Creates new folders and logfile."

test-tool: func [][

	apropos source-tool [

		logfile: clean-path %test/source-tool.log.txt

		; Run conversion.

		make-dir/deep conversion.folder

		foreach file read conversion.folder [delete conversion.folder/:file]
		foreach file read original.folder [write conversion.folder/:file read original.folder/:file]
		core.source.folder: original.folder
		core.output.folder: conversion.folder
		update/all

		; Run roundtrip.

		make-dir/deep roundtrip.folder

		foreach file read roundtrip.folder [delete roundtrip.folder/:file]
		foreach file read conversion.folder [write roundtrip.folder/:file read conversion.folder/:file]
		core.source.folder: conversion.folder
		core.output.folder: roundtrip.folder
		update/all
	]

	requirements 'source-tool [

		[{Conversion folder should have same file names as original.}
			equal? read conversion.folder read original.folder
		]

		[{Roundtrip folder sould have same file names as conversion folder.}
			equal? read conversion.folder read roundtrip.folder
		]

		[{Roundtrip files should not have changed from conversion.}
			all map-each file read conversion.folder [
				equal? read conversion.folder/:file roundtrip.folder/:file
			]
		]
	]
]

test-tool