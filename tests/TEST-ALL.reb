REBOL []

if not value? 'script-base [script-base: http://codeconscious.com/rebol-scripts/]
if not value? 'script-environment? [do script-base/script-environment.r]

do %requirements.reb

requirements 'tests [

	[[test-abnf-parser Passed] = do %test-abnf-parser.reb]
]

