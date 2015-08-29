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
**  Module:  m-pools.c
**  Summary: memory allocation pool management
**  Section: memory
**  Author:  Carl Sassenrath
**  Notes:
**		A point of Rebol's design was to remain small and solve its
**		problems without relying on a lot of abstraction.  Its
**		memory-management was thus focused on staying low-level...and
**		being able to do efficient and lightweight allocations of
**		two major elements: series and graphic objects (GOBs).
**
**		Both series and GOBs have a fixed-size component that can
**		be easily allocated from a memory pool.  This portion is
**		called the "Node" (or NOD) in both Rebol and Red terminology;
**		it is an item whose pointer is valid for the lifetime of
**		the object, regardless of resizing.  This is where header
**		information is stored, and pointers to these objects may
**		be saved in REBVAL values; such that they are kept alive
**		by the garbage collector.
**
**		The more complicated thing to do memory pooling of is the
**		variable-sized portion of a series (currently called the
**		"series data")...as series sizes can vary widely.  But a
**		trick Rebol has is that a series might be able to take
**		advantage of being given back an allocation larger than
**		requested.  They can use it as reserved space for growth.
**
**		(Typical models for implementation of things like C++'s
**		std::vector do not reach below new[] or delete[]...which
**		are generally implemented with malloc and free under
**		the hood.  Their buffered additional capacity is done
**		assuming the allocation they get is as big as they asked
**		for...no more and no less.)
**
**		While Rebol's memory pooling is a likely-useful tool even
**		with modern alternatives, there are also useful tools
**		like Valgrind and Address Sanitizer which can more easily
**		root out bugs if each allocation and free is done
**		separately through malloc and free.  Therefore there is
**		an option for always using malloc, which you can enable
**		by setting the environment variable R3_ALWAYS_MALLOC to 1.
**
***********************************************************************/

//-- Special Debugging Options:
//#define CHAFF					// Fill series data to crash old references
//#define HIT_END				// Panic_DEAD_END if block tail is past block terminator.
//#define WATCH_FREED			// Show # series freed each GC
//#define MEM_STRESS			// Special torture mode enabled
//#define INSPECT_SERIES

#include "sys-core.h"
#include "sys-int-funcs.h"

#ifdef HAVE_ASAN_INTERFACE_H
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(reg, mem_size)
#define ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)
#endif

/***********************************************************************
**
*/	void *Alloc_Mem(size_t size)
/*
**		NOTE: Instead of Alloc_Mem, use the ALLOC and ALLOC_ARRAY
**		wrapper macros to ensure the memory block being freed matches
**		the appropriate size for the type.
**
*************************************************************************
**
**		Alloc_Mem is an interface for a basic memory allocator.
**		It is coupled with a Free_Mem function that clients must
**		call with the correct size of the memory block to be freed.
**		It is thus lower-level than malloc()... whose memory blocks
**		remember the size of the allocation so you don't need to
**		pass it into free().
**
**		One motivation behind using such an allocator in Rebol
**		is to allow it to keep knowledge of how much memory the
**		system is using.  This means it can decide when to trigger a
**		garbage collection, or raise an out-of-memory error before
**		the operating system would, e.g. via 'ulimit':
**
**			http://stackoverflow.com/questions/1229241/
**
**		Finer-grained allocations are done with memory pooling.  But
**		the blocks of memory used by the pools are still acquired
**		using ALLOC_ARRAY and FREE_ARRAY.
**
***********************************************************************/
{
	// Trap memory usage limit *before* the allocation is performed

	PG_Mem_Usage += size;
	if ((PG_Mem_Limit != 0) && (PG_Mem_Usage > PG_Mem_Limit))
		Check_Security(SYM_MEMORY, POL_EXEC, 0);

	// While conceptually a simpler interface than malloc(), the
	// current implementations on all C platforms just pass through to
	// malloc and free.  NOTE: use of calloc is temporary for the
	// pooling commit, as it covers up bugs.  Those are addressed in
	// a separate patch.

#ifdef NDEBUG
	return calloc(size, 1);
#else
	{
		// In debug builds we cache the size at the head of the
		// allocation so we can check it.

		void *ptr = calloc(size + sizeof(size_t), 1);
		*cast(size_t *, ptr) = size;
		return cast(char *, ptr) + sizeof(size_t);
	}
#endif
}


/*******************************************************************************
**
**  Name: "Free_Mem"
**  Summary: none
**  Details: {
**      NOTE: Instead of Free_Mem, use the FREE and FREE_ARRAY
**      wrapper macros to ensure the memory block being freed matches
**      the appropriate size for the type.}
**  Spec: none
**
*******************************************************************************/

void Free_Mem(void *mem, size_t size)
{
#ifdef NDEBUG
	free(mem);
#else
	{
		// In debug builds we will not only be able to assert the
		// correct size...but if someone tries to use a normal free()
		// and bypass Free_Mem it will trigger debug alerts from the
		// C runtime of trying to free a non-head-of-malloc.  This
		// helps in ensuring we get a balanced PG_Mem_Usage of 0 at the
		// end of the program.  We also know the host allocator uses
		// a similar trick, but since it doesn't need to remember the
		// size it puts a known garbage value for us to check for.

		char *ptr = cast(char *, mem) - sizeof(size_t);
		if (*cast(size_t *, ptr) == cast(size_t, -1020)) {
			Debug_Fmt("** Free_Mem() likely used on OS_Alloc_Mem() memory!");
			Debug_Fmt("** You should use OS_FREE() instead of FREE().");
			assert(FALSE);
		}
		assert(*cast(size_t *, ptr) == size);
		free(ptr);
	}
#endif
	PG_Mem_Usage -= size;
}


#define POOL_MAP

#define	BAD_MEM_PTR ((REBYTE *)0xBAD1BAD1)

//#define GC_TRIGGER (GC_Active && (GC_Ballast <= 0 || (GC_Pending && !GC_Disabled)))

#ifdef POOL_MAP
	#ifdef NDEBUG
		#define FIND_POOL(n) \
			((n <= 4 * MEM_BIG_SIZE) \
				? cast(REBCNT, PG_Pool_Map[n]) \
				: cast(REBCNT, SYSTEM_POOL))
	#else
		#define FIND_POOL(n) \
			((!PG_Always_Malloc && (n <= 4 * MEM_BIG_SIZE)) \
				? cast(REBCNT, PG_Pool_Map[n]) \
				: cast(REBCNT, SYSTEM_POOL))
	#endif
