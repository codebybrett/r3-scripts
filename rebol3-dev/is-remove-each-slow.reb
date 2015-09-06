REBOL [
	purpose: {Compare remove-each and map-each for filtering.}
]

timeit: funct [block][recycle start: now/precise do block difference now/precise start]

counts: [100000 1000000 10000000]

tests: [
	[remove-each x array count [true]]
	[remove-each x copy array count [true]]
	[map-each x array count [()]]
	[map-each x array count [continue]]
]

results: map-each test tests [map-each count counts [timeit bind test 'count]]

?? tests
print mold new-line/all results true
