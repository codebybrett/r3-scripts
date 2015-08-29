/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  c-word.c
**  Summary: symbol table and word related functions
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**    Word table is a block composed of symbols, each of which contain
**    a canon word number, alias word number (if it exists), and an
**    index that refers to the string for the text itself.
**
**    The canon number for a word is unique and is used to compare
**    words. The word table is independent of context frames and
**    words are never garbage collected.
**
**    The alias is used mainly for upper and lower case equality,
**    but can also be used to create ALIASes.
**
**    The word strings are stored as a single large string series.
**    NEVER CACHE A WORD NAME POINTER if new words may be added (e.g.
**    LOAD), because the series may get moved in memory.
**
***********************************************************************/

#include "sys-core.h"
#include <stdio.h>

#define WORD_TABLE_SIZE 1024  // initial size in words


/***********************************************************************
**
*/	static REBCNT const Primes[] =
/*
**		Prime numbers used for hash table sizes. Divide by 2 for
**		number of words that can be held in the symbol table.
**
***********************************************************************/
{
	251,
	509,
	1021,
	2039,
	4093,
	8191,
	16381,
	32749,
	65521,
	131071,
	262139,
	524287,
	1048573,
	2097143,
	4194301,
	8388593,
	16777213,
	33554393,
	67108859,
	134217689,
	268435399,
	0
};


/*******************************************************************************
**
**  Name: "Get_Hash_Prime"
**  Summary: none
**  Details: {
**      Given a size, return a prime number that is larger.}
**  Spec: none
**
*******************************************************************************/

REBINT Get_Hash_Prime(REBCNT size)
{
	REBINT n;

	for (n = 0; Primes[n] && size > Primes[n]; n++);

	if (!Primes[n]) return 0;

	return Primes[n];
}


/*******************************************************************************
**
**  Name: "Expand_Hash"
**  Summary: none
**  Details: {
**      Expand hash series. Clear it but set its tail.}
**  Spec: none
**
*******************************************************************************/

void Expand_Hash(REBSER *ser)
{
	REBINT pnum = Get_Hash_Prime(ser->tail + 1);
	if (!pnum) Trap_Num(RE_SIZE_LIMIT, ser->tail+1);

	assert(!Is_Array_Series(ser));
	Remake_Series(ser, pnum + 1, SERIES_WIDE(ser), MKS_POWER_OF_2);

	Clear_Series(ser);
	ser->tail = pnum;
}


/*******************************************************************************
**
**  Name: "Expand_Word_Table"
**  Summary: none
**  Details: {
**      Expand the hash table part of the word_table by allocating
**      the next larger table size and rehashing all the words of
**      the current table.  Free the old hash array.}
**  Spec: none
**
*******************************************************************************/

static void Expand_Word_Table(void)
{
	REBCNT *hashes;
	REBVAL *word;
	REBINT hash;
	REBCNT size;
	REBINT skip;
	REBCNT n;

	// Allocate a new hash table:
	Expand_Hash(PG_Word_Table.hashes);
	// Debug_Fmt("WORD-TABLE: expanded (%d symbols, %d slots)", PG_Word_Table.series->tail, PG_Word_Table.hashes->tail);

	// Rehash all the symbols:
	word = BLK_SKIP(PG_Word_Table.series, 1);
	hashes = (REBCNT *)PG_Word_Table.hashes->data;
	size = PG_Word_Table.hashes->tail;
	for (n = 1; n < PG_Word_Table.series->tail; n++, word++) {
		const REBYTE *name = VAL_SYM_NAME(word);
		hash = Hash_Word(name, LEN_BYTES(name));
		skip  = (hash & 0x0000FFFF) % size;
		if (skip == 0) skip = 1;
		hash = (hash & 0x00FFFF00) % size;
		while (hashes[hash]) {
			hash += skip;
			if (hash >= (REBINT)size) hash -= size;
		}
		hashes[hash] = n;
	}
}


/*******************************************************************************
**
**  Name: "Make_Word_Name"
**  Summary: none
**  Details: {
**      Allocates and copies the text string of the word.}
**  Spec: none
**
*******************************************************************************/

static REBCNT Make_Word_Name(const REBYTE *str, REBCNT len)
{
	REBCNT pos = SERIES_TAIL(PG_Word_Names);

	Append_Mem_Extra(PG_Word_Names, str, len, 1); // so we can do next line...
	PG_Word_Names->tail++; // keep terminator for each string
	return pos;
}


/*******************************************************************************
**
**  Name: "Make_Word"
**  Summary: none
**  Details: {
**      Given a string and its length, compute its hash value,
**      search for a match, and if not found, add it to the table.
**      Return the table index for the word (whether found or new).}
**  Spec: none
**
*******************************************************************************/

