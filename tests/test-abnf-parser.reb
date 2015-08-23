REBOL []


do http://codeconscious.com/rebol-scripts/abnf-parser.r

parse-abnf-rfc: funct [] [

	rfc: read/string %rfc/rfc5234-ABNF.txt
	rfc: copy find rfc {^/4.  ABNF Definition of ABNF^/}
	rfc: rfc-without-page-breaks rfc newline

	abnf: rejoin delimit extract-abnf rfc newline

	tree: build-abnf-ast abnf

	abnf-ast-to-rebol tree
]

requirements 'test-abnf-parser [

	[{Parses ABNF RFC.}
		equal? parse-abnf-rfc (load %abnf/rules.abnf.reb)
	]

]


