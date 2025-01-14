/*
 * Oracle Linux DTrace.
 * Copyright (c) 2021, 2022, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

#ifndef BPF_LIB_H
#define BPF_LIB_H

/*
 * Explicit inline assembler to implement a dynamic upper bound check:
 *
 *	if (var > bnd)
 *		var = bnd;
 *
 * The BPF GCC compiler does not guarantee that the bound (expected to be a
 * variable that holds a constant value) will be encoded in the source register
 * while the BPF verifier does require that (for now).
 */
#define set_upper_bound(var, bnd) \
	asm ("jle %0, %1, 1f\n\t" \
	     "mov %0, %1\n\t" \
	     "1:" \
		: "+r" (var) \
		: "r" (bnd) \
		: /* no clobbers */ \
	);

/*
 * Explicit inline assembler to implement a dynamic lower bound check:
 *
 *	if (var < bnd)
 *		var = bnd;
 *
 * The BPF GCC compiler does not guarantee that the bound (expected to be a
 * variable that holds a constant value) will be encoded in the source register
 * while the BPF verifier does require that (for now).
 */
#define set_lower_bound(var, bnd) \
	asm ("jge %0, %1, 1f\n\t" \
	     "mov %0, %1\n\t" \
	     "1:" \
		: "+r" (var) \
		: "r" (bnd) \
		: /* no clobbers */ \
	);

/*
 * Explicit inline assembler to implement a non-negative bound check:
 *
 *	if (var < 0)
 *		var = 0;
 */
#define set_not_neg_bound(var) \
	asm ("jsge %0, 0, 1f\n\t" \
             "mov %0, 0\n\t" \
             "1:" \
                : "+r" (var) \
                : /* no inputs */ \
                : /* no clobbers */ \
        );


#endif /* BPF_LIB_H */