REBCNT Make_Word(const REBYTE *str, REBCNT len)
{
	REBINT	hash;
	REBINT	size;
	REBINT	skip;
	REBINT	n;
	REBCNT	h;
	REBCNT	*hashes;
	REBVAL  *words;
	REBVAL  *w;

	//REBYTE *sss = Get_Sym_Name(1);	// (Debugging method)

	// Prior convention was to assume zero termination if length was zero,
	// but that creates problems.  Caller should use LEN_BYTES for that.

	assert(len != 0);

	// !!! ...but should the zero length word be a valid word?

	// If hash part of word table is too dense, expand it:
	if (PG_Word_Table.series->tail > PG_Word_Table.hashes->tail/2)
		Expand_Word_Table();

	assert(SERIES_TAIL(PG_Word_Table.series) == SERIES_TAIL(Bind_Table));

	// If word symbol part of word table is full, expand it:
	if (SERIES_FULL(PG_Word_Table.series)) {
		Extend_Series(PG_Word_Table.series, 256);
	}
	if (SERIES_FULL(Bind_Table)) {
		Extend_Series(Bind_Table, 256);
		CLEAR_SERIES(Bind_Table);
	}

	size   = (REBINT)PG_Word_Table.hashes->tail;
	words  = BLK_HEAD(PG_Word_Table.series);
	hashes = (REBCNT *)PG_Word_Table.hashes->data;

	// Hash the word, including a skip factor for lookup:
	hash  = Hash_Word(str, len);
	skip  = (hash & 0x0000FFFF) % size;
	if (skip == 0) skip = 1;
	hash = (hash & 0x00FFFF00) % size;
	//Debug_Fmt("%s hash %d skip %d", str, hash, skip);

	// Search hash table for word match:
	while ((h = hashes[hash])) {
		while ((n = Compare_UTF8(VAL_SYM_NAME(words+h), str, len)) >= 0) {
			//if (Match_String("script", str, len))
			//	Debug_Fmt("---- %s %d %d\n", VAL_SYM_NAME(&words[h]), n, h);
			if (n == 0) return h; // direct hit
			if (VAL_SYM_ALIAS(words+h)) h = VAL_SYM_ALIAS(words+h);
			else goto make_sym; // Create new alias for word
		}
		hash += skip;
		if (hash >= size) hash -= size;
	}

make_sym:
	n = PG_Word_Table.series->tail;
	w = words + n;
	if (h) {
		// Alias word (h = canon word)
		VAL_SYM_ALIAS(words+h) = n;
		VAL_SYM_CANON(w) = VAL_SYM_CANON(words+h);
	} else {
		// Canon (base version of) word (h == 0)
		hashes[hash] = n;
		VAL_SYM_CANON(w) = n;
	}
	VAL_SYM_ALIAS(w) = 0;
	VAL_SYM_NINDEX(w) = Make_Word_Name(str, len);
	VAL_SET(w, REB_HANDLE);

	// These are allowed because of the SERIES_FULL checks above which
	// add one extra to the TAIL check comparision. However, their
	// termination values (nulls) will be missing.
	PG_Word_Table.series->tail++;
	Bind_Table->tail++;

	return n;
}


/*******************************************************************************
**
**  Name: "Last_Word_Num"
**  Summary: none
**  Details: {
**      Return the number of the last word created.  Used to
**      mark a range of canon-words (e.g. operators).}
**  Spec: none
**
*******************************************************************************/

REBCNT Last_Word_Num(void)
{
	return PG_Word_Table.series->tail - 1;
}


/*******************************************************************************
**
**  Name: "Val_Init_Word"
**  Summary: none
**  Details: {
**      Initialize an ANY-WORD! type with a binding to a context.}
**  Spec: none
**
*******************************************************************************/

void Val_Init_Word(REBVAL *value, REBCNT type, REBINT sym, REBSER *frame, REBCNT index)
{
	VAL_SET(value, type);
	VAL_WORD_SYM(value) = sym;
	assert(frame);
	VAL_WORD_FRAME(value) = frame;
	VAL_WORD_INDEX(value) = index;
	assert(ANY_WORD(value));
}


/*******************************************************************************
**
**  Name: "Val_Init_Word_Unbound"
**  Summary: none
**  Details: {
**      Initialize a value as a word. Set frame as unbound (no context).}
**  Spec: none
**
*******************************************************************************/

void Val_Init_Word_Unbound(REBVAL *value, REBCNT type, REBCNT sym)
{
	VAL_SET(value, type);
	VAL_WORD_FRAME(value) = NULL;
	VAL_WORD_SYM(value) = sym;
#ifndef NDEBUG
	VAL_WORD_INDEX(value) = WORD_INDEX_UNBOUND;
#endif
	assert(ANY_WORD(value));
}