#else
	#ifdef NDEBUG
		#define FIND_POOL(n) Find_Pool(n)
	#else
		#define FIND_POOL(n) (PG_Always_Malloc ? SYSTEM_POOL : Find_Pool(n))
	#endif
#endif

/***********************************************************************
**
**	MEMORY POOLS
**
**		Memory management operates off an array of pools, the first
**		group of which are fixed size (so require no compaction).
**
***********************************************************************/
const REBPOOLSPEC Mem_Pool_Spec[MAX_POOLS] =
{
	{8, 256},			// 0-8 Small string pool

	MOD_POOL( 1, 256),	// 9-16 (when REBVAL is 16)
	MOD_POOL( 2, 512),	// 17-32 - Small series (x 16)
	MOD_POOL( 3, 1024),	// 33-64
	MOD_POOL( 4, 512),
	MOD_POOL( 5, 256),
	MOD_POOL( 6, 128),
	MOD_POOL( 7, 128),
	MOD_POOL( 8,  64),
	MOD_POOL( 9,  64),
	MOD_POOL(10,  64),
	MOD_POOL(11,  32),
	MOD_POOL(12,  32),
	MOD_POOL(13,  32),
	MOD_POOL(14,  32),
	MOD_POOL(15,  32),
	MOD_POOL(16,  64),	// 257
	MOD_POOL(20,  32),	// 321 - Mid-size series (x 64)
	MOD_POOL(24,  16),	// 385
	MOD_POOL(28,  16),	// 449
	MOD_POOL(32,   8),	// 513

	DEF_POOL(MEM_BIG_SIZE,  16),	// 1K - Large series (x 1024)
	DEF_POOL(MEM_BIG_SIZE*2, 8),	// 2K
	DEF_POOL(MEM_BIG_SIZE*3, 4),	// 3K
	DEF_POOL(MEM_BIG_SIZE*4, 4),	// 4K

	DEF_POOL(sizeof(REBSER), 4096),	// Series headers
	DEF_POOL(sizeof(REBGOB), 128),	// Gobs
	DEF_POOL(sizeof(REBLHL), 32), // external libraries
	DEF_POOL(sizeof(REBRIN), 128), // external routines
	DEF_POOL(1, 1),	// Just used for tracking main memory
};


/*******************************************************************************
**
**  Name: "Init_Pools"
**  Summary: none
**  Details: "^/        Initialize memory pool array."
**  Spec: none
**
*******************************************************************************/

void Init_Pools(REBINT scale)
{
	REBCNT n;
	REBINT unscale = 1;

#ifndef NDEBUG
	const char *env_always_malloc = NULL;
	env_always_malloc = getenv("R3_ALWAYS_MALLOC");
	if (env_always_malloc != NULL && atoi(env_always_malloc) != 0) {
		Debug_Str(
			"**\n"
			"** R3_ALWAYS_MALLOC is TRUE in environment variable!\n"
			"** Memory allocations aren't pooled, expect slowness...\n"
			"**\n"
		);
		PG_Always_Malloc = TRUE;
	}
#endif

	if (scale == 0) scale = 1;
	else if (scale < 0) unscale = -scale, scale = 1;

	// Copy pool sizes to new pool structure:
	Mem_Pools = ALLOC_ARRAY(REBPOL, MAX_POOLS);
	for (n = 0; n < MAX_POOLS; n++) {
		Mem_Pools[n].wide = Mem_Pool_Spec[n].wide;
		Mem_Pools[n].units = (Mem_Pool_Spec[n].units * scale) / unscale;
		if (Mem_Pools[n].units < 2) Mem_Pools[n].units = 2;
	}

	// For pool lookup. Maps size to pool index. (See Find_Pool below)
	PG_Pool_Map = ALLOC_ARRAY(REBYTE, (4 * MEM_BIG_SIZE) + 4); // extra
	n = 9;  // sizes 0 - 8 are pool 0
	for (; n <= 16 * MEM_MIN_SIZE; n++) PG_Pool_Map[n] = MEM_TINY_POOL     + ((n-1) / MEM_MIN_SIZE);
	for (; n <= 32 * MEM_MIN_SIZE; n++) PG_Pool_Map[n] = MEM_SMALL_POOLS-4 + ((n-1) / (MEM_MIN_SIZE * 4));
	for (; n <=  4 * MEM_BIG_SIZE; n++) PG_Pool_Map[n] = MEM_MID_POOLS     + ((n-1) / MEM_BIG_SIZE);
}


#ifndef POOL_MAP
/*******************************************************************************
**
**  Name: "Find_Pool"
**  Summary: none
**  Details: {
**      Given a size, tell us what pool it belongs to.}
**  Spec: none
**
*******************************************************************************/

static REBCNT Find_Pool(REBCNT size)
{
	if (size <= 8) return 0;  // Note: 0 - 8 (and size change for proper modulus)
	size--;
	if (size < 16 * MEM_MIN_SIZE) return MEM_TINY_POOL   + (size / MEM_MIN_SIZE);
	if (size < 32 * MEM_MIN_SIZE) return MEM_SMALL_POOLS-4 + (size / (MEM_MIN_SIZE * 4));
	if (size <  4 * MEM_BIG_SIZE) return MEM_MID_POOLS   + (size / MEM_BIG_SIZE);
	return SYSTEM_POOL;
}


/***********************************************************************
**
*/	void Check_Pool_Map(void)
**
************************************************************************
{
	int n;

	for (n = 0; n <= 4 * MEM_BIG_SIZE + 1; n++)
		if (FIND_POOL(n) != Find_Pool(n))
			Debug_Fmt("%d: %d %d", n, FIND_POOL(n), Find_Pool(n));
}
*/
#endif


/*******************************************************************************
**
**  Name: "Fill_Pool"
**  Summary: none
**  Details: {
**      Allocate memory for a pool.  The amount allocated will be
**      determined from the size and units specified when the
**      pool header was created.  The nodes of the pool are linked
**      to the free list.}
**  Spec: none
**
*******************************************************************************/

