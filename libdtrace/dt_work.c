/*
 * Oracle Linux DTrace.
 * Copyright (c) 2006, 2021, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

#include <dt_impl.h>
#include <dt_peb.h>
#include <dt_probe.h>
#include <dt_bpf.h>
#include <dt_state.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <libproc.h>
#include <port.h>
#include <linux/perf_event.h>
#include <sys/epoll.h>
#include <valgrind/valgrind.h>

void
BEGIN_probe(void)
{
}

void
END_probe(void)
{
}

int
dtrace_status(dtrace_hdl_t *dtp)
{
	if (!dtp->dt_active)
		return DTRACE_STATUS_NONE;

	if (dtp->dt_stopped)
		return DTRACE_STATUS_STOPPED;

	if (dt_state_get_activity(dtp) == DT_ACTIVITY_DRAINING) {
		if (!dtp->dt_stopped)
			dtrace_stop(dtp);

		return DTRACE_STATUS_EXITED;
	}

	return DTRACE_STATUS_OKAY;
}

int
dtrace_go(dtrace_hdl_t *dtp, uint_t cflags)
{
	size_t			size;
	int			err;
	struct epoll_event	ev;

	if (dtp->dt_active)
		return dt_set_errno(dtp, EINVAL);

	/*
	 * Create the global BPF maps.  This is done only once regardless of
	 * how many programs there are.
	 */
	err = dt_bpf_gmap_create(dtp);
	if (err)
		return err;

	err = dt_bpf_load_progs(dtp, cflags);
	if (err)
		return err;

	/*
	 * Set up the event polling file descriptor.
	 */
	dtp->dt_poll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (dtp->dt_poll_fd < 0)
		return dt_set_errno(dtp, errno);

	/*
	 * Register the proc eventfd descriptor to receive notifications about
	 * process exit.
	 */
	ev.events = EPOLLIN;
	ev.data.ptr = dtp->dt_procs;
	if (epoll_ctl(dtp->dt_poll_fd, EPOLL_CTL_ADD, dtp->dt_proc_fd, &ev) ==
	    -1)
		return dt_set_errno(dtp, errno);

	/*
	 * We need enough space for the pref_event_header, a 32-bit size, a
	 * 4-byte gap, and the largest trace data record we may be writing to
	 * the buffer.  In other words, the buffer needs to be large enough to
	 * hold at least one perf-encapsulated trace data record.
	 */
	dtrace_getopt(dtp, "bufsize", &size);
	if (size == 0 ||
	    size < sizeof(struct perf_event_header) + sizeof(uint32_t) +
		   dtp->dt_maxreclen)
		return dt_set_errno(dtp, EDT_BUFTOOSMALL);
	if (dt_pebs_init(dtp, size) == -1)
		return dt_set_errno(dtp, EDT_NOMEM);

	/*
	 * We must initialize the aggregation consumer handling before we
	 * trigger the BEGIN probe.
	 */
	err = dt_aggregate_go(dtp);
	if (err)
		return err;

	if (RUNNING_ON_VALGRIND)
		VALGRIND_NON_SIMD_CALL0(BEGIN_probe);
	else
		BEGIN_probe();

	dtp->dt_active = 1;
	dtp->dt_beganon = dt_state_get_beganon(dtp);

	/*
	 * An exit() action during the BEGIN probe processing will cause the
	 * activity state to become STOPPED once the BEGIN probe is done.  We
	 * need to move it back to DRAINING in that case.
	 */
	if (dt_state_get_activity(dtp) == DT_ACTIVITY_STOPPED)
		dt_state_set_activity(dtp, DT_ACTIVITY_DRAINING);

#if 0
	if (dt_options_load(dtp) == -1)
		return dt_set_errno(dtp, errno);
#endif

	return 0;
}

