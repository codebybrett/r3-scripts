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
**  Module:  u-compress.c
**  Summary: interface to zlib compression
**  Section: utility
**  Notes:
**
***********************************************************************/

#include "sys-core.h"
#include "sys-zlib.h"

/*
 *  This number represents the top file size that,
 *  if the data is random, will produce a larger output
 *  file than input.  The number is really a bit smaller
 *  but we like to be safe. -- SN
 */
#define STERLINGS_MAGIC_NUMBER      10000

/*
 *  This number represents the largest that a small file that expands
 *  on compression can expand.  The actual value is closer to
 *  500 bytes but why take chances? -- SN
 */
#define STERLINGS_MAGIC_FIX         1024

/*
 *  The why_compress_constant is here to satisfy the condition that
 *  somebody might just try compressing some big file that is already well
 *  compressed (or expands for some other wild reason).  So we allocate
 *  a compression buffer a bit larger than the original file size.
 *  10% is overkill for really large files so some other limit might
 *  be a good idea.
*/
#define WHY_COMPRESS_CONSTANT       0.1

//
//  Compress: C
//  
//      Compress a binary (only).
//      data
//      /part
//      length
//      /crc32
//  
//      Note: If the file length is "small", it can't overrun on
//      compression too much so we use our magic numbers; otherwise,
//      we'll just be safe by a percentage of the file size.  This may
//      be a bit much, though.
//

REBSER *Compress(REBSER *input, REBINT index, REBINT len, REBFLG use_crc)
{
	// NOTE: The use_crc flag is not present in Zlib 1.2.8
	// Instead, compress's fifth paramter is the compression level
	// It can be a value from 1 to 9, or Z_DEFAULT_COMPRESSION if you
	// want it to pick what the library author considers the "worth it"
	// tradeoff of time to generally suggest.

	uLongf size;
	REBSER *output;
	REBINT err;
	REBYTE out_size[sizeof(REBCNT)];

	if (len < 0) Trap_DEAD_END(RE_PAST_END); // !!! better msg needed
	size = len + (len > STERLINGS_MAGIC_NUMBER ? len / 10 + 12 : STERLINGS_MAGIC_FIX);
	output = Make_Binary(size);

	// dest, dest-len, src, src-len, level
	err = z_compress2(BIN_HEAD(output), &size, BIN_HEAD(input) + index, len, Z_DEFAULT_COMPRESSION);
	if (err) {
		REBVAL arg;
		if (err == Z_MEM_ERROR) Trap_DEAD_END(RE_NO_MEMORY);
		SET_INTEGER(&arg, err);
		Trap1_DEAD_END(RE_BAD_PRESS, &arg); //!!!provide error string descriptions
	}
	SET_STR_END(output, size);
	SERIES_TAIL(output) = size;
	REBCNT_To_Bytes(out_size, (REBCNT)len); // Tag the size to the end.
	Append_Series(output, (REBYTE*)out_size, sizeof(REBCNT));
	if (SERIES_AVAIL(output) > 1024) {
		// Is there wasted space?  Trim it down if too big.
		// !!! Revisit this based on mem alloc alg.
		REBSER *smaller = Copy_Sequence(output);
		Free_Series(output);
		output = smaller;
	}

	return output;
}


//
//  Decompress: C
//  
//      Decompress a binary (only).
//  
//      Rebol's compress/decompress functions store an extra length
//      at the tail of the data, to double-check the zlib result
//

REBSER *Decompress(const REBYTE *data, REBCNT len, REBCNT limit, REBFLG use_crc)
{
	// NOTE: The use_crc flag is not present in Zlib 1.2.8
	// There is no fifth parameter to uncompress matching the fifth to compress

	uLongf size;
	REBSER *output;
	REBINT err;

	// Get the size from the end and make the output buffer that size.
	if (len <= 4) Trap_DEAD_END(RE_PAST_END); // !!! better msg needed
	size = Bytes_To_REBCNT(data + len - sizeof(REBCNT));

	// NOTE: You can hit this if you 'make prep' without doing a full rebuild
	// (If you 'make clean' and build again and this goes away, it was that)
	if (limit && size > limit) Trap_Num(RE_SIZE_LIMIT, size);

	output = Make_Binary(size);

	err = z_uncompress(BIN_HEAD(output), &size, data, len);
	if (err) {
		REBVAL arg;

		Free_Series(output);
		if (PG_Boot_Phase < BOOT_ERRORS) return 0;
		if (err == Z_MEM_ERROR) Trap_DEAD_END(RE_NO_MEMORY);
		SET_INTEGER(&arg, err);
		Trap1_DEAD_END(RE_BAD_PRESS, &arg); //!!!provide error string descriptions
	}
	SET_STR_END(output, size);
	SERIES_TAIL(output) = size;

	return output;
}
