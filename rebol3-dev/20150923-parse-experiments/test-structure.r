REBOL []


do %../GitHub/reb/env.reb

script-needs [
	%parse-kit.reb
]

use-script %parse-analysis-view.r

do %../GitHub/grammars/c/c-lexicals.reb
do %c-structure.reb

text: read/string %../GitHub/temporary.201508-source-format-change/src/core/n-system.c

vp: does bind [
	visualise-parse text grammar [parse/all/case text grammar/translation-unit]
] c.structure


HALT