int
dtrace_stop(dtrace_hdl_t *dtp)
{
	int		gen = dtp->dt_statusgen;

	if (dtp->dt_stopped)
		return 0;

	if (dt_state_get_activity(dtp) < DT_ACTIVITY_DRAINING)
		dt_state_set_activity(dtp, DT_ACTIVITY_DRAINING);

	if (RUNNING_ON_VALGRIND)
		VALGRIND_NON_SIMD_CALL0(END_probe);
	else
		END_probe();

	dtp->dt_stopped = 1;
	dtp->dt_endedon = dt_state_get_endedon(dtp);

#if 0
	/*
	 * Now that we're stopped, we're going to get status one final time.
	 */
	if (dt_ioctl(dtp, DTRACEIOC_STATUS, &dtp->dt_status[gen]) == -1)
		return dt_set_errno(dtp, errno);
#endif

	if (dt_handle_status(dtp, &dtp->dt_status[gen ^ 1],
	    &dtp->dt_status[gen]) == -1)
		return -1;

	return 0;
}

#if 0
dtrace_workstatus_t
dtrace_work(dtrace_hdl_t *dtp, FILE *fp,
    dtrace_consume_probe_f *pfunc, dtrace_consume_rec_f *rfunc, void *arg)
{
	int status = dtrace_status(dtp);
	dtrace_optval_t policy = dtp->dt_options[DTRACEOPT_BUFPOLICY];
	dtrace_workstatus_t rval;

	switch (status) {
	case DTRACE_STATUS_EXITED:
	case DTRACE_STATUS_FILLED:
	case DTRACE_STATUS_STOPPED:
		/*
		 * Tracing is stopped.  We now want to force dtrace_consume()
		 * and dtrace_aggregate_snap() to proceed, regardless of
		 * switchrate and aggrate.  We do this by clearing the times.
		 */
		dtp->dt_lastswitch = 0;
		dtp->dt_lastagg = 0;
		rval = DTRACE_WORKSTATUS_DONE;
		break;

	case DTRACE_STATUS_NONE:
	case DTRACE_STATUS_OKAY:
		rval = DTRACE_WORKSTATUS_OKAY;
		break;

	default:
		return DTRACE_WORKSTATUS_ERROR;
	}

	if ((status == DTRACE_STATUS_NONE || status == DTRACE_STATUS_OKAY) &&
	    policy != DTRACEOPT_BUFPOLICY_SWITCH) {
		/*
		 * There either isn't any status or things are fine -- and
		 * this is a "ring" or "fill" buffer.  We don't want to consume
		 * any of the trace data or snapshot the aggregations; we just
		 * return.
		 */
		assert(rval == DTRACE_WORKSTATUS_OKAY);
		return rval;
	}

#if 0
	if (dtrace_aggregate_snap(dtp) == -1)
		return DTRACE_WORKSTATUS_ERROR;
#endif

	if (dtrace_consume(dtp, fp, pfunc, rfunc, arg) == -1)
		return DTRACE_WORKSTATUS_ERROR;

	return rval;
}
#else
dtrace_workstatus_t
dtrace_work(dtrace_hdl_t *dtp, FILE *fp, dtrace_consume_probe_f *pfunc,
	    dtrace_consume_rec_f *rfunc, void *arg)
{
	dtrace_workstatus_t	rval;

	switch (dtrace_status(dtp)) {
	case DTRACE_STATUS_EXITED:
	case DTRACE_STATUS_STOPPED:
		rval = DTRACE_WORKSTATUS_DONE;
		break;
	case DTRACE_STATUS_NONE:
	case DTRACE_STATUS_OKAY:
		rval = DTRACE_WORKSTATUS_OKAY;
		break;
	default:
		return DTRACE_WORKSTATUS_ERROR;
	}

	if (dtrace_consume(dtp, fp, pfunc, rfunc, arg) ==
	    DTRACE_WORKSTATUS_ERROR)
		return DTRACE_WORKSTATUS_ERROR;

	return rval;
}
#endif
