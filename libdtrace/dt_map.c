/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, 2021, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <dt_impl.h>
#include <dt_pcb.h>
#include <dt_probe.h>
#include <dt_printf.h>

dtrace_datadesc_t *
dt_datadesc_hold(dtrace_datadesc_t *ddp)
{
	ddp->dtdd_refcnt++;
	return ddp;
}

void
dt_datadesc_release(dtrace_hdl_t *dtp, dtrace_datadesc_t *ddp)
{
	int			i;
	dtrace_recdesc_t	*rec;

	if (--ddp->dtdd_refcnt > 0)
		return;

	for (i = 0, rec = &ddp->dtdd_recs[0]; i < ddp->dtdd_nrecs; i++, rec++) {
		if (rec->dtrd_format != NULL)
			dt_printf_destroy(rec->dtrd_format);
	}
	dt_free(dtp, ddp->dtdd_recs);
	dt_free(dtp, ddp);
}

dtrace_datadesc_t *
dt_datadesc_create(dtrace_hdl_t *dtp)
{
	dtrace_datadesc_t	*ddp;

	ddp = dt_zalloc(dtp, sizeof(dtrace_datadesc_t));
	if (ddp == NULL) {
		dt_set_errno(dtp, EDT_NOMEM);
		return NULL;
	}

	return dt_datadesc_hold(ddp);
}

int
dt_datadesc_finalize(dtrace_hdl_t *dtp, dtrace_datadesc_t *ddp)
{
	dt_pcb_t		*pcb = dtp->dt_pcb;
	dtrace_datadesc_t	*oddp = pcb->pcb_ddesc;

	/*
	 * If the number of allocated data records is greater than the actual
	 * number needed, we create a copy with the right number of records and
	 * free the oversized one.
	 */
	if (oddp->dtdd_nrecs < pcb->pcb_maxrecs) {
		dtrace_recdesc_t	*nrecs;

		nrecs = dt_calloc(dtp, oddp->dtdd_nrecs,
				  sizeof(dtrace_recdesc_t));
		if (nrecs == NULL)
			return dt_set_errno(dtp, EDT_NOMEM);

		memcpy(nrecs, ddp->dtdd_recs,
		       oddp->dtdd_nrecs * sizeof(dtrace_recdesc_t));
		dt_free(dtp, ddp->dtdd_recs);
		ddp->dtdd_recs = nrecs;
		pcb->pcb_maxrecs = oddp->dtdd_nrecs;
	}

	ddp->dtdd_nrecs = oddp->dtdd_nrecs;

	return 0;
}

/*
 * Associate a probe data description and probe description with an enabled
 * probe ID.  This means that the given ID refers to the program matching the
 * probe data description being attached to the probe that matches the probe
 * description.
 */
dtrace_epid_t
dt_epid_add(dtrace_hdl_t *dtp, dtrace_datadesc_t *ddp, dtrace_id_t prid)
{
	dtrace_id_t	max = dtp->dt_maxprobe;
	dtrace_epid_t	epid;

	epid = dtp->dt_nextepid++;
	if (epid >= max || dtp->dt_ddesc == NULL) {
		dtrace_id_t		nmax = max ? (max << 1) : 2;
		dtrace_datadesc_t	**nddesc;
		dtrace_probedesc_t	**npdesc;

		nddesc = dt_calloc(dtp, nmax, sizeof(void *));
		npdesc = dt_calloc(dtp, nmax, sizeof(void *));
		if (nddesc == NULL || npdesc == NULL) {
			dt_free(dtp, nddesc);
			dt_free(dtp, npdesc);
			return dt_set_errno(dtp, EDT_NOMEM);
		}

		if (dtp->dt_ddesc != NULL) {
			size_t	osize = max * sizeof(void *);

			memcpy(nddesc, dtp->dt_ddesc, osize);
			dt_free(dtp, dtp->dt_ddesc);
			memcpy(npdesc, dtp->dt_pdesc, osize);
			dt_free(dtp, dtp->dt_pdesc);
		}

		dtp->dt_ddesc = nddesc;
		dtp->dt_pdesc = npdesc;
		dtp->dt_maxprobe = nmax;
	}

	if (dtp->dt_ddesc[epid] != NULL)
		return epid;

	dtp->dt_ddesc[epid] = dt_datadesc_hold(ddp);
	dtp->dt_pdesc[epid] = (dtrace_probedesc_t *)dtp->dt_probes[prid]->desc;

	return epid;
}

