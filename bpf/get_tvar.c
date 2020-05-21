// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, 2020, Oracle and/or its affiliates. All rights reserved.
 */
#include <linux/bpf.h>
#include <stdint.h>
#include <bpf-helpers.h>

#ifndef noinline
# define noinline	__attribute__((noinline))
#endif

extern struct bpf_map_def tvars;

noinline uint64_t dt_get_tvar(uint32_t id)
{
	uint64_t	*val;

	val = bpf_map_lookup_elem(&tvars, &id);
	return val ? *val : 0;
}
