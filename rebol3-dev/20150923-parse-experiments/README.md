Parse experiments
=========================

Messy mostly broken experiments with parsing, tokenisation and C source.

Work in progress. Will be moved.

c-lexicals.complete.reb

* Complete, but unchecked, translation from section A.1 of the C language specification N1570.
* Preprocessing tokens are useful.
* I'm unsure at this point whether having "tokens" is useful.

c-structure.reb

* First draft translation (unchecked) of phrase structure grammar from Section A.2 of the C language specification N1570.
* My initial interest here is "function-definition" but at this stage I'm uncertain how to get to the point that it works.

parse-kit.reb

* This is an experimentally modified version of [parse-kit.reb](https://github.com/codebybrett/reb).

c-pp-tokeniser.reb

* A C tokenising function that returns the next preprocessing token.

test-lexing.reb

* Tokenises a C source file.
* Do [ENV.reb](https://github.com/codebybrett/reb) before trying it

token-kit.reb, token-kit.test.reb

* Draft scripts for tokenising text. Works.
* An example of parse rule rewriting.

token-kit.1.wsp-working.reb

* An earlier experiment.