static void Fill_Pool(REBPOL *pool)
{
	REBSEG	*seg;
	REBNOD	*node;
	REBYTE	*next;
	REBCNT	units = pool->units;
	REBCNT	mem_size = pool->wide * units + sizeof(REBSEG);

	seg = cast(REBSEG *, ALLOC_ARRAY(char, mem_size));
	if (!seg) {
		assert(FALSE);
		Panic_Core(RP_NO_MEMORY, mem_size);
	}

	CLEAR(seg, mem_size);  // needed to clear series nodes
	seg->size = mem_size;
	seg->next = pool->segs;
   	pool->segs = seg;
	pool->free += units;
	pool->has += units;

	// Add new nodes to the end of free list:
	if (pool->last == NULL) {
		node = (REBNOD*)&pool->first;
	} else {
		node = pool->last;
		ASAN_UNPOISON_MEMORY_REGION(node, pool->wide);
	}

	for (next = (REBYTE *)(seg + 1); units > 0; units--, next += pool->wide) {
		*node = (REBNOD) next;
		node  = cast(void**, *node);
	}

	*node = 0;
	if (pool->last != NULL) {
		ASAN_POISON_MEMORY_REGION(pool->last, pool->wide);
	}
	pool->last = node;
	ASAN_POISON_MEMORY_REGION(seg, mem_size);
}


/***********************************************************************
**
*/	void *Make_Node(REBCNT pool_id)
/*
**		Allocate a node from a pool.  The node will NOT be cleared.
**		If the pool has run out of nodes, it will be refilled.
**
***********************************************************************/
{
	REBNOD *node;
	REBPOL *pool;

	pool = &Mem_Pools[pool_id];
	if (!pool->first) Fill_Pool(pool);
	node = pool->first;

	ASAN_UNPOISON_MEMORY_REGION(node, pool->wide);

	pool->first = cast(void**, *node);
	if (node == pool->last) {
		pool->last = NULL;
	}
	pool->free--;
	return (void *)node;
}


/*******************************************************************************
**
**  Name: "Free_Node"
**  Summary: none
**  Details: "^/        Free a node, returning it to its pool."
**  Spec: none
**
*******************************************************************************/

void Free_Node(REBCNT pool_id, REBNOD *node)
{
	REBPOL *pool = &Mem_Pools[pool_id];

	if (pool->last == NULL) { //pool is empty
		Fill_Pool(pool); //insert an empty segment, such that this node won't be picked by next Make_Node to enlongate the poisonous time of this area to catch stale pointers
	}
	ASAN_UNPOISON_MEMORY_REGION(pool->last, pool->wide);
	*(pool->last) = node;
	ASAN_POISON_MEMORY_REGION(pool->last, pool->wide);
	pool->last = node;
	*node = NULL;

	ASAN_POISON_MEMORY_REGION(node, pool->wide);

	pool->free++;
}


/*******************************************************************************
**
**  Name: "Series_Data_Alloc"
**  Summary: none
**  Details: {
**      Allocates element array for an already allocated REBSER header
**      structure.  Resets the bias and tail to zero, and sets the new
**      width.  Flags like SER_PROTECT or SER_KEEP are left as they
**      were, and other fields in the series structure are untouched.
**  
**      This routine can thus be used for an initial construction
**      or an operation like expansion.  Currently not exported
**      from this file.}
**  Spec: none
**
*******************************************************************************/

static REBOOL Series_Data_Alloc(REBSER *series, REBCNT length, REBYTE wide, REBCNT flags)
{
	REBCNT size; // size of allocation (possibly bigger than we need)

	REBCNT pool_num = FIND_POOL(length * wide);

	// Data should have not been allocated yet OR caller has extracted it
	// and nulled it to indicate taking responsibility for freeing it.
	assert(!series->data);

	if (pool_num < SYSTEM_POOL) {
		// ...there is a pool designated for allocations of this size range
		series->data = cast(REBYTE*, Make_Node(pool_num));
		if (!series->data)
			return FALSE;

		// The pooled allocation might wind up being larger than we asked.
		// Don't waste the space...mark as capacity the series could use.
		size = Mem_Pools[pool_num].wide;
		assert(size >= length * wide);

		// We don't round to power of 2 for allocations in memory pools
		SERIES_CLR_FLAG(series, SER_POWER_OF_2);
	}
	else {
		// ...the allocation is too big for a pool.  But instead of just
		// doing an unpooled allocation to give you the size you asked
		// for, the system does some second-guessing to align to 2Kb
		// boundaries (or choose a power of 2, if requested).

		size = length * wide;
		if (flags & MKS_POWER_OF_2) {
			REBCNT len = 2048;
			while(len < size)
				len *= 2;
			size = len;
			SERIES_SET_FLAG(series, SER_POWER_OF_2);
		}
		else {
			size = ALIGN(size, 2048);
			SERIES_CLR_FLAG(series, SER_POWER_OF_2);
		}

		series->data = ALLOC_ARRAY(REBYTE, size);
		if (!series->data)
			return FALSE;

		Mem_Pools[SYSTEM_POOL].has += size;
		Mem_Pools[SYSTEM_POOL].free++;
	}

#ifdef CHAFF
	// REVIEW: Rely completely on address sanitizer "poisoning" instead?
	memset(series->data, 0xff, size);
#endif

	// Keep the series flags like SER_KEEP, but use new width and set bias 0.

	series->info = ((series->info >> 8) << 8) | wide;
	SERIES_SET_BIAS(series, 0);

	if (flags & MKS_ARRAY) {
		assert(wide == sizeof(REBVAL));
		SERIES_SET_FLAG(series, SER_ARRAY);
		assert(Is_Array_Series(series));
	}
	else {
		SERIES_CLR_FLAG(series, SER_ARRAY);
		assert(!Is_Array_Series(series));
	}

	// The allocation may have returned more than we requested, so we note
	// that in 'rest' so that the series can expand in and use the space.

	series->rest = size / wide; // wastes remainder if size % wide != 0 :-(

	// We set the tail of all series to zero initially, but currently do
	// leave series termination to callers.  (This is under review.)

	series->tail = 0;

	// See if allocation tripped our need to queue a garbage collection

	if ((GC_Ballast -= size) <= 0) SET_SIGNAL(SIG_RECYCLE);

	assert(Series_Allocated_Size(series) == size);
	return TRUE;
}