int
dt_epid_lookup(dtrace_hdl_t *dtp, dtrace_epid_t epid, dtrace_datadesc_t **ddp,
	       dtrace_probedesc_t **pdp)
{
	if (epid >= dtp->dt_maxprobe ||
	    dtp->dt_ddesc[epid] == NULL || dtp->dt_pdesc[epid] == NULL)
		return -1;

	*ddp = dtp->dt_ddesc[epid];
	*pdp = dtp->dt_pdesc[epid];

	return 0;
}

void
dt_epid_destroy(dtrace_hdl_t *dtp)
{
	size_t i;

	assert((dtp->dt_pdesc != NULL && dtp->dt_ddesc != NULL &&
	    dtp->dt_maxprobe > 0) || (dtp->dt_pdesc == NULL &&
	    dtp->dt_ddesc == NULL && dtp->dt_maxprobe == 0));

	if (dtp->dt_pdesc == NULL)
		return;

	for (i = 0; i < dtp->dt_maxprobe; i++) {
		if (dtp->dt_ddesc[i] == NULL) {
			assert(dtp->dt_pdesc[i] == NULL);
			continue;
		}

		dt_datadesc_release(dtp, dtp->dt_ddesc[i]);
		assert(dtp->dt_pdesc[i] != NULL);
	}

	free(dtp->dt_pdesc);
	dtp->dt_pdesc = NULL;

	free(dtp->dt_ddesc);
	dtp->dt_ddesc = NULL;
	dtp->dt_nextepid = 0;
	dtp->dt_maxprobe = 0;
}

uint32_t
dt_rec_add(dtrace_hdl_t *dtp, dt_cg_gap_f gapf, dtrace_actkind_t kind,
	   uint32_t size, uint16_t alignment, dt_pfargv_t *pfp, uint64_t arg)
{
	dt_pcb_t		*pcb = dtp->dt_pcb;
	uint32_t		off;
	uint32_t		gap;
	dtrace_datadesc_t	*ddp = pcb->pcb_ddesc;
	dtrace_recdesc_t	*rec;
	int			cnt, max;

	assert(gapf);
	assert(alignment > 0 && alignment <= 8 &&
	       (alignment & (alignment - 1)) == 0);

	/* make more space if necessary */
	cnt = ddp->dtdd_nrecs + 1;
	max = pcb->pcb_maxrecs;
	if (cnt >= max) {
		int			nmax = max ? (max << 1) : cnt;
		dtrace_recdesc_t	*nrecs;

		nrecs = dt_calloc(dtp, nmax, sizeof(dtrace_recdesc_t));
		if (nrecs == NULL)
			longjmp(pcb->pcb_jmpbuf, EDT_NOMEM);

		if (ddp->dtdd_recs != NULL) {
			size_t	osize = max * sizeof(dtrace_recdesc_t);

			memcpy(nrecs, ddp->dtdd_recs, osize);
			dt_free(dtp, ddp->dtdd_recs);
		}

		ddp->dtdd_recs = nrecs;
		pcb->pcb_maxrecs = nmax;
	}

	/* add the new record */
	rec = &ddp->dtdd_recs[ddp->dtdd_nrecs++];
	off = (pcb->pcb_bufoff + (alignment - 1)) & ~(alignment - 1);
	rec->dtrd_action = kind;
	rec->dtrd_size = size;
	rec->dtrd_offset = off;
	rec->dtrd_alignment = alignment;
	rec->dtrd_format = pfp;
	rec->dtrd_arg = arg;

	/* fill in the gap */
	gap = off - pcb->pcb_bufoff;
	if (gap > 0)
		(*gapf)(pcb, gap);

	/* update offset */
	pcb->pcb_bufoff = off + size;

	return off;
}

