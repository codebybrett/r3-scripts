REBOL []

if not value? 'script-base [script-base: http://codeconscious.com/rebol-scripts/]
if not value? 'script-environment? [do script-base/script-environment.r]

do %requirements.reb

requirements 'tests [

	[found? find do %test-abnf-parser.reb 'passed]

	[found? find do %test-rowsets.reb 'passed]
]