/*******************************************************************************
**
**  Name: "Series_Allocated_Size"
**  Summary: none
**  Details: {
**      When we want the actual memory accounting for a series, the
**      whole story may not be told by the element size multiplied
**      by the capacity.  The series may have been allocated from
**      a pool where it was rounded up to the pool size, and the
**      elements may not fit evenly in that space.  Or it may have
**      been allocated from the "system pool" via Alloc_Mem, but
**      rounded up to a power of 2.
**  
**      (Note: It's necessary to know the size because Free_Mem
**      requires it, as Rebol's allocator doesn't remember the size
**      of system pool allocations for you.  It also needs it in
**      order to keep track of GC boundaries and memory use quotas.)
**  
**      Rather than pay for the cost on every series of an "actual
**      allocation size", the optimization choice is to only pay
**      for a "rounded up to power of 2" bit.  (Since there are a
**      LOT of series created in Rebol, each byte is scrutinized.)}
**  Spec: none
**
*******************************************************************************/

REBCNT Series_Allocated_Size(REBSER *series)
{
	REBCNT total = SERIES_TOTAL(series);
	REBCNT pool_num = FIND_POOL(total);

	if (pool_num < SERIES_POOL) {
		assert(!SERIES_GET_FLAG(series, SER_POWER_OF_2));
		assert(Mem_Pools[pool_num].wide >= total);
		return Mem_Pools[pool_num].wide;
	}

	if (SERIES_GET_FLAG(series, SER_POWER_OF_2)) {
		REBCNT len = 2048;
		while(len < total)
			len *= 2;
		return len;
	}

	return ALIGN(total, 2048);
}


/***********************************************************************
**
*/	REBSER *Make_Series(REBCNT length, REBYTE wide, REBCNT flags)
/*
**		Make a series of a given length and width (unit size).
**		Small series will be allocated from a REBOL pool.
**		Large series will be allocated from system memory.
**		A width of zero is not allowed.
**
***********************************************************************/
{
	REBSER *series;

	CHECK_C_STACK_OVERFLOW(&series);

	// PRESERVE flag only makes sense for Remake_Series, where there is
	// previous data to be kept.
	assert(!(flags & MKS_PRESERVE));
	assert(wide != 0 && length != 0);

	if ((cast(REBU64, length) * wide) > MAX_I32) Trap(RE_NO_MEMORY);

	PG_Reb_Stats->Series_Made++;
	PG_Reb_Stats->Series_Memory += length * wide;

//	if (GC_TRIGGER) Recycle();

	series = cast(REBSER*, Make_Node(SERIES_POOL));

	if ((GC_Ballast -= sizeof(REBSER)) <= 0) SET_SIGNAL(SIG_RECYCLE);

#ifndef NDEBUG
	// For debugging purposes, it's nice to be able to crash on some
	// kind of guard for tracking the call stack at the point of allocation
	// if we find some undesirable condition that we want a trace from
	series->guard = cast(REBINT*, malloc(sizeof(*series->guard)));
	free(series->guard);
#endif

	series->info = 0; // start with all flags clear...
	series->data = NULL;

	if (flags & MKS_LOCK) SERIES_SET_FLAG(series, SER_LOCK);

	if (flags & MKS_EXTERNAL) {
		// External series will poke in their own data pointer after the
		// REBSER header allocation is done

		SERIES_SET_FLAG(series, SER_EXTERNAL);
		series->info |= wide & 0xFF;
		series->rest = length;
	}
	else {
		// Allocate the actual data blob that holds the series elements

		if (!Series_Data_Alloc(series, length, wide, flags)) {
			Free_Node(SERIES_POOL, cast(REBNOD*, series));
			Trap(RE_NO_MEMORY);
		}
	}

	series->extra.size = 0;

#if !defined(NDEBUG)
	// All series start out in the debug tracking list of series that
	// must be freed (if not handed to Manage_Series for the GC to take
	// care of.)  We tack the new series on the head of the doubly-linked
	// track list.

	if (GC_Manuals) {
		series->next = GC_Manuals;
		GC_Manuals->prev = series;
	}
	else
		series->next = NULL;
	series->prev = NULL;
	GC_Manuals = series;
#endif

	CHECK_MEMORY(2);

	if (flags & MKS_ARRAY) PG_Reb_Stats->Blocks++;

	return series;
}


/*******************************************************************************
**
**  Name: "Free_Unbiased_Series_Data"
**  Summary: none
**  Details: {
**      Routines that are part of the core series implementation
**      call this, including Expand_Series.  It requires a low-level
**      awareness that the series data pointer cannot be freed
**      without subtracting out the "biasing" which skips the pointer
**      ahead to account for unused capacity at the head of the
**      allocation.  They also must know the total allocation size.}
**  Spec: none
**
*******************************************************************************/

static void Free_Unbiased_Series_Data(REBYTE *unbiased, REBCNT size)
{
	REBCNT pool_num = FIND_POOL(size);
	REBPOL *pool;

	if (GC_Stay_Dirty) {
		memset(unbiased, 0xbb, size);
		return;
	}

	// Verify that size matches pool size:
	if (pool_num < SERIES_POOL) {
		/* size < wide when "wide" is not a multiple of element size */
		assert(Mem_Pools[pool_num].wide >= size);
	}

	if (pool_num < SYSTEM_POOL) {
		REBNOD *node = cast(REBNOD*, unbiased);
		pool = &Mem_Pools[pool_num];
		*node = pool->first;
		pool->first = node;
		pool->free++;
	}
	else {
		FREE_ARRAY(REBYTE, size, unbiased);
		Mem_Pools[SYSTEM_POOL].has -= size;
		Mem_Pools[SYSTEM_POOL].free--;
	}

	CHECK_MEMORY(2);
}


/*******************************************************************************
**
**  Name: "Expand_Series"
**  Summary: none
**  Details: {
**      Expand a series at a particular index point by the number
**      number of units specified by delta.
**  
**          index - where space is expanded (but not cleared)
**          delta - number of UNITS to expand (keeping terminator)
**          tail  - will be updated
**  
**                  |<---rest--->|
**          <-bias->|<-tail->|   |
**          +--------------------+
**          |       abcdefghi    |
**          +--------------------+
**                  |    |
**                  data index
**  
**      If the series has enough space within it, then it will be used,
**      otherwise the series data will be reallocated.
**  
**      When expanded at the head, if bias space is available, it will
**      be used (if it provides enough space).
**  
**      !!! It seems the original intent of this routine was
**      to be used with a group of other routines that were "Noterm"
**      and do not terminate.  However, Expand_Series assumed that
**      the capacity of the original series was at least (tail + 1)
**      elements, and would include the terminator when "sliding"
**      the data in the update.  This makes the other Noterm routines
**      seem a bit high cost for their benefit.  If this were to be
**      changed to Expand_Series_Noterm it would put more burden
**      on the clients...for a *potential* benefit in being able to
**      write just a REB_END byte into the terminal REBVAL vs. copying
**      the entire value cell.  (Of course, with a good memcpy it
**      might be an irrelevant difference.)  For the moment we reverse
**      the burden by enforcing the assumption that the incoming series
**      was already terminated.  That way our "slide" of the data via
**      memcpy will keep it terminated.
**  
**      WARNING: never use direct pointers into the series data, as the
**      series data can be relocated in memory.}
**  Spec: none
**
*******************************************************************************/

