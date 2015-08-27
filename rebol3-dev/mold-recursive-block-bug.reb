REBOL [
	purpose: {Demonstate bug in MOLD with recursive blocks.}
]

add-parents: func [
    {Modify structure [value properties child1 child2 ...] to restore parents to a tree.}
    block [block!]
    /parent node [none! block!] "Specify parent node." /local
    reference
][
    insert/only at block 2 node
    reference: at block 4
    forall reference [
        add-parents/parent reference/1 reference
    ]
    block
]

tree: [root [type root]
    [node1 [content "node1-content"]] 
    [node2 [content "node2-content"]] 
]

simple-tree-node: equal? mold tree {[root [type root]
    [node1 [content "node1-content"]]
    [node2 [content "node2-content"]]
]} ; Correct.

add-parents tree

recursive-tree-root: equal? mold tree {[root none [type root]
    [node1 [...] [content "node1-content"]]
    [node2 [...] [content "node2-content"]]
]} ; Correct.

recursive-tree-node: equal? mold tree/4 {[node1 [...] [content "node1-content"]]}
; Unexpected/Corrupt.

?? simple-tree-node
?? recursive-tree-root
?? recursive-tree-node
