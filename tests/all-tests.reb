REBOL []

if not value? 'script-base [script-base: http://codeconscious.com/rebol-scripts/]
if not value? 'script-environment? [do script-base/script-environment.r]

do %requirements.reb

; -----------------------------------------------
; Should return with: [tests passed]
;
;	If not, shows which test failed.
;
; -----------------------------------------------

requirements 'tests [

	[{abnf}

		found? find do %test-abnf-parser.reb 'passed
	]

	[{rowsets}

		found? find do %test-rowsets.reb 'passed
	]
]

