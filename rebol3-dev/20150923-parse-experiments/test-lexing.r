REBOL []

script-needs [
	%rewrite.r
]

do %c-pp-tokeniser.reb
do %token-kit.reb

text: read %../../../ren-c/src/core/n-system.c

r: tokenise get in c-pp-tokeniser 'token text

pattern: token-matching [eol wsp] [eol wsp]
rewrite r [[x: pattern] [[(x/1)] indent]]

parse r token-matching [eol] [some [eol x: (new-line x true) | skip]]


HALT