void Expand_Series(REBSER *series, REBCNT index, REBCNT delta)
{
	REBYTE wide = SERIES_WIDE(series);
	REBOOL any_block = Is_Array_Series(series);

	REBCNT start;
	REBCNT size;
	REBCNT extra;
	REBUPT n_found;
	REBUPT n_available;
	REBCNT x;
	REBYTE *data_old;
	REBCNT size_old;
	REBINT bias_old;
	REBINT tail_old;

	// ASSERT_SERIES_TERM(series);

	if (delta == 0) return;

	any_block = Is_Array_Series(series);

	// Optimized case of head insertion:
	if (index == 0 && SERIES_BIAS(series) >= delta) {
		series->data -= wide * delta;
		SERIES_TAIL(series) += delta;
		SERIES_REST(series) += delta;
		SERIES_SUB_BIAS(series, delta);
		return;
	}

	// Range checks:
	if (delta & 0x80000000) Trap(RE_PAST_END); // 2GB max
	if (index > series->tail) index = series->tail; // clip

	// Width adjusted variables:
	start = index * wide;
	extra = delta * wide;
	size  = (series->tail + 1) * wide;

	if ((size + extra) <= SERIES_SPACE(series)) {
		// No expansion was needed. Slide data down if necessary.
		// Note that the tail is always moved here. This is probably faster
		// than doing the computation to determine if it needs to be done.

		memmove(series->data + start + extra, series->data + start, size - start);
		series->tail += delta;

		if ((SERIES_TAIL(series) + SERIES_BIAS(series)) * wide >= SERIES_TOTAL(series)) {
			Dump_Series(series, "Overflow");
			assert(FALSE);
			Panic(RP_MISC); // shouldn't be possible, but code here panic'd
		}

		return;
	}

	// We need to expand the current series allocation.

	if (SERIES_GET_FLAG(series, SER_LOCK)) Panic(RP_LOCKED_SERIES);

#ifndef NDEBUG
	if (Reb_Opts->watch_expand) {
		Debug_Fmt(
			"Expand %x wide: %d tail: %d delta: %d",
			series, wide, series->tail, delta
		);
	}
#endif

	// Create a new series that is bigger.
	// Have we recently expanded the same series?
	x = 1;
	n_available = 0;
	for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
		if (Prior_Expand[n_found] == series) {
			x = series->tail + delta + 1; // Double the size
			break;
		}
		if (!Prior_Expand[n_found])
			n_available = n_found;
	}

#ifndef NDEBUG
	if (Reb_Opts->watch_expand) {
		// Print_Num("Expand:", series->tail + delta + 1);
	}
#endif

	data_old = series->data;
	bias_old = SERIES_BIAS(series);
	size_old = Series_Allocated_Size(series);
	tail_old = SERIES_TAIL(series);

	series->data = NULL;
	if (!Series_Data_Alloc(
		series,
		series->tail + delta + x,
		wide,
		any_block ? (MKS_ARRAY | MKS_POWER_OF_2) : MKS_POWER_OF_2
	)) {
		Trap(RE_NO_MEMORY);
		DEAD_END_VOID;
	}

	assert(SERIES_BIAS(series) == 0); // should be reset

	// If necessary, add series to the recently expanded list:
	if (n_found >= MAX_EXPAND_LIST)
		Prior_Expand[n_available] = series;

	// Copy the series up to the expansion point:
	memcpy(series->data, data_old, start);

	// Copy the series after the expansion point:
	// In AT_TAIL cases, this just moves the terminator to the new tail.
	memcpy(series->data + start + extra, data_old + start, size - start);
	series->tail = tail_old + delta;

	// We have to de-bias the data pointer before we can free it.
	Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);

	PG_Reb_Stats->Series_Expanded++;
}


/*******************************************************************************
**
**  Name: "Remake_Series"
**  Summary: none
**  Details: {
**      Reallocate a series as a given maximum size. Content in the
**      retained portion of the length may be kept as-is if the
**      MKS_PRESERVE is passed in the flags.  The other flags are
**      handled the same as when passed to Make_Series.}
**  Spec: none
**
*******************************************************************************/

void Remake_Series(REBSER *series, REBCNT units, REBYTE wide, REBCNT flags)
{
	REBINT bias_old = SERIES_BIAS(series);
	REBINT size_old = Series_Allocated_Size(series);
	REBCNT tail_old = series->tail;
	REBYTE wide_old = SERIES_WIDE(series);
	REBOOL any_block = Is_Array_Series(series);

	// Extract the data pointer to take responsibility for it.  (The pointer
	// may have already been extracted if the caller is doing their own
	// updating preservation.)
	REBYTE *data_old = series->data;

	assert(series->data);
	series->data = NULL;

	// SER_EXTERNAL manages its own memory and shouldn't call Remake
	assert(!(flags & MKS_EXTERNAL));

	// SER_LOCK has unexpandable data and shouldn't call Remake
	assert(!(flags & MKS_LOCK));

	// We only let you preserve if the data is the same width as original
#if !defined(NDEBUG)
	if (flags & MKS_PRESERVE) {
		assert(wide == wide_old);
		if (flags & MKS_ARRAY) assert(SERIES_GET_FLAG(series, SER_ARRAY));
	}
#endif

	if (!Series_Data_Alloc(
		series, units + 1, wide, any_block ? MKS_ARRAY | flags : flags
	)) {
		// Put series back how it was (there may be extant references)
		series->data = data_old;
		Trap(RE_NO_MEMORY);
		DEAD_END_VOID;
	}

	if (flags & MKS_PRESERVE) {
		// Preserve as much data as possible (if it was requested, some
		// operations may extract the data pointer ahead of time and do this
		// more selectively)

		series->tail = MIN(tail_old, units);
		memcpy(series->data, data_old, series->tail * wide);
	} else
		series->tail = 0;

	TERM_SERIES(series);

	Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);
}


