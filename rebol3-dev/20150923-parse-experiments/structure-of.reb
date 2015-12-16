REBOL []

structure-of: func [
    "Return word structure of object."
    object [object!] /local
    words
    result pos obj
][
    words: words-of object
    result: make block! to integer! 1.5 * length words
    foreach word words [
        insert tail pos: tail result word
        new-line pos true
        if object? obj: get word [
            insert/only tail pos structure-of obj
        ]
    ]
    result
]