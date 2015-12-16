REBOL []

script-needs [
	%rewrite.r
]

do %c-pp-tokeniser.reb
do %token-kit.reb
;;do %token-kit-linear.reb


text: read %../GitHub/ren-c/src/core/n-system.c


r: tokenise get in c-pp-tokeniser 'token text
parse r/1 [some [into [x: (new-line/all x false) 'eol skip] x: (new-line x true) | skip]]

pattern: token-matching [wsp] [['wsp string!]]

pm delta-time [rewrite r [pattern [indent]]]

HALT