/*******************************************************************************
**
**  Name: "GC_Kill_Series"
**  Summary: none
**  Details: {
**      Only the garbage collector should be calling this routine.
**      It frees a series even though it is under GC management,
**      because the GC has figured out no references exist.}
**  Spec: none
**
*******************************************************************************/

void GC_Kill_Series(REBSER *series)
{
	REBCNT n;
	REBCNT size = SERIES_TOTAL(series);

	// !!! Original comment on freeing series data said: "Protect flag can
	// be used to prevent GC away from the data field".  ???
	REBOOL protect = TRUE;

	assert(!SERIES_FREED(series));

	PG_Reb_Stats->Series_Freed++;

	// Remove series from expansion list, if found:
	for (n = 1; n < MAX_EXPAND_LIST; n++) {
		if (Prior_Expand[n] == series) Prior_Expand[n] = 0;
	}

	if (SERIES_GET_FLAG(series, SER_EXTERNAL)) {
		// External series have their REBSER GC'd when Rebol doesn't need it,
		// but the data pointer itself is not one that Rebol allocated
		// !!! Should the external owner be told about the GC/free event?
	}
	else {
		REBYTE wide = SERIES_WIDE(series);
		REBCNT bias = SERIES_BIAS(series);
		series->data -= wide * bias;
		Free_Unbiased_Series_Data(series->data, Series_Allocated_Size(series));
	}

	series->info = 0; // includes width
	//series->data = BAD_MEM_PTR;
	//series->tail = 0xBAD2BAD2;
	//series->extra.size = 0xBAD3BAD3;

	Free_Node(SERIES_POOL, cast(REBNOD*, series));

	if (REB_I32_ADD_OF(GC_Ballast, size, &GC_Ballast)) {
		GC_Ballast = MAX_I32;
	}

	// GC may no longer be necessary:
	if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
}


/*******************************************************************************
**
**  Name: "Free_Series"
**  Summary: none
**  Details: {
**      Free a series, returning its memory for reuse.  You can only
**      call this on series that are not managed by the GC.}
**  Spec: none
**
*******************************************************************************/

void Free_Series(REBSER *series)
{
	// We can only free a series that is not under management by the
	// garbage collector
#if !defined(NDEBUG)
	if (SERIES_GET_FLAG(series, SER_MANAGED)) {
		Debug_Fmt("Trying to Free_Series() on a series managed by GC.");
		Panic_Series(series);
	}

	if (series->next)
		series->next->prev = series->prev;

	if (series->prev)
		series->prev->next = series->next;
	else {
		assert(series == GC_Manuals);
		GC_Manuals = series->next;
	}
#endif

	// With bookkeeping done, use the same routine the GC uses to free
	GC_Kill_Series(series);
}


/*******************************************************************************
**
**  Name: "Widen_String"
**  Summary: none
**  Details: {
**      Widen string from 1 byte to 2 bytes.
**  
**      NOTE: allocates new memory. Cached pointers are invalid.}
**  Spec: none
**
*******************************************************************************/

void Widen_String(REBSER *series, REBOOL preserve)
{
	REBINT bias_old = SERIES_BIAS(series);
	REBINT size_old = Series_Allocated_Size(series);
	REBCNT tail_old = series->tail;
	REBYTE wide_old = SERIES_WIDE(series);

	REBYTE *data_old = series->data;

	REBYTE *bp;
	REBUNI *up;
	REBCNT n;

	series->data = NULL;

#if !defined(NDEBUG)
	// We may be resizing a partially constructed series, or otherwise
	// not want to preserve the previous contents
	if (preserve)
		ASSERT_SERIES(series);
#endif

	assert(SERIES_WIDE(series) == 1);

	if (!Series_Data_Alloc(
		series, tail_old + 1, cast(REBYTE, sizeof(REBUNI)), MKS_NONE
	)) {
		// Put series back how it was (there may be extant references)
		series->data = data_old;
		Trap(RE_NO_MEMORY);
		DEAD_END_VOID;
	}

	bp = data_old;
	up = UNI_HEAD(series);

	if (preserve) {
		for (n = 0; n <= tail_old; n++) up[n] = bp[n]; // includes terminator
		SERIES_TAIL(series) = tail_old;
	}
	else {
		SERIES_TAIL(series) = 0;
		TERM_SERIES(series);
	}

	Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);

	ASSERT_SERIES(series);
}


#if !defined(NDEBUG)

/*******************************************************************************
**
**  Name: "Manage_Series_Debug"
**  Summary: none
**  Details: {
**      When a series is first created, it is in a state of being
**      manually memory managed.  Thus, you can call Free_Series on
**      it if you are sure you do not need it.  This will transition
**      a manually managed series to be one managed by the GC.  There
**      is no way to transition it back--once a series has become
**      managed, only the GC can free it.
**  
**      All series that wind up in user-visible values *must* be
**      managed, because the user can make copies of values
**      containing that series.  When these copies are made, it's
**      no longer safe to assume it's okay to free the original.
**  
**      Though marking a series as managed is just setting a bit in
**      the release build (and hence, cheap+fast), we do some more
**      work to ensure coherence in the debug build.}
**  Spec: none
**
*******************************************************************************/

void Manage_Series_Debug(REBSER *series)
{
	assert(!SERIES_GET_FLAG(series, SER_MANAGED));
	SERIES_SET_FLAG(series, SER_MANAGED);

#if !defined(NDEBUG)
	if (series->prev == NULL)
		GC_Manuals = series->next;
	else
		series->prev->next = series->next;
	if (series->next)
		series->next->prev = series->prev;
#endif
}


/*******************************************************************************
**
**  Name: "Manage_Frame_Debug"
**  Summary: none
**  Details: {
**    Special handler for making sure frames are managed by the GC,
**      specifically.  If you've poked in a wordlist from somewhere
**      else you might not be able to use this.}
**  Spec: none
**
*******************************************************************************/

void Manage_Frame_Debug(REBSER *frame)
{
	if (
		SERIES_GET_FLAG(frame, SER_MANAGED)
		!= SERIES_GET_FLAG(FRM_WORD_SERIES(frame), SER_MANAGED)
	) {
		if (!SERIES_GET_FLAG(frame, SER_MANAGED)) {
			Debug_Fmt("Frame word series is managed but frame is not!");
			Panic_Series(frame);
		}

		Debug_Fmt("Frame is managed but frame word series is not!");
		Panic_Series(FRM_WORD_SERIES(frame));
	}

	MANAGE_SERIES(FRM_WORD_SERIES(frame));
	MANAGE_SERIES(frame);
}


