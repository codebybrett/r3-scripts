[
    rulelist: [some [rule | [any c-wsp c-nl]]]
    rule: [rulename defined-as elements c-nl]
    rulename: [ALPHA any [ALPHA | DIGIT | "-"]]
    defined-as: [any c-wsp ["=" | "=/"] any c-wsp]
    elements: [alternation any c-wsp]
    c-wsp: [WSP | [c-nl WSP]]
    c-nl: [comment | CRLF]
    comment: [";" any [WSP | VCHAR] CRLF]
    alternation: [concatenation any [any c-wsp "/" any c-wsp concatenation]]
    concatenation: [repetition any [some c-wsp repetition]]
    repetition: [opt [repeat] element]
    repeat: [some DIGIT | [any DIGIT "*" any DIGIT]]
    element: [rulename | group | option | char-val | num-val | prose-val]
    group: ["(" any c-wsp alternation any c-wsp ")"]
    option: ["[" any c-wsp alternation any c-wsp "]"]
    char-val: [DQUOTE any [charset.x20-21_x23-7E] DQUOTE]
    num-val: ["%" [bin-val | dec-val | hex-val]]
    bin-val: [(nocase "b") some BIT opt [some ["." some BIT] | ["-" some BIT]]]
    dec-val: [(nocase "d") some DIGIT opt [some ["." some DIGIT] | ["-" some DIGIT]]]
    hex-val: [(nocase "x") some HEXDIG opt [some ["." some HEXDIG] | ["-" some HEXDIG]]]
    prose-val: ["<" any [charset.x20-3D_x3F-7E] ">"]
    ALPHA: (charset [#"A" - #"Z" #"a" - #"z"])
    BIT: ["0" | "1"]
    CHAR: (charset [#"^A" - #""])
    CR: #"^M"
    CRLF: [CR LF]
    CTL: [charset.x00-1F | #""]
    DIGIT: (charset [#"0" - #"9"])
    DQUOTE: #"^""
    HEXDIG: [DIGIT | (nocase "A") | (nocase "B") | (nocase "C") | (nocase "D") | (nocase "E") | (nocase "F")]
    HTAB: #"^-"
    LF: #"^/"
    LWSP: [any [WSP | CRLF WSP]]
    OCTET: (charset [#"^@" - #"ÿ"])
    SP: #" "
    VCHAR: (charset [#"!" - #"~"])
    WSP: [SP | HTAB]
    charset.x20-21_x23-7E: (charset [#" " - #"!" #"#" - #"~"])
    charset.x20-3D_x3F-7E: (charset [#" " - #"=" #"?" - #"~"])
    charset.x00-1F: (charset [#"^@" - #"^_"])
]