/*******************************************************************************
**
**  Name: "Val_Init_Word_Typed"
**  Summary: none
**  Details: {
**      When a special flag is set on a REB_WORD--or a value of
**      ANY-WORD! type--it becomes an internal value holding a
**      64-bit typeset (rather than a pointer and a binding index).
**  
**      These 'typed' words are found in the identifying function
**      argument series or the words of an object.  For functions,
**      typeset bits hold the legal Rebol types those elements can
**      hold.  They are currently unused in objects.
**  
**      NOTE: These should not be leaked out to the user as ordinary
**      words.  When a user reflects the `words-of` list, any series
**      with typed words in them must be copied and mutated back to
**      ordinary words.}
**  Spec: none
**
*******************************************************************************/

void Val_Init_Word_Typed(REBVAL *value, REBCNT type, REBCNT sym, REBU64 typeset)
{
	VAL_SET(value, type);
	VAL_SET_EXT(value, EXT_WORD_TYPED);
	VAL_BIND_SYM(value) = sym;
	VAL_BIND_TYPESET(value) = typeset;
	assert(ANY_WORD(value));
}


/***********************************************************************
**
*/	const REBYTE *Get_Sym_Name(REBCNT num)
/*
***********************************************************************/
{
	if (num == 0 || num >= PG_Word_Table.series->tail) return cb_cast("???");
	return VAL_SYM_NAME(BLK_SKIP(PG_Word_Table.series, num));
}


/***********************************************************************
**
*/	const REBYTE *Get_Word_Name(const REBVAL *value)
/*
***********************************************************************/
{
	if (value) return Get_Sym_Name(VAL_WORD_SYM(value));
	return cb_cast("(unnamed)");
}


/***********************************************************************
**
*/	const REBYTE *Get_Type_Name(const REBVAL *value)
/*
***********************************************************************/
{
	return Get_Sym_Name(VAL_TYPE(value)+1);
}


/*******************************************************************************
**
**  Name: "Compare_Word"
**  Summary: none
**  Details: {
**      Compare the names of two words and return the difference.
**      Note that words are kept UTF8 encoded.
**      Positive result if s > t and negative if s < t.}
**  Spec: none
**
*******************************************************************************/

REBINT Compare_Word(const REBVAL *s, const REBVAL *t, REBFLG is_case)
{
	REBYTE *sp = VAL_WORD_NAME(s);
	REBYTE *tp = VAL_WORD_NAME(t);

	// Use a more strict comparison than normal:
	if (is_case) return COMPARE_BYTES(sp, tp);

	// They are the equivalent words:
	if (VAL_WORD_CANON(s) == VAL_WORD_CANON(t)) return 0;

	// They must be differ by case:
	return Compare_UTF8(sp, tp, LEN_BYTES(tp)) + 2;
}


/*******************************************************************************
**
**  Name: "Init_Words"
**  Summary: none
**  Details: {
**      Only flags BIND_Table creation only (for threads).}
**  Spec: none
**
*******************************************************************************/

void Init_Words(REBFLG only)
{
	REBCNT n = Get_Hash_Prime(WORD_TABLE_SIZE * 4); // extra to reduce rehashing

	if (!only) {
		// Create the hash for locating words quickly:
		// Note that the TAIL is never changed for this series.
		PG_Word_Table.hashes = Make_Series(n + 1, sizeof(REBCNT), MKS_NONE);
		KEEP_SERIES(PG_Word_Table.hashes, "word hashes"); // pointer array
		Clear_Series(PG_Word_Table.hashes);
		PG_Word_Table.hashes->tail = n;

		// The word (symbol) table itself:
		PG_Word_Table.series = Make_Array(WORD_TABLE_SIZE);
		SET_NONE(BLK_HEAD(PG_Word_Table.series)); // Put a NONE at head.
		KEEP_SERIES(PG_Word_Table.series, "word table"); // words are never GC'd
		PG_Word_Table.series->tail = 1;  // prevent the zero case

		// A normal char array to hold symbol names:
		PG_Word_Names = Make_Binary(6 * WORD_TABLE_SIZE); // average word size
		KEEP_SERIES(PG_Word_Names, "word names");
	}

	// The bind table. Used to cache context indexes for given symbols.
	Bind_Table = Make_Series(
		SERIES_REST(PG_Word_Table.series), sizeof(REBCNT), MKS_NONE
	);
	KEEP_SERIES(Bind_Table, "bind table"); // numeric table
	CLEAR_SERIES(Bind_Table);
	Bind_Table->tail = PG_Word_Table.series->tail;
}