/*******************************************************************************
**
**  Name: "Manuals_Leak_Check_Debug"
**  Summary: none
**  Details: {
**      Routine for checking that the pointer passed in is the
**      same as the head of the series that the GC is not tracking,
**      which is used to check for leaks relative to an initial
**      status of outstanding series.}
**  Spec: none
**
*******************************************************************************/

void Manuals_Leak_Check_Debug(REBSER *manuals, const char *label_str)
{
	if (GC_Manuals != manuals) {
		REBINT count = 0;
		REBSER *temp = manuals;
		while (temp != GC_Manuals) {
			temp = temp->prev;
			if (!temp) {
				Debug_Fmt("Bad tracking chain in REBSER leak check.");
				Panic_Series(GC_Manuals);
			}
			count++;
		}
		Debug_Fmt("%d leaked REBSERs during %s", count, label_str);
		Debug_Fmt("Panic_Series() on most recent (for valgrind, ASAN)");
		Panic_Series(GC_Manuals);
	}
}


/*******************************************************************************
**
**  Name: "Assert_Value_Managed_Debug"
**  Summary: none
**  Details: {
**      Routine for checking that the pointer passed in is the
**      same as the head of the series that the GC is not tracking,
**      which is used to check for leaks relative to an initial
**      status of outstanding series.}
**  Spec: none
**
*******************************************************************************/

void Assert_Value_Managed_Debug(const REBVAL *value)
{
	if (ANY_OBJECT(value)) {
		REBSER *frame = VAL_OBJ_FRAME(value);
		if (!SERIES_GET_FLAG(frame, SER_MANAGED)) {
			Debug_Fmt("ASSERT_VALUE_MANAGED() failing on object frame");
			Panic_Series(frame);
		}
		if (!SERIES_GET_FLAG(FRM_WORD_SERIES(frame), SER_MANAGED)) {
			Debug_Fmt("ASSERT_VALUE_MANAGED() failing on object frame words");
			Panic_Series(FRM_WORD_SERIES(frame));
		}
	}
	else if (ANY_SERIES(value)) {
		if (!SERIES_GET_FLAG(VAL_SERIES(value), SER_MANAGED)) {
			Debug_Fmt("ASSERT_VALUE_MANAGED() failing on series");
			Panic_Series(VAL_SERIES(value));
		}
	}
}

#endif


/*******************************************************************************
**
**  Name: "Free_Gob"
**  Summary: none
**  Details: {
**      Free a gob, returning its memory for reuse.}
**  Spec: none
**
*******************************************************************************/

void Free_Gob(REBGOB *gob)
{
	FREE_GOB(gob);

	Free_Node(GOB_POOL, (REBNOD *)gob);

	if (REB_I32_ADD_OF(GC_Ballast, Mem_Pools[GOB_POOL].wide, &GC_Ballast)) {
		GC_Ballast = MAX_I32;
	}

	if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
}


/*******************************************************************************
**
**  Name: "Series_In_Pool"
**  Summary: none
**  Details: {
**      Confirm that the series value is in the series pool.}
**  Spec: none
**
*******************************************************************************/

REBFLG Series_In_Pool(REBSER *series)
{
	REBSEG	*seg;
	REBSER *start;

	// Scan all series headers to check that series->size is correct:
	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		start = (REBSER *) (seg + 1);
		if (series >= start && series <= (REBSER*)((REBYTE*)start + seg->size - sizeof(REBSER)))
			return TRUE;
	}

	return FALSE;
}


/*******************************************************************************
**
**  Name: "Check_Memory"
**  Summary: none
**  Details: {
**      FOR DEBUGGING ONLY:
**      Traverse the free lists of all pools -- just to prove we can.
**      This is useful for finding corruption from bad memory writes,
**      because a write past the end of a node will destory the pointer
**      for the next free area.}
**  Spec: none
**
*******************************************************************************/

REBCNT Check_Memory(void)
{
	REBCNT pool_num;
	REBNOD *node;
	REBCNT count = 0;
	REBSEG *seg;
	REBSER *series;

	//Debug_Str("<ChkMem>");
	PG_Reb_Stats->Free_List_Checked++;

	// Scan all series headers to check that series->size is correct:
	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (!SERIES_REST(series) || !series->data)
					goto crash;
				// Does the size match a known pool?
				pool_num = FIND_POOL(SERIES_TOTAL(series));
				// Just to be sure the pool matches the allocation:
				if (pool_num < SERIES_POOL && Mem_Pools[pool_num].wide != SERIES_TOTAL(series))
					goto crash;
			}
			series++;
		}
	}

	// Scan each memory pool:
	for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
		count = 0;
		// Check each free node in the memory pool:
		for (node = cast(void **, Mem_Pools[pool_num].first); node; node = cast(void**, *node)) {
			count++;
			// The node better belong to one of the pool's segments:
			for (seg = Mem_Pools[pool_num].segs; seg; seg = seg->next) {
				if ((REBUPT)node > (REBUPT)seg && (REBUPT)node < (REBUPT)seg + (REBUPT)seg->size) break;
			}
			if (!seg) goto crash;
		}
		// The number of free nodes must agree with header:
		if (
			(Mem_Pools[pool_num].free != count) ||
			(Mem_Pools[pool_num].free == 0 && Mem_Pools[pool_num].first != 0)
		)
			goto crash;
	}

	return count;
crash:
	Panic_DEAD_END(RP_CORRUPT_MEMORY);
	return 0; // for compiler only
}


/*******************************************************************************
**
**  Name: "Dump_All"
**  Summary: none
**  Details: "^/        Dump all series of a given size."
**  Spec: none
**
*******************************************************************************/

void Dump_All(REBINT size)
{
	REBSEG	*seg;
	REBSER *series;
	REBCNT count;
	REBCNT n = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (SERIES_WIDE(series) == size) {
					//Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SERIES_TOTAL(series), series->data);
					Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SERIES_REST(series), (SERIES_LABEL(series) ? SERIES_LABEL(series) : "-"));
				}
			}
			series++;
		}
	}
}