int
dt_aggid_add(dtrace_hdl_t *dtp, const dt_ident_t *aid)
{
	dtrace_id_t		max;
	dtrace_aggdesc_t	*agg;
	dtrace_recdesc_t	*recs;
	dtrace_aggid_t		id = aid->di_id;
	dt_ident_t		*fid = aid->di_iarg;
	int			i;
	uint_t			off = 0;

	while (id >= (max = dtp->dt_maxagg) || dtp->dt_adesc == NULL) {
		dtrace_id_t		nmax = max ? (max << 1) : 1;
		dtrace_aggdesc_t	**nadesc;

		nadesc = dt_calloc(dtp, nmax, sizeof(void *));
		if (nadesc == NULL)
			return dt_set_errno(dtp, EDT_NOMEM);

		if (dtp->dt_adesc != NULL) {
			size_t	osize = max * sizeof(void *);

			memcpy(nadesc, dtp->dt_adesc, osize);
			free(dtp->dt_adesc);
		}

		dtp->dt_adesc = nadesc;
		dtp->dt_maxagg = nmax;
	}

	/* Already added? */
	if (dtp->dt_adesc[id] != NULL)
		return 0;

	agg = dt_zalloc(dtp, sizeof(dtrace_aggdesc_t));
	if (agg == NULL)
		return dt_set_errno(dtp, EDT_NOMEM);

	/*
	 * Note the relationship between the aggregation storage
	 * size (di_size) and the aggregation data size (dtagd_size):
	 *     di_size = dtagd_size * (num copies) + (size of latch seq #)
	 */
	agg->dtagd_id = id;
	agg->dtagd_name = aid->di_name;
	agg->dtagd_sig = ((dt_idsig_t *)aid->di_data)->dis_auxinfo;
	agg->dtagd_varid = aid->di_id;
	agg->dtagd_size = (aid->di_size - sizeof(uint64_t)) / DT_AGG_NUM_COPIES;
	agg->dtagd_nrecs = agg->dtagd_size / sizeof(uint64_t);

	recs = dt_calloc(dtp, agg->dtagd_nrecs, sizeof(dtrace_recdesc_t));
	if (recs == NULL) {
		dt_free(dtp, agg);
		return dt_set_errno(dtp, EDT_NOMEM);
	}

	agg->dtagd_recs = recs;

	for (i = 0; i < agg->dtagd_nrecs; i++) {
		dtrace_recdesc_t	*rec = &recs[i];

		rec->dtrd_action = fid->di_id;
		rec->dtrd_size = sizeof(uint64_t);
		rec->dtrd_offset = off;
		rec->dtrd_alignment = sizeof(uint64_t);
		rec->dtrd_format = NULL;
		rec->dtrd_arg = 1;

		off += sizeof(uint64_t);
	}

	dtp->dt_adesc[id] = agg;

	return 0;
}

int
dt_aggid_lookup(dtrace_hdl_t *dtp, dtrace_aggid_t aggid, dtrace_aggdesc_t **adp)
{
	if (aggid >= dtp->dt_maxagg || dtp->dt_adesc[aggid] == NULL)
		return -1;

	*adp = dtp->dt_adesc[aggid];

	return 0;
}

void
dt_aggid_destroy(dtrace_hdl_t *dtp)
{
	size_t i;

	assert((dtp->dt_adesc != NULL && dtp->dt_maxagg != 0) ||
	       (dtp->dt_adesc == NULL && dtp->dt_maxagg == 0));

	if (dtp->dt_adesc == NULL)
		return;

	for (i = 0; i < dtp->dt_maxagg; i++) {
		if (dtp->dt_adesc[i] != NULL) {
			dt_free(dtp, dtp->dt_adesc[i]->dtagd_recs);
			dt_free(dtp, dtp->dt_adesc[i]);
		}
	}

	dt_free(dtp, dtp->dt_adesc);
	dtp->dt_adesc = NULL;
	dtp->dt_maxagg = 0;
}