/*******************************************************************************
**
**  Name: "Dump_Series_In_Pool"
**  Summary: none
**  Details: {
**      Dump all series in pool @pool_id, UNKNOWN (-1) for all pools}
**  Spec: none
**
*******************************************************************************/

void Dump_Series_In_Pool(REBCNT pool_id)
{
	REBSEG	*seg;
	REBSER *series;
	REBCNT count;
	REBCNT n = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (
					pool_id == UNKNOWN
					|| FIND_POOL(SERIES_TOTAL(series)) == pool_id
				) {
					Debug_Fmt(
							  Str_Dump, //"%s Series %x %s: Wide: %2d Size: %6d - Bias: %d Tail: %d Rest: %d Flags: %x"
							  "Dump",
							  series,
							  (SERIES_LABEL(series) ? SERIES_LABEL(series) : "-"),
							  SERIES_WIDE(series),
							  SERIES_TOTAL(series),
							  SERIES_BIAS(series),
							  SERIES_TAIL(series),
							  SERIES_REST(series),
							  SERIES_FLAGS(series)
							 );
					//Dump_Series(series, "Dump");
					if (Is_Array_Series(series)) {
						Debug_Values(BLK_HEAD(series), SERIES_TAIL(series), 1024); /* FIXME limit */
					} else{
						Dump_Bytes(series->data, (SERIES_TAIL(series)+1) * SERIES_WIDE(series));
					}
				}
			}
			series++;
		}
	}
}


/*******************************************************************************
**
**  Name: "Dump_Pools"
**  Summary: none
**  Details: "^/        Print statistics about all memory pools."
**  Spec: none
**
*******************************************************************************/

static void Dump_Pools(void)
{
	REBSEG	*seg;
	REBCNT	segs;
	REBCNT	size;
	REBCNT  used;
	REBCNT	total = 0;
	REBCNT  tused = 0;
	REBCNT  n;

	for (n = 0; n < SYSTEM_POOL; n++) {
		size = segs = 0;

		for (seg = Mem_Pools[n].segs; seg; seg = seg->next, segs++)
			size += seg->size;

		used = Mem_Pools[n].has - Mem_Pools[n].free;
		Debug_Fmt("Pool[%-2d] %-4dB %-5d/%-5d:%-4d (%-2d%%) %-2d segs, %-07d total",
			n,
			Mem_Pools[n].wide,
			used,
			Mem_Pools[n].has,
			Mem_Pools[n].units,
			Mem_Pools[n].has ? ((used * 100) / Mem_Pools[n].has) : 0,
			segs,
			size
		);

		tused += used * Mem_Pools[n].wide;
		total += size;
	}
	Debug_Fmt("Pools used %d of %d (%2d%%)", tused, total, (tused*100) / total);
	Debug_Fmt("System pool used %d", Mem_Pools[SYSTEM_POOL].has);
	//Debug_Fmt("Raw allocator reports %d", PG_Mem_Usage);
}


/*******************************************************************************
**
**  Name: "Inspect_Series"
**  Summary: none
**  Details: none
**  Spec: none
**
*******************************************************************************/

REBU64 Inspect_Series(REBCNT flags)
{
	REBSEG	*seg;
	REBSER	*series;
	REBCNT  segs, n, tot, blks, strs, unis, nons, odds, fre;
	REBCNT  str_size, uni_size, blk_size, odd_size, seg_size, fre_size;
	REBFLG  f = 0;
	REBINT  pool_num;
#ifdef SERIES_LABELS
	REBYTE  *kind;
#endif
	REBU64  tot_size;

	segs = tot = blks = strs = unis = nons = odds = fre = 0;
	seg_size = str_size = uni_size = blk_size = odd_size = fre_size = 0;
	tot_size = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {

		seg_size += seg->size;
		segs++;

		series = (REBSER *) (seg + 1);

		for (n = Mem_Pools[SERIES_POOL].units; n > 0; n--) {

			if (SERIES_WIDE(series)) {
				tot++;
				tot_size += SERIES_TOTAL(series);
				f = 0;
			} else {
				fre++;
			}

#ifdef SERIES_LABELS
			kind = "----";
			if (SERIES_GET_FLAG(series, SER_KEEP)) kind = "KEEP";
			//if (Find_Root(series)) kind = "ROOT";
			if (!SERIES_FREED(series) && series->label) {
				Debug_Fmt_("%08x: %16s %s ", series, series->label, kind);
				f = 1;
			} else if (!SERIES_FREED(series) && (flags & 0x100)) {
				Debug_Fmt_("%08x: %s ", series, kind);
				f = 1;
			}
#endif
			if (Is_Array_Series(series)) {
				blks++;
				blk_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("BLOCK ");
			}
			else if (SERIES_WIDE(series) == 1) {
				strs++;
				str_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("STRING");
			}
			else if (SERIES_WIDE(series) == sizeof(REBUNI)) {
				unis++;
				uni_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("UNICOD");
			}
			else if (SERIES_WIDE(series)) {
				odds++;
				odd_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("ODD[%d]", SERIES_WIDE(series));
			}
			if (f && SERIES_WIDE(series)) {
				Debug_Fmt(" units: %-5d tail: %-5d bytes: %-7d", SERIES_REST(series), SERIES_TAIL(series), SERIES_TOTAL(series));
			}

			series++;
		}
	}

	// Size up unused memory:
	for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
		fre_size += Mem_Pools[pool_num].free * Mem_Pools[pool_num].wide;
	}

	if (flags & 1) {
		Debug_Fmt(
			  "Series Memory Info:\n"
			  "  node   size = %d\n"
			  "  series size = %d\n"
			  "  %-6d segs = %-7d bytes - headers\n"
			  "  %-6d blks = %-7d bytes - blocks\n"
			  "  %-6d strs = %-7d bytes - byte strings\n"
			  "  %-6d unis = %-7d bytes - unicode strings\n"
			  "  %-6d odds = %-7d bytes - odd series\n"
			  "  %-6d used = %-7d bytes - total used\n"
			  "  %-6d free / %-7d bytes - free headers / node-space\n"
			  ,
			  sizeof(REBVAL),
			  sizeof(REBSER),
			  segs, seg_size,
			  blks, blk_size,
			  strs, str_size,
			  unis, uni_size,
			  odds, odd_size,
			  tot,  tot_size,
			  fre,  fre_size   // the 2 are not related
		);
	}

	if (flags & 2) Dump_Pools();

	return tot_size;
}

