/*
 * Oracle Linux DTrace.
 * Copyright (c) 2009, 2022, Oracle and/or its affiliates. All rights reserved.
 * Licensed under the Universal Permissive License v 1.0 as shown at
 * http://oss.oracle.com/licenses/upl.
 */

#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <port.h>

#include <dt_printf.h>
#include <dt_string.h>
#include <dt_impl.h>

/*ARGSUSED*/
static int
pfcheck_addr(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_pointer(dnp) || dt_node_is_integer(dnp);
}

/*ARGSUSED*/
static int
pfcheck_kaddr(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_pointer(dnp) || dt_node_is_integer(dnp) ||
	    dt_node_is_symaddr(dnp);
}

/*ARGSUSED*/
static int
pfcheck_uaddr(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	dtrace_hdl_t *dtp = pfv->pfv_dtp;
	dt_ident_t *idp = dt_idhash_lookup(dtp->dt_macros, "target");

	if (dt_node_is_usymaddr(dnp))
		return 1;

	if (idp == NULL || idp->di_id == 0)
		return 0;

	return dt_node_is_pointer(dnp) || dt_node_is_integer(dnp);
}

/*ARGSUSED*/
static int
pfcheck_stack(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_stack(dnp);
}

/*ARGSUSED*/
static int
pfcheck_time(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_integer(dnp) &&
	    dt_node_type_size(dnp) == sizeof(uint64_t);
}

/*ARGSUSED*/
static int
pfcheck_str(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	ctf_file_t *ctfp;
	ctf_encoding_t e;
	ctf_arinfo_t r;
	ctf_id_t base;
	uint_t kind;

	if (dt_node_is_string(dnp))
		return 1;

	ctfp = dnp->dn_ctfp;
	base = ctf_type_resolve(ctfp, dnp->dn_type);
	kind = ctf_type_kind(ctfp, base);

	return kind == CTF_K_ARRAY && ctf_array_info(ctfp, base, &r) == 0 &&
	    (base = ctf_type_resolve(ctfp, r.ctr_contents)) != CTF_ERR &&
	    ctf_type_encoding(ctfp, base, &e) == 0 && IS_CHAR(e);
}

/*ARGSUSED*/
static int
pfcheck_wstr(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_id_t base = ctf_type_resolve(ctfp, dnp->dn_type);
	uint_t kind = ctf_type_kind(ctfp, base);

	ctf_encoding_t e;
	ctf_arinfo_t r;

	return kind == CTF_K_ARRAY && ctf_array_info(ctfp, base, &r) == 0 &&
	    (base = ctf_type_resolve(ctfp, r.ctr_contents)) != CTF_ERR &&
	    ctf_type_kind(ctfp, base) == CTF_K_INTEGER &&
	    ctf_type_encoding(ctfp, base, &e) == 0 && e.cte_bits == 32;
}

/*ARGSUSED*/
static int
pfcheck_csi(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_integer(dnp) &&
	    dt_node_type_size(dnp) <= sizeof(int);
}

/*ARGSUSED*/
static int
pfcheck_fp(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_float(dnp);
}

/*ARGSUSED*/
static int
pfcheck_xint(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return dt_node_is_integer(dnp);
}

/*ARGSUSED*/
static int
pfcheck_dint(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	if (dnp->dn_flags & DT_NF_SIGNED)
		pfd->pfd_flags |= DT_PFCONV_SIGNED;
	else
		pfd->pfd_fmt[strlen(pfd->pfd_fmt) - 1] = 'u';

	return dt_node_is_integer(dnp);
}

/*ARGSUSED*/
static int
pfcheck_xshort(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_id_t type = ctf_type_resolve(ctfp, dnp->dn_type);
	char n[DT_TYPE_NAMELEN];

	return ctf_type_name(ctfp, type, n, sizeof(n)) != NULL && (
	    strcmp(n, "short") == 0 || strcmp(n, "signed short") == 0 ||
	    strcmp(n, "unsigned short") == 0);
}

/*ARGSUSED*/
static int
pfcheck_xlong(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_id_t type = ctf_type_resolve(ctfp, dnp->dn_type);
	char n[DT_TYPE_NAMELEN];

	return ctf_type_name(ctfp, type, n, sizeof(n)) != NULL && (
	    strcmp(n, "long") == 0 || strcmp(n, "signed long") == 0 ||
	    strcmp(n, "unsigned long") == 0);
}

/*ARGSUSED*/
static int
pfcheck_xlonglong(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	ctf_file_t *ctfp = dnp->dn_ctfp;
	ctf_id_t type = dnp->dn_type;
	char n[DT_TYPE_NAMELEN];

	if (ctf_type_name(ctfp, ctf_type_resolve(ctfp, type), n,
	    sizeof(n)) != NULL && (strcmp(n, "long long") == 0 ||
	    strcmp(n, "signed long long") == 0 ||
	    strcmp(n, "unsigned long long") == 0))
		return 1;

	/*
	 * If the type used for %llx or %llX is not an [unsigned] long long, we
	 * also permit it to be a [u]int64_t or any typedef thereof.  We know
	 * that these typedefs are guaranteed to work with %ll[xX] in either
	 * compilation environment even though they alias to "long" in LP64.
	 */
	while (ctf_type_kind(ctfp, type) == CTF_K_TYPEDEF) {
		if (ctf_type_name(ctfp, type, n, sizeof(n)) != NULL &&
		    (strcmp(n, "int64_t") == 0 || strcmp(n, "uint64_t") == 0))
			return 1;

		type = ctf_type_reference(ctfp, type);
	}

	return 0;
}

/*ARGSUSED*/
static int
pfcheck_type(dt_pfargv_t *pfv, dt_pfargd_t *pfd, dt_node_t *dnp)
{
	return ctf_type_compat(dnp->dn_ctfp, ctf_type_resolve(dnp->dn_ctfp,
	    dnp->dn_type), pfd->pfd_conv->pfc_dctfp, pfd->pfd_conv->pfc_dtype);
}

/*ARGSUSED*/
static int
pfprint_sint(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t unormal, uint64_t sig)
{
	int64_t normal = (int64_t)unormal;
	int32_t n = (int32_t)normal;

	switch (size) {
	case sizeof(int8_t):
		return dt_printf(dtp, fp, format,
		    (int32_t)*((int8_t *)addr) / n);
	case sizeof(int16_t):
		return dt_printf(dtp, fp, format,
		    (int32_t)*((int16_t *)addr) / n);
	case sizeof(int32_t):
		return dt_printf(dtp, fp, format,
		    *((int32_t *)addr) / n);
	case sizeof(int64_t):
		return dt_printf(dtp, fp, format,
		    *((int64_t *)addr) / normal);
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}
}

/*ARGSUSED*/
static int
pfprint_uint(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	uint32_t n = (uint32_t)normal;

	switch (size) {
	case sizeof(uint8_t):
		return dt_printf(dtp, fp, format,
		    (uint32_t)*((uint8_t *)addr) / n);
	case sizeof(uint16_t):
		return dt_printf(dtp, fp, format,
		    (uint32_t)*((uint16_t *)addr) / n);
	case sizeof(uint32_t):
		return dt_printf(dtp, fp, format, *((uint32_t *)addr) / n);
	case sizeof(uint64_t):
		return dt_printf(dtp, fp, format, *((uint64_t *)addr) / normal);
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}
}

static int
pfprint_dint(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	if (pfd->pfd_flags & DT_PFCONV_SIGNED)
		return pfprint_sint(dtp, fp, format, pfd, addr, size, normal,
				    sig);
	else
		return pfprint_uint(dtp, fp, format, pfd, addr, size, normal,
				    sig);
}

/*ARGSUSED*/
static int
pfprint_fp(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	   const dt_pfargd_t *pfd, const void *addr, size_t size,
	   uint64_t normal, uint64_t sig)
{
	double n = (double)normal;
	long double ldn = (long double)normal;

	switch (size) {
	case sizeof(float):
		return dt_printf(dtp, fp, format, (double)*((float *)addr) / n);
	case sizeof(double):
		return dt_printf(dtp, fp, format, *((double *)addr) / n);
	case sizeof(long double):
		return dt_printf(dtp, fp, format, *((long double *)addr) / ldn);
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}
}

/*ARGSUSED*/
static int
pfprint_addr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	char *s;
	int n, len = 256;
	uint64_t val;

	switch (size) {
	case sizeof(uint32_t):
		val = *((uint32_t *)addr);
		break;
	case sizeof(uint64_t):
		val = *((uint64_t *)addr);
		break;
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}

	do {
		n = len;
		s = alloca(n);
	} while ((len = dtrace_addr2str(dtp, val, s, n)) > n);

	return dt_printf(dtp, fp, format, s);
}

/*ARGSUSED*/
static int
pfprint_mod(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	    const dt_pfargd_t *pfd, const void *addr, size_t size,
	    uint64_t normal, uint64_t sig)
{
	return dt_print_mod(dtp, fp, format, (caddr_t)addr);
}

/*ARGSUSED*/
static int
pfprint_umod(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	return dt_print_umod(dtp, fp, format, (caddr_t)addr);
}

/*ARGSUSED*/
static int
pfprint_uaddr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	      const dt_pfargd_t *pfd, const void *addr, size_t size,
	      uint64_t normal, uint64_t sig)
{
	char *s;
	int n, len = 256;
	uint64_t val, tgid = 0;

	dt_ident_t *idp = dt_idhash_lookup(dtp->dt_macros, "target");

	switch (size) {
	case sizeof(uint32_t):
		val = (u_longlong_t)*((uint32_t *)addr);
		break;
	case sizeof(uint64_t):
		val = (u_longlong_t)*((uint64_t *)addr);
		break;
	case sizeof(uint64_t) * 3:
		tgid = ((uint64_t *)(uintptr_t)addr)[1];
		val = ((uint64_t *)(uintptr_t)addr)[2];
		break;
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}

	if (tgid == 0 && dtp->dt_vector == NULL && idp != NULL)
		tgid = idp->di_id; /* pretend it is a pid */

	do {
		n = len;
		s = alloca(n);
	} while ((len = dtrace_uaddr2str(dtp, tgid, val, s, n)) > n);

	return dt_printf(dtp, fp, format, s);
}

/*ARGSUSED*/
static int
pfprint_stack(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	      const dt_pfargd_t *pfd, const void *vaddr, size_t size,
	      uint64_t normal, uint64_t sig)
{
	int width;
	dtrace_optval_t saved = dtp->dt_options[DTRACEOPT_STACKINDENT];
	const dtrace_recdesc_t *rec = pfd->pfd_rec;
	caddr_t addr = (caddr_t)vaddr;
	int err = 0;

	/*
	 * We have stashed the value of the STACKINDENT option, and we will
	 * now override it for the purposes of formatting the stack.  If the
	 * field has been specified as left-aligned (i.e. (%-#), we set the
	 * indentation to be the width.  This is a slightly odd semantic, but
	 * it's useful functionality -- and it's slightly odd to begin with to
	 * be using a single format specifier to be formatting multiple lines
	 * of text...
	 */
	if (pfd->pfd_dynwidth < 0) {
		assert(pfd->pfd_flags & DT_PFCONV_DYNWIDTH);
		width = -pfd->pfd_dynwidth;
	} else if (pfd->pfd_flags & DT_PFCONV_LEFT) {
		width = pfd->pfd_dynwidth ? pfd->pfd_dynwidth : pfd->pfd_width;
	} else {
		width = 0;
	}

	dtp->dt_options[DTRACEOPT_STACKINDENT] = width;

	switch (rec->dtrd_action) {
	case DTRACEACT_USTACK:
	case DTRACEACT_JSTACK:
		err = dt_print_ustack(dtp, fp, format, addr, rec->dtrd_arg);
		break;

	case DTRACEACT_STACK:
		err = dt_print_stack(dtp, fp, format, addr, rec->dtrd_arg,
		    rec->dtrd_size / rec->dtrd_arg);
		break;

	default:
		assert(0);
	}

	dtp->dt_options[DTRACEOPT_STACKINDENT] = saved;

	return err;
}

/*ARGSUSED*/
static int
pfprint_time(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	char src[32], buf[32], *dst = buf;
	hrtime_t time = *((uint64_t *)addr);
	time_t sec = (time_t)(time / NANOSEC);
	int i;

	/*
	 * ctime(3C) returns a string of the form "Dec  3 17:20:00 1973\n\0".
	 * Below, we turn this into the canonical adb/mdb /[yY] format,
	 * "1973 Dec  3 17:20:00".
	 */
	ctime_r(&sec, src);

	/*
	 * Place the 4-digit year at the head of the string...
	 */
	for (i = 20; i < 24; i++)
		*dst++ = src[i];

	/*
	 * ...and follow it with the remainder (month, day, hh:mm:ss).
	 */
	for (i = 3; i < 19; i++)
		*dst++ = src[i];

	*dst = '\0';
	return dt_printf(dtp, fp, format, buf);
}

/*
 * This prints the time in RFC 822 standard form.  This is useful for emitting
 * notions of time that are consumed by standard tools (e.g., as part of an
 * RSS feed).
 */
/*ARGSUSED*/
static int
pfprint_time822(dtrace_hdl_t *dtp, FILE *fp, const char *format,
		const dt_pfargd_t *pfd, const void *addr, size_t size,
		uint64_t normal, uint64_t sig)
{
	hrtime_t time = *((uint64_t *)addr);
	time_t sec = (time_t)(time / NANOSEC);
	struct tm tm;
	char buf[64];

	localtime_r(&sec, &tm);
	strftime(buf, sizeof(buf), "%a, %d %b %G %T %Z", &tm);
	return dt_printf(dtp, fp, format, buf);
}

/*ARGSUSED*/
static int
pfprint_cstr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	char *s = alloca(size + 1);

	memcpy(s, addr, size);
	s[size] = '\0';
	return dt_printf(dtp, fp, format, s);
}

/*ARGSUSED*/
static int
pfprint_wstr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	wchar_t *ws = alloca(size + sizeof(wchar_t));

	memcpy(ws, addr, size);
	ws[size / sizeof(wchar_t)] = L'\0';
	return dt_printf(dtp, fp, format, ws);
}

/*ARGSUSED*/
static int
pfprint_estr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	char *s;
	int n;

	if ((s = strchr2esc(addr, size)) == NULL)
		return dt_set_errno(dtp, EDT_NOMEM);

	n = dt_printf(dtp, fp, format, s);
	free(s);
	return n;
}

static int
pfprint_echr(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	     const dt_pfargd_t *pfd, const void *addr, size_t size,
	     uint64_t normal, uint64_t sig)
{
	char c;

	switch (size) {
	case sizeof(int8_t):
		c = *(int8_t *)addr;
		break;
	case sizeof(int16_t):
		c = *(int16_t *)addr;
		break;
	case sizeof(int32_t):
		c = *(int32_t *)addr;
		break;
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}

	return pfprint_estr(dtp, fp, format, pfd, &c, 1, normal, sig);
}

/*ARGSUSED*/
static int
pfprint_pct(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	    const dt_pfargd_t *pfd, const void *addr, size_t size,
	    uint64_t normal, uint64_t sig)
{
	return dt_printf(dtp, fp, "%%");
}

static const char pfproto_xint[] = "char, short, int, long, or long long";
static const char pfproto_csi[] = "char, short, or int";
static const char pfproto_fp[] = "float, double, or long double";
static const char pfproto_addr[] = "pointer or integer";
static const char pfproto_uaddr[] =
	"pointer or integer (with -p/-c) or _usymaddr (without -p/-c)";
static const char pfproto_cstr[] = "char [] or string (or use stringof)";
static const char pfproto_wstr[] = "wchar_t []";

/*
 * Printf format conversion dictionary.  This table should match the set of
 * conversions offered by printf(3C), as well as some additional extensions.
 * The second parameter is an ASCII string which is either an actual type
 * name we should look up (if pfcheck_type is specified), or just a descriptive
 * string of the types expected for use in error messages.
 */
static const dt_pfconv_t _dtrace_conversions[] = {
{ "a", "s", pfproto_addr, pfcheck_kaddr, pfprint_addr },
{ "A", "s", pfproto_uaddr, pfcheck_uaddr, pfprint_uaddr },
{ "c", "c", pfproto_csi, pfcheck_csi, pfprint_sint },
{ "C", "s", pfproto_csi, pfcheck_csi, pfprint_echr },
{ "d", "d", pfproto_xint, pfcheck_dint, pfprint_dint },
{ "e", "e", pfproto_fp, pfcheck_fp, pfprint_fp },
{ "E", "E", pfproto_fp, pfcheck_fp, pfprint_fp },
{ "f", "f", pfproto_fp, pfcheck_fp, pfprint_fp },
{ "g", "g", pfproto_fp, pfcheck_fp, pfprint_fp },
{ "G", "G", pfproto_fp, pfcheck_fp, pfprint_fp },
{ "hd", "d", "short", pfcheck_type, pfprint_sint },
{ "hi", "i", "short", pfcheck_type, pfprint_sint },
{ "ho", "o", "unsigned short", pfcheck_type, pfprint_uint },
{ "hu", "u", "unsigned short", pfcheck_type, pfprint_uint },
{ "hx", "x", "short", pfcheck_xshort, pfprint_uint },
{ "hX", "X", "short", pfcheck_xshort, pfprint_uint },
{ "i", "i", pfproto_xint, pfcheck_dint, pfprint_dint },
{ "k", "s", "stack", pfcheck_stack, pfprint_stack },
{ "lc", "lc", "int", pfcheck_type, pfprint_sint }, /* a.k.a. wint_t */
{ "ld",	"d", "long", pfcheck_type, pfprint_sint },
{ "li",	"i", "long", pfcheck_type, pfprint_sint },
{ "lo",	"o", "unsigned long", pfcheck_type, pfprint_uint },
{ "lu", "u", "unsigned long", pfcheck_type, pfprint_uint },
{ "ls",	"ls", pfproto_wstr, pfcheck_wstr, pfprint_wstr },
{ "lx",	"x", "long", pfcheck_xlong, pfprint_uint },
{ "lX",	"X", "long", pfcheck_xlong, pfprint_uint },
{ "lld", "d", "long long", pfcheck_type, pfprint_sint },
{ "lli", "i", "long long", pfcheck_type, pfprint_sint },
{ "llo", "o", "unsigned long long", pfcheck_type, pfprint_uint },
{ "llu", "u", "unsigned long long", pfcheck_type, pfprint_uint },
{ "llx", "x", "long long", pfcheck_xlonglong, pfprint_uint },
{ "llX", "X", "long long", pfcheck_xlonglong, pfprint_uint },
{ "Le",	"e", "long double", pfcheck_type, pfprint_fp },
{ "LE",	"E", "long double", pfcheck_type, pfprint_fp },
{ "Lf",	"f", "long double", pfcheck_type, pfprint_fp },
{ "Lg",	"g", "long double", pfcheck_type, pfprint_fp },
{ "LG",	"G", "long double", pfcheck_type, pfprint_fp },
{ "o", "o", pfproto_xint, pfcheck_xint, pfprint_uint },
{ "p", "x", pfproto_addr, pfcheck_addr, pfprint_uint },
{ "s", "s", "char [] or string (or use stringof)", pfcheck_str, pfprint_cstr },
{ "S", "s", pfproto_cstr, pfcheck_str, pfprint_estr },
{ "T", "s", "int64_t", pfcheck_time, pfprint_time822 },
{ "u", "u", pfproto_xint, pfcheck_xint, pfprint_uint },
{ "wc",	"wc", "int", pfcheck_type, pfprint_sint }, /* a.k.a. wchar_t */
{ "ws", "ws", pfproto_wstr, pfcheck_wstr, pfprint_wstr },
{ "x", "x", pfproto_xint, pfcheck_xint, pfprint_uint },
{ "X", "X", pfproto_xint, pfcheck_xint, pfprint_uint },
{ "Y", "s", "int64_t", pfcheck_time, pfprint_time },
{ "%", "%", "void", pfcheck_type, pfprint_pct },
{ NULL, NULL, NULL, NULL, NULL }
};

int
dt_pfdict_create(dtrace_hdl_t *dtp)
{
	uint_t n = _dtrace_strbuckets;
	const dt_pfconv_t *pfd;
	dt_pfdict_t *pdi;

	if ((pdi = malloc(sizeof(dt_pfdict_t))) == NULL ||
	    (pdi->pdi_buckets = malloc(sizeof(dt_pfconv_t *) * n)) == NULL) {
		free(pdi);
		return dt_set_errno(dtp, EDT_NOMEM);
	}

	dtp->dt_pfdict = pdi;
	memset(pdi->pdi_buckets, 0, sizeof(dt_pfconv_t *) * n);
	pdi->pdi_nbuckets = n;

	for (pfd = _dtrace_conversions; pfd->pfc_name != NULL; pfd++) {
		dtrace_typeinfo_t dtt;
		dt_pfconv_t *pfc;
		uint_t h;

		if ((pfc = malloc(sizeof(dt_pfconv_t))) == NULL) {
			dt_pfdict_destroy(dtp);
			return dt_set_errno(dtp, EDT_NOMEM);
		}

		memcpy(pfc, pfd, sizeof(dt_pfconv_t));
		h = str2hval(pfc->pfc_name, 0) % n;
		pfc->pfc_next = pdi->pdi_buckets[h];
		pdi->pdi_buckets[h] = pfc;

		dtt.dtt_ctfp = NULL;
		dtt.dtt_type = CTF_ERR;

		/*
		 * The "D" container or its parent must contain a definition of
		 * any type referenced by a printf conversion.  If none can be
		 * found, we fail to initialize the printf dictionary.
		 */
		if (pfc->pfc_check == &pfcheck_type && dtrace_lookup_by_type(
		    dtp, DTRACE_OBJ_DDEFS, pfc->pfc_tstr, &dtt) != 0) {
			dt_pfdict_destroy(dtp);
			return dt_set_errno(dtp, EDT_NOCONV);
		}

		pfc->pfc_dctfp = dtt.dtt_ctfp;
		pfc->pfc_dtype = dtt.dtt_type;

		/*
		 * The "C" container may contain an alternate definition of an
		 * explicit conversion type.  If it does, use it; otherwise
		 * just set pfc_ctype to pfc_dtype so it is always valid.
		 */
		if (pfc->pfc_check == &pfcheck_type && dtrace_lookup_by_type(
		    dtp, DTRACE_OBJ_CDEFS, pfc->pfc_tstr, &dtt) == 0) {
			pfc->pfc_cctfp = dtt.dtt_ctfp;
			pfc->pfc_ctype = dtt.dtt_type;
		} else {
			pfc->pfc_cctfp = pfc->pfc_dctfp;
			pfc->pfc_ctype = pfc->pfc_dtype;
		}

		if (pfc->pfc_check == NULL || pfc->pfc_print == NULL ||
		    pfc->pfc_ofmt == NULL || pfc->pfc_tstr == NULL) {
			dt_pfdict_destroy(dtp);
			return dt_set_errno(dtp, EDT_BADCONV);
		}

		dt_dprintf("loaded printf conversion %%%s\n", pfc->pfc_name);
	}

	return 0;
}

void
dt_pfdict_destroy(dtrace_hdl_t *dtp)
{
	dt_pfdict_t *pdi = dtp->dt_pfdict;
	dt_pfconv_t *pfc, *nfc;
	uint_t i;

	if (pdi == NULL)
		return;

	for (i = 0; i < pdi->pdi_nbuckets; i++) {
		for (pfc = pdi->pdi_buckets[i]; pfc != NULL; pfc = nfc) {
			nfc = pfc->pfc_next;
			free(pfc);
		}
	}

	free(pdi->pdi_buckets);
	free(pdi);
	dtp->dt_pfdict = NULL;
}

static const dt_pfconv_t *
dt_pfdict_lookup(dtrace_hdl_t *dtp, const char *name)
{
	dt_pfdict_t *pdi = dtp->dt_pfdict;
	uint_t h = str2hval(name, 0) % pdi->pdi_nbuckets;
	const dt_pfconv_t *pfc;

	for (pfc = pdi->pdi_buckets[h]; pfc != NULL; pfc = pfc->pfc_next) {
		if (strcmp(pfc->pfc_name, name) == 0)
			break;
	}

	return pfc;
}

static dt_pfargv_t *
dt_printf_error(dtrace_hdl_t *dtp, int err)
{
	if (yypcb != NULL)
		longjmp(yypcb->pcb_jmpbuf, err);

	dt_set_errno(dtp, err);
	return NULL;
}

dt_pfargv_t *
dt_printf_create(dtrace_hdl_t *dtp, const char *s)
{
	dt_pfargd_t *pfd, *nfd = NULL;
	dt_pfargv_t *pfv;
	const char *p, *q;
	char *format;

	if ((pfv = malloc(sizeof(dt_pfargv_t))) == NULL ||
	    (format = strdup(s)) == NULL) {
		free(pfv);
		return dt_printf_error(dtp, EDT_NOMEM);
	}

	pfv->pfv_format = format;
	pfv->pfv_argv = NULL;
	pfv->pfv_argc = 0;
	pfv->pfv_flags = 0;
	pfv->pfv_dtp = dtp;

	for (q = format; (p = strchr(q, '%')) != NULL; q = *p ? p + 1 : p) {
		uint_t namelen = 0;
		int digits = 0;
		int dot = 0;

		char name[8];
		char c;
		int n;

		if ((pfd = malloc(sizeof(dt_pfargd_t))) == NULL) {
			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_NOMEM);
		}

		if (pfv->pfv_argv != NULL)
			nfd->pfd_next = pfd;
		else
			pfv->pfv_argv = pfd;

		memset(pfd, 0, sizeof(dt_pfargd_t));
		pfv->pfv_argc++;
		nfd = pfd;

		if (p > q) {
			pfd->pfd_preflen = (size_t)(p - q);
			pfd->pfd_prefix = q;
		}

		fmt_switch:
		switch (c = *++p) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (dot == 0 && digits == 0 && c == '0') {
				pfd->pfd_flags |= DT_PFCONV_ZPAD;
				pfd->pfd_flags &= ~DT_PFCONV_LEFT;
				goto fmt_switch;
			}

			for (n = 0; isdigit(c); c = *++p)
				n = n * 10 + c - '0';

			if (dot)
				pfd->pfd_prec = n;
			else
				pfd->pfd_width = n;

			p--;
			digits++;
			goto fmt_switch;

		case '#':
			pfd->pfd_flags |= DT_PFCONV_ALT;
			goto fmt_switch;

		case '*':
			n = dot ? DT_PFCONV_DYNPREC : DT_PFCONV_DYNWIDTH;

			if (pfd->pfd_flags & n) {
				yywarn("format conversion #%u has more than "
				       "one '*' specified for the output %s\n",
				       pfv->pfv_argc,
				       dot ? "precision" : "width");

				dt_printf_destroy(pfv);
				return dt_printf_error(dtp, EDT_COMPILER);
			}

			pfd->pfd_flags |= n;
			goto fmt_switch;

		case '+':
			pfd->pfd_flags |= DT_PFCONV_SPOS;
			goto fmt_switch;

		case '-':
			pfd->pfd_flags |= DT_PFCONV_LEFT;
			pfd->pfd_flags &= ~DT_PFCONV_ZPAD;
			goto fmt_switch;

		case '.':
			if (dot++ != 0) {
				yywarn("format conversion #%u has more than "
				    "one '.' specified\n", pfv->pfv_argc);

				dt_printf_destroy(pfv);
				return dt_printf_error(dtp, EDT_COMPILER);
			}
			digits = 0;
			goto fmt_switch;

		case '?':
			if (dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64)
				pfd->pfd_width = 16;
			else
				pfd->pfd_width = 8;
			goto fmt_switch;

		case '@':
			pfd->pfd_flags |= DT_PFCONV_AGG;
			goto fmt_switch;

		case '\'':
			pfd->pfd_flags |= DT_PFCONV_GROUP;
			goto fmt_switch;

		case ' ':
			pfd->pfd_flags |= DT_PFCONV_SPACE;
			goto fmt_switch;

		case '$':
			yywarn("format conversion #%u uses unsupported "
			    "positional format (%%n$)\n", pfv->pfv_argc);

			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_COMPILER);

		case '%':
			if (p[-1] == '%')
				goto default_lbl; /* if %% then use "%" conv */

			yywarn("format conversion #%u cannot be combined "
			    "with other format flags: %%%%\n", pfv->pfv_argc);

			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_COMPILER);

		case '\0':
			yywarn("format conversion #%u name expected before "
			    "end of format string\n", pfv->pfv_argc);

			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_COMPILER);

		case 'h':
		case 'l':
		case 'L':
		case 'w':
			if (namelen < sizeof(name) - 2)
				name[namelen++] = c;
			goto fmt_switch;

		default_lbl:
		default:
			name[namelen++] = c;
			name[namelen] = '\0';
		}

		pfd->pfd_conv = dt_pfdict_lookup(dtp, name);

		if (pfd->pfd_conv == NULL) {
			yywarn("format conversion #%u is undefined: %%%s\n",
			    pfv->pfv_argc, name);
			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_COMPILER);
		}
	}

	if (*q != '\0' || *format == '\0') {
		if ((pfd = malloc(sizeof(dt_pfargd_t))) == NULL) {
			dt_printf_destroy(pfv);
			return dt_printf_error(dtp, EDT_NOMEM);
		}

		if (pfv->pfv_argv != NULL)
			nfd->pfd_next = pfd;
		else
			pfv->pfv_argv = pfd;

		memset(pfd, 0, sizeof(dt_pfargd_t));
		pfv->pfv_argc++;

		pfd->pfd_prefix = q;
		pfd->pfd_preflen = strlen(q);
	}

	return pfv;
}

void
dt_printf_destroy(dt_pfargv_t *pfv)
{
	dt_pfargd_t *pfd, *nfd;

	for (pfd = pfv->pfv_argv; pfd != NULL; pfd = nfd) {
		nfd = pfd->pfd_next;
		free(pfd);
	}

	free(pfv->pfv_format);
	free(pfv);
}

void
dt_printf_validate(dt_pfargv_t *pfv, uint_t flags,
    dt_ident_t *idp, int foff, dtrace_actkind_t kind, dt_node_t *dnp)
{
	dt_pfargd_t *pfd = pfv->pfv_argv;
	const char *func = idp->di_name;

	char n[DT_TYPE_NAMELEN];
	dtrace_typeinfo_t dtt;
	const char *aggtype;
	dt_node_t aggnode;
	int i, j;

	if (pfv->pfv_format[0] == '\0') {
		xyerror(D_PRINTF_FMT_EMPTY,
		    "%s( ) format string is empty\n", func);
	}

	pfv->pfv_flags = flags;

	/*
	 * We fake up a parse node representing the type that can be used with
	 * an aggregation result conversion, which -- for all but count() --
	 * is a signed quantity.
	 */
	if (kind != DTRACEAGG_COUNT)
		aggtype = "int64_t";
	else
		aggtype = "uint64_t";

	if (dt_type_lookup(aggtype, &dtt) != 0)
		xyerror(D_TYPE_ERR, "failed to lookup agg type %s\n", aggtype);

	memset(&aggnode, 0, sizeof(aggnode));
	dt_node_type_assign(&aggnode, dtt.dtt_ctfp, dtt.dtt_type);

	for (i = 0, j = 0; i < pfv->pfv_argc; i++, pfd = pfd->pfd_next) {
		const dt_pfconv_t *pfc = pfd->pfd_conv;
		const char *dyns[2];
		int dync = 0;

		char vname[64];
		dt_node_t *vnp;

		if (pfc == NULL)
			continue; /* no checking if argd is just a prefix */

		if (pfc->pfc_print == &pfprint_pct) {
			strcat(pfd->pfd_fmt, pfc->pfc_ofmt);
			continue;
		}

		if (pfd->pfd_flags & DT_PFCONV_DYNPREC)
			dyns[dync++] = ".*";
		if (pfd->pfd_flags & DT_PFCONV_DYNWIDTH)
			dyns[dync++] = "*";

		for (; dync != 0; dync--) {
			if (dnp == NULL) {
				xyerror(D_PRINTF_DYN_PROTO,
				    "%s( ) prototype mismatch: conversion "
				    "#%d (%%%s) is missing a corresponding "
				    "\"%s\" argument\n", func, i + 1,
				    pfc->pfc_name, dyns[dync - 1]);
			}

			if (dt_node_is_integer(dnp) == 0) {
				xyerror(D_PRINTF_DYN_TYPE,
				    "%s( ) argument #%d is incompatible "
				    "with conversion #%d prototype:\n"
				    "\tconversion: %% %s %s\n"
				    "\t prototype: int\n\t  argument: %s\n",
				    func, j + foff + 1, i + 1,
				    dyns[dync - 1], pfc->pfc_name,
				    dt_node_type_name(dnp, n, sizeof(n)));
			}

			dnp = dnp->dn_list;
			j++;
		}

		/*
		 * If this conversion is consuming the aggregation data, set
		 * the value node pointer (vnp) to a fake node based on the
		 * aggregating function result type.  Otherwise assign vnp to
		 * the next parse node in the argument list, if there is one.
		 */
		if (pfd->pfd_flags & DT_PFCONV_AGG) {
			if (!(flags & DT_PRINTF_AGGREGATION))
				xyerror(D_PRINTF_AGG_CONV,
				    "%%@ conversion requires an aggregation"
				    " and is not for use with %s( )\n", func);
			strlcpy(vname, "aggregating action", sizeof(vname));
			vnp = &aggnode;
		} else if (dnp == NULL) {
			xyerror(D_PRINTF_ARG_PROTO,
			    "%s( ) prototype mismatch: conversion #%d (%%"
			    "%s) is missing a corresponding value argument\n",
			    func, i + 1, pfc->pfc_name);
		} else {
			snprintf(vname, sizeof(vname),
			    "argument #%d", j + foff + 1);
			vnp = dnp;
			dnp = dnp->dn_list;
			j++;
		}

		/*
		 * Fill in the proposed final format string by prepending any
		 * size-related prefixes to the pfconv's format string.  The
		 * pfc_check() function below may optionally modify the format
		 * as part of validating the type of the input argument.
		 */
		if (pfc->pfc_print == &pfprint_sint ||
		    pfc->pfc_print == &pfprint_uint ||
		    pfc->pfc_print == &pfprint_dint) {
			if (dt_node_type_size(vnp) == sizeof(uint64_t))
				strcpy(pfd->pfd_fmt, "ll");
		} else if (pfc->pfc_print == &pfprint_fp) {
			if (dt_node_type_size(vnp) == sizeof(long double))
				strcpy(pfd->pfd_fmt, "L");
		}

		strcat(pfd->pfd_fmt, pfc->pfc_ofmt);

		/*
		 * Validate the format conversion against the value node type.
		 * If the conversion is good, create the descriptor format
		 * string by concatenating together any required printf(3C)
		 * size prefixes with the conversion's native format string.
		 */
		if (pfc->pfc_check(pfv, pfd, vnp) == 0)
			xyerror(D_PRINTF_ARG_TYPE,
			    "%s( ) %s is incompatible with "
			    "conversion #%d prototype:\n\tconversion: %%%s\n"
			    "\t prototype: %s\n\t  argument: %s\n", func,
			    vname, i + 1, pfc->pfc_name, pfc->pfc_tstr,
			    dt_node_type_name(vnp, n, sizeof(n)));
	}

	if ((flags & DT_PRINTF_EXACTLEN) && dnp != NULL)
		xyerror(D_PRINTF_ARG_EXTRA,
		    "%s( ) prototype mismatch: only %d arguments "
		    "required by this format string\n", func, j);
}

void
dt_printa_validate(dt_node_t *lhs, dt_node_t *rhs)
{
	dt_ident_t *lid, *rid;
	dt_node_t *lproto, *rproto;
	int largc, rargc, argn;
	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	assert(lhs->dn_kind == DT_NODE_AGG);
	assert(rhs->dn_kind == DT_NODE_AGG);

	lid = lhs->dn_ident;
	rid = rhs->dn_ident;

	lproto = ((dt_idsig_t *)lid->di_data)->dis_args;
	rproto = ((dt_idsig_t *)rid->di_data)->dis_args;

	/*
	 * First, get an argument count on each side.  These must match.
	 */
	for (largc = 0; lproto != NULL; lproto = lproto->dn_list)
		largc++;

	for (rargc = 0; rproto != NULL; rproto = rproto->dn_list)
		rargc++;

	if (largc != rargc) {
		xyerror(D_PRINTA_AGGKEY, "printa( ): @%s and @%s do not have "
		    "matching key signatures: @%s has %d key%s, @%s has %d "
		    "key%s", lid->di_name, rid->di_name,
		    lid->di_name, largc, largc == 1 ? "" : "s",
		    rid->di_name, rargc, rargc == 1 ? "" : "s");
	}

	/*
	 * Now iterate over the keys to verify that each type matches.
	 */
	lproto = ((dt_idsig_t *)lid->di_data)->dis_args;
	rproto = ((dt_idsig_t *)rid->di_data)->dis_args;

	for (argn = 1; lproto != NULL; argn++, lproto = lproto->dn_list,
	    rproto = rproto->dn_list) {
		assert(rproto != NULL);

		if (dt_node_is_argcompat(lproto, rproto))
			continue;

		xyerror(D_PRINTA_AGGPROTO, "printa( ): @%s[ ] key #%d is "
		    "incompatible with @%s:\n%9s key #%d: %s\n"
		    "%9s key #%d: %s\n",
		    rid->di_name, argn, lid->di_name, lid->di_name, argn,
		    dt_node_type_name(lproto, n1, sizeof(n1)), rid->di_name,
		    argn, dt_node_type_name(rproto, n2, sizeof(n2)));
	}
}

static int
dt_printf_getint(dtrace_hdl_t *dtp, const dtrace_recdesc_t *recp,
    uint_t nrecs, const void *buf, size_t len, int *ip)
{
	uintptr_t addr;

	if (nrecs == 0)
		return dt_set_errno(dtp, EDT_DMISMATCH);

	addr = (uintptr_t)buf + recp->dtrd_offset;

	if (addr + recp->dtrd_size > (uintptr_t)buf + len)
		return dt_set_errno(dtp, EDT_DOFFSET);

	if (addr & (recp->dtrd_alignment - 1))
		return dt_set_errno(dtp, EDT_DALIGN);

	switch (recp->dtrd_size) {
	case sizeof(int8_t):
		*ip = (int)*((int8_t *)addr);
		break;
	case sizeof(int16_t):
		*ip = (int)*((int16_t *)addr);
		break;
	case sizeof(int32_t):
		*ip = (int)*((int32_t *)addr);
		break;
	case sizeof(int64_t):
		*ip = (int)*((int64_t *)addr);
		break;
	default:
		return dt_set_errno(dtp, EDT_DMISMATCH);
	}

	return 0;
}

/*ARGSUSED*/
static int
pfprint_average(dtrace_hdl_t *dtp, FILE *fp, const char *format,
		const dt_pfargd_t *pfd, const void *addr, size_t size,
		uint64_t normal, uint64_t sig)
{
	const uint64_t *data = addr;

	if (size != sizeof(uint64_t) * 2)
		return dt_set_errno(dtp, EDT_DMISMATCH);

	return dt_printf(dtp, fp, format,
			 data[0] ? data[1] / normal / data[0] : 0);
}

/*ARGSUSED*/
static int
pfprint_stddev(dtrace_hdl_t *dtp, FILE *fp, const char *format,
	       const dt_pfargd_t *pfd, const void *addr, size_t size,
	       uint64_t normal, uint64_t sig)
{
	const uint64_t *data = addr;

	if (size != sizeof(uint64_t) * 4)
		return dt_set_errno(dtp, EDT_DMISMATCH);

	return dt_printf(dtp, fp, format, dt_stddev((uint64_t *)data, normal));
}

/*ARGSUSED*/
static int
pfprint_quantize(dtrace_hdl_t *dtp, FILE *fp, const char *format,
		 const dt_pfargd_t *pfd, const void *addr, size_t size,
		 uint64_t normal, uint64_t sig)
{
	return dt_print_quantize(dtp, fp, addr, size, normal);
}

/*ARGSUSED*/
static int
pfprint_lquantize(dtrace_hdl_t *dtp, FILE *fp, const char *format,
		  const dt_pfargd_t *pfd, const void *addr, size_t size,
		  uint64_t normal, uint64_t sig)
{
	return dt_print_lquantize(dtp, fp, addr, size, normal, sig);
}

static int
pfprint_llquantize(dtrace_hdl_t *dtp, FILE *fp, const char *format,
		   const dt_pfargd_t *pfd, const void *addr, size_t size,
		   uint64_t normal, uint64_t sig)
{
	return dt_print_llquantize(dtp, fp, addr, size, normal, sig);
}

static int
dt_printf_format(dtrace_hdl_t *dtp, FILE *fp, const dt_pfargv_t *pfv,
    const dtrace_recdesc_t *recs, uint_t nrecs, const void *buf,
    size_t len, const dtrace_aggdata_t **aggsdata, int naggvars)
{
	dt_pfargd_t *pfd = pfv->pfv_argv;
	const dtrace_recdesc_t *recp = recs;
	const dtrace_aggdata_t *aggdata = NULL; /* gcc -Wmaybe-uninitialized */
	dtrace_aggdesc_t *agg;
	caddr_t lim = (caddr_t)buf + len, limit;
	char format[64] = "%";
	int i, aggrec = 0, curagg = -1;
	uint64_t normal, sig;

	/*
	 * If we are formatting an aggregation, set 'aggrec' to the index of
	 * the final record description (the aggregation result) so we can use
	 * this record index with any conversion where DT_PFCONV_AGG is set.
	 * (The actual aggregation used will vary as we increment through the
	 * aggregation variables that we have been passed.)  Finally, we
	 * decrement nrecs to prevent this record from being used with any
	 * other conversion.
	 */
	if (pfv->pfv_flags & DT_PRINTF_AGGREGATION) {
		assert(aggsdata != NULL);
		assert(naggvars > 0);

		if (nrecs == 0)
			return dt_set_errno(dtp, EDT_DMISMATCH);

		curagg = naggvars > 1 ? 1 : 0;
		aggdata = aggsdata[0];
		aggrec = aggdata->dtada_desc->dtagd_nrecs - 1;
		nrecs--;
	}

	for (i = 0; i < pfv->pfv_argc; i++, pfd = pfd->pfd_next) {
		const dt_pfconv_t *pfc = pfd->pfd_conv;
		int width = pfd->pfd_width;
		int prec = pfd->pfd_prec;
		int rval;

		char *f = format + 1; /* skip initial '%' */
		const dtrace_recdesc_t *rec;
		dt_pfprint_f *func;
		caddr_t addr;
		size_t size;
		uint32_t flags = 0;

		if (pfd->pfd_preflen != 0) {
			char *tmp = alloca(pfd->pfd_preflen + 1);

			memcpy(tmp, pfd->pfd_prefix, pfd->pfd_preflen);
			tmp[pfd->pfd_preflen] = '\0';

			if ((rval = dt_printf(dtp, fp, tmp)) < 0)
				return rval;

			if (pfv->pfv_flags & DT_PRINTF_AGGREGATION) {
				/*
				 * For printa(), we flush the buffer after each
				 * prefix, setting the flags to indicate that
				 * this is part of the printa() format string.
				 */
				flags = DTRACE_BUFDATA_AGGFORMAT;

				if (pfc == NULL && i == pfv->pfv_argc - 1)
					flags |= DTRACE_BUFDATA_AGGLAST;

				if (dt_buffered_flush(dtp, NULL, NULL,
				    aggdata, flags) < 0)
					return -1;
			}
		}

		if (pfc == NULL) {
			if (pfv->pfv_argc == 1)
				return nrecs != 0;
			continue;
		}

		/*
		 * If the conversion is %%, just invoke the print callback
		 * with no data record and continue; it consumes no record.
		 */
		if (pfc->pfc_print == &pfprint_pct) {
			if (pfc->pfc_print(dtp, fp, NULL, pfd, NULL, 0, 1, 0) >= 0)
				continue;
			return -1;		/* errno is set for us */
		}

		if (pfd->pfd_flags & DT_PFCONV_DYNWIDTH) {
			if (dt_printf_getint(dtp, recp++, nrecs--, buf,
					     len, &width) == -1)
				return -1;	/* errno is set for us */
			pfd->pfd_dynwidth = width;
		} else
			pfd->pfd_dynwidth = 0;

		if ((pfd->pfd_flags & DT_PFCONV_DYNPREC) &&
		    dt_printf_getint(dtp, recp++, nrecs--, buf, len, &prec) == -1)
			return -1;		/* errno is set for us */

		if (pfd->pfd_flags & DT_PFCONV_AGG) {
			/*
			 * This should be impossible -- the compiler shouldn't
			 * create a DT_PFCONV_AGG conversion without an
			 * aggregation present.  Still, we'd rather fail
			 * gracefully than blow up...
			 */
			if (aggsdata == NULL)
				return dt_set_errno(dtp, EDT_DMISMATCH);

			aggdata = aggsdata[curagg];
			agg = aggdata->dtada_desc;

			/*
			 * We increment the current aggregation variable, but
			 * not beyond the number of aggregation variables that
			 * we're printing. This has the (desired) effect that
			 * DT_PFCONV_AGG conversions beyond the number of
			 * aggregation variables (re-)convert the aggregation
			 * value of the last aggregation variable.
			 */
			if (curagg < naggvars - 1)
				curagg++;

			rec = &agg->dtagd_recs[aggrec];
			addr = aggdata->dtada_data;
			limit = addr + aggdata->dtada_size;
			normal = aggdata->dtada_normal;
			size = agg->dtagd_size;
			sig = agg->dtagd_sig;
			flags = DTRACE_BUFDATA_AGGVAL;
		} else {
			if (nrecs == 0)
				return dt_set_errno(dtp, EDT_DMISMATCH);

			if (pfv->pfv_flags & DT_PRINTF_AGGREGATION) {
				/*
				 * When printing aggregation keys, we always
				 * set the aggdata to be the representative
				 * (zeroth) aggregation.  The aggdata isn't
				 * actually used here in this case, but it is
				 * passed to the buffer handler and must
				 * therefore still be correct.
				 */
				aggdata = aggsdata[0];
				flags = DTRACE_BUFDATA_AGGKEY;
			}

			rec = recp++;
			nrecs--;
			addr = (caddr_t)buf + rec->dtrd_offset;
			limit = lim;
			normal = 1;
			size = rec->dtrd_size;
			sig = 0;
		}

		if (addr + size > limit) {
			dt_dprintf("bad size: addr=%p size=0x%x lim=%p\n",
			    (void *)addr, rec->dtrd_size, (void *)lim);
			return dt_set_errno(dtp, EDT_DOFFSET);
		}

		if (rec->dtrd_alignment != 0 &&
		    ((uintptr_t)addr & (rec->dtrd_alignment - 1)) != 0) {
			dt_dprintf("bad align: addr=%p size=0x%x align=0x%x\n",
			    (void *)addr, rec->dtrd_size, rec->dtrd_alignment);
			return dt_set_errno(dtp, EDT_DALIGN);
		}

		switch (rec->dtrd_action) {
		case DT_AGG_AVG:
			func = pfprint_average;
			break;
		case DT_AGG_STDDEV:
			func = pfprint_stddev;
			break;
		case DT_AGG_QUANTIZE:
			func = pfprint_quantize;
			break;
		case DT_AGG_LQUANTIZE:
			func = pfprint_lquantize;
			break;
		case DT_AGG_LLQUANTIZE:
			func = pfprint_llquantize;
			break;
		case DTRACEACT_MOD:
			func = pfprint_mod;
			break;
		case DTRACEACT_UMOD:
			func = pfprint_umod;
			break;
		default:
			func = pfc->pfc_print;
			break;
		}

		if (pfd->pfd_flags & DT_PFCONV_ALT)
			*f++ = '#';
		if (pfd->pfd_flags & DT_PFCONV_ZPAD)
			*f++ = '0';
		if (width < 0 || (pfd->pfd_flags & DT_PFCONV_LEFT))
			*f++ = '-';
		if (pfd->pfd_flags & DT_PFCONV_SPOS)
			*f++ = '+';
		if (pfd->pfd_flags & DT_PFCONV_GROUP)
			*f++ = '\'';
		if (pfd->pfd_flags & DT_PFCONV_SPACE)
			*f++ = ' ';

		/*
		 * If we're printing a stack and DT_PFCONV_LEFT is set, we
		 * don't add the width to the format string.  See the block
		 * comment in pfprint_stack() for a description of the
		 * behavior in this case.
		 */
		if (func == pfprint_stack && (pfd->pfd_flags & DT_PFCONV_LEFT))
			width = 0;

		if (width != 0)
			f += snprintf(f, sizeof(format) - (f - format), "%d",
				      ABS(width));

		if (prec > 0)
			f += snprintf(f, sizeof(format) - (f - format), ".%d",
				      prec);

		strcpy(f, pfd->pfd_fmt);
		pfd->pfd_rec = rec;

		if (func(dtp, fp, format, pfd, addr, size, normal, sig) < 0)
			return -1; /* errno is set for us */

		if (pfv->pfv_flags & DT_PRINTF_AGGREGATION) {
			/*
			 * For printa(), we flush the buffer after each tuple
			 * element, inidicating that this is the last record
			 * as appropriate.
			 */
			if (i == pfv->pfv_argc - 1)
				flags |= DTRACE_BUFDATA_AGGLAST;

			if (dt_buffered_flush(dtp, NULL,
			    rec, aggdata, flags) < 0)
				return -1;
		}
	}

	return (int)(recp - recs);
}

int
dtrace_sprintf(dtrace_hdl_t *dtp, FILE *fp, void *fmtdata,
    const dtrace_recdesc_t *recp, uint_t nrecs, const void *buf, size_t len)
{
	dtrace_optval_t size;
	int rval;

	rval = dtrace_getopt(dtp, "strsize", &size);
	assert(rval == 0);
	assert(dtp->dt_sprintf_buflen == 0);

	if (dtp->dt_sprintf_buf != NULL)
		free(dtp->dt_sprintf_buf);

	if ((dtp->dt_sprintf_buf = malloc(size)) == NULL)
		return dt_set_errno(dtp, EDT_NOMEM);

	memset(dtp->dt_sprintf_buf, 0, size);
	dtp->dt_sprintf_buflen = size;
	rval = dt_printf_format(dtp, fp, fmtdata, recp, nrecs, buf, len,
	    NULL, 0);
	dtp->dt_sprintf_buflen = 0;

	if (rval == -1)
		free(dtp->dt_sprintf_buf);

	return rval;
}

/*ARGSUSED*/
int
dtrace_system(dtrace_hdl_t *dtp, FILE *fp, void *fmtdata,
    const dtrace_probedata_t *data, const dtrace_recdesc_t *recp,
    uint_t nrecs, const void *buf, size_t len)
{
	int rval = dtrace_sprintf(dtp, fp, fmtdata, recp, nrecs, buf, len);

	if (rval == -1)
		return rval;

	/*
	 * Before we execute the specified command, flush fp to assure that
	 * any prior dt_printf()'s appear before the output of the command
	 * not after it.
	 */
	fflush(fp);

	if (system(dtp->dt_sprintf_buf) == -1)
		return dt_set_errno(dtp, errno);

	return rval;
}

int
dtrace_freopen(dtrace_hdl_t *dtp, FILE *fp, void *fmtdata,
    const dtrace_probedata_t *data, const dtrace_recdesc_t *recp,
    uint_t nrecs, const void *buf, size_t len)
{
	char tmpbuf[40];
	FILE *nfp;
	int rval, errval;
	dt_pfargv_t *pfv = fmtdata;
	dt_pfargd_t *pfd = pfv->pfv_argv;

	rval = dtrace_sprintf(dtp, fp, fmtdata, recp, nrecs, buf, len);

	if (rval == -1 || fp == NULL)
		return rval;

	if (pfd->pfd_preflen != 0 &&
	    strcmp(pfd->pfd_prefix, DT_FREOPEN_RESTORE) == 0) {
		/*
		 * The only way to have the format string set to the value
		 * DT_FREOPEN_RESTORE is via the empty freopen() string --
		 * denoting that we should restore the old stdout.
		 */
		assert(strcmp(dtp->dt_sprintf_buf, DT_FREOPEN_RESTORE) == 0);

		if (dtp->dt_stdout_fd == -1) {
			/*
			 * We could complain here by generating an error,
			 * but it seems like overkill:  it seems that calling
			 * freopen() to restore stdout when freopen() has
			 * never before been called should just be a no-op,
			 * so we just return in this case.
			 */
			return rval;
		}

		snprintf(tmpbuf, sizeof(tmpbuf),
		    "/dev/fd/%d", dtp->dt_stdout_fd);
		free(dtp->dt_freopen_filename);
		dtp->dt_freopen_filename = strndup(tmpbuf, sizeof(tmpbuf));
	} else {
		free(dtp->dt_freopen_filename);
		dtp->dt_freopen_filename = strdup(dtp->dt_sprintf_buf);
	}

	/*
	 * freopen(3C) will always close the specified stream and underlying
	 * file descriptor -- even if the specified file can't be opened.
	 * Even for the semantic cesspool that is standard I/O, this is
	 * surprisingly brain-dead behavior:  it means that any failure to
	 * open the specified file destroys the specified stream in the
	 * process -- which is particularly relevant when the specified stream
	 * happens (or rather, happened) to be stdout.  This could be resolved
	 * were there an "fdreopen()" equivalent of freopen() that allowed one
	 * to pass a file descriptor instead of the name of a file, but there
	 * is no such thing.  However, we can effect this ourselves by first
	 * fopen()'ing the desired file, and then (assuming that that works),
	 * freopen()'ing "/dev/fd/[fileno]", where [fileno] is the underlying
	 * file descriptor for the fopen()'d file.  This way, if the fopen()
	 * fails, we can fail the operation without destroying stdout.
	 */
	if ((nfp = fopen(dtp->dt_freopen_filename, "ace")) == NULL) {
		char *faultstr;

		asprintf(&faultstr, "couldn't freopen() \"%s\": %s",
				dtp->dt_freopen_filename, strerror(errno));

		if ((errval = dt_handle_liberr(dtp, data, faultstr)) == 0)
			return rval;

		free(faultstr);

		return errval;
	}

	snprintf(tmpbuf, sizeof(tmpbuf), "/dev/fd/%d", fileno(nfp));

	if (dtp->dt_stdout_fd == -1) {
		/*
		 * If this is the first time that we're calling freopen(),
		 * we're going to stash away the file descriptor for stdout.
		 * We don't expect the dup(2) to fail, so if it does we must
		 * return failure.
		 */
		if ((dtp->dt_stdout_fd = dup(fileno(fp))) == -1) {
			fclose(nfp);
			return dt_set_errno(dtp, errno);
		}
	}

	if (freopen(tmpbuf, "aF", fp) == NULL) {
		fclose(nfp);
		return dt_set_errno(dtp, errno);
	}

	fclose(nfp);

	return rval;
}

/*ARGSUSED*/
int
dtrace_fprintf(dtrace_hdl_t *dtp, FILE *fp, void *fmtdata,
    const dtrace_probedata_t *data, const dtrace_recdesc_t *recp,
    uint_t nrecs, const void *buf, size_t len)
{
	return dt_printf_format(dtp, fp, fmtdata,
	    recp, nrecs, buf, len, NULL, 0);
}

void *
dtrace_printf_create(dtrace_hdl_t *dtp, const char *s)
{
	dt_pfargv_t *pfv = dt_printf_create(dtp, s);
	dt_pfargd_t *pfd;
	int i;

	if (pfv == NULL)
		return NULL;		/* errno has been set for us */

	pfd = pfv->pfv_argv;

	for (i = 0; i < pfv->pfv_argc; i++, pfd = pfd->pfd_next) {
		const dt_pfconv_t *pfc = pfd->pfd_conv;

		if (pfc == NULL)
			continue;

		/*
		 * If the output format is not %s then we assume that we have
		 * been given a correctly-sized format string, so we copy the
		 * true format name including the size modifier.  If the output
		 * format is %s, then either the input format is %s as well or
		 * it is one of our custom formats (e.g. pfprint_addr), so we
		 * must set pfd_fmt to be the output format conversion "s".
		 */
		if (strcmp(pfc->pfc_ofmt, "s") != 0)
			strcat(pfd->pfd_fmt, pfc->pfc_name);
		else
			strcat(pfd->pfd_fmt, pfc->pfc_ofmt);
	}

	return pfv;
}

void *
dtrace_printa_create(dtrace_hdl_t *dtp, const char *s)
{
	dt_pfargv_t *pfv = dtrace_printf_create(dtp, s);

	if (pfv == NULL)
		return NULL;		/* errno has been set for us */

	pfv->pfv_flags |= DT_PRINTF_AGGREGATION;

	return pfv;
}

/*ARGSUSED*/
size_t
dtrace_printf_format(dtrace_hdl_t *dtp, void *fmtdata, char *s, size_t len)
{
	dt_pfargv_t *pfv = fmtdata;
	dt_pfargd_t *pfd = pfv->pfv_argv;

	/*
	 * An upper bound on the string length is the length of the original
	 * format string, plus three times the number of conversions (each
	 * conversion could add up an additional "ll" and/or pfd_width digit
	 * in the case of converting %? to %16) plus one for a terminating \0.
	 */
	size_t formatlen = strlen(pfv->pfv_format) + 3 * pfv->pfv_argc + 1;
	char *format = alloca(formatlen);
	char *f = format;
	int i, j;

	for (i = 0; i < pfv->pfv_argc; i++, pfd = pfd->pfd_next) {
		const dt_pfconv_t *pfc = pfd->pfd_conv;
		const char *str;
		int width = pfd->pfd_width;
		int prec = pfd->pfd_prec;

		if (pfd->pfd_preflen != 0) {
			for (j = 0; j < pfd->pfd_preflen; j++)
				*f++ = pfd->pfd_prefix[j];
		}

		if (pfc == NULL)
			continue;

		*f++ = '%';

		if (pfd->pfd_flags & DT_PFCONV_ALT)
			*f++ = '#';
		if (pfd->pfd_flags & DT_PFCONV_ZPAD)
			*f++ = '0';
		if (pfd->pfd_flags & DT_PFCONV_LEFT)
			*f++ = '-';
		if (pfd->pfd_flags & DT_PFCONV_SPOS)
			*f++ = '+';
		if (pfd->pfd_flags & DT_PFCONV_DYNWIDTH)
			*f++ = '*';
		if (pfd->pfd_flags & DT_PFCONV_DYNPREC) {
			*f++ = '.';
			*f++ = '*';
		}
		if (pfd->pfd_flags & DT_PFCONV_GROUP)
			*f++ = '\'';
		if (pfd->pfd_flags & DT_PFCONV_SPACE)
			*f++ = ' ';
		if (pfd->pfd_flags & DT_PFCONV_AGG)
			*f++ = '@';

		if (width != 0)
			f += snprintf(f, formatlen - (f - format), "%d",
				      width);

		if (prec != 0)
			f += snprintf(f, formatlen - (f - format), ".%d",
				      prec);

		/*
		 * If the output format is %s, then either %s is the underlying
		 * conversion or the conversion is one of our customized ones,
		 * e.g. pfprint_addr.  In these cases, put the original string
		 * name of the conversion (pfc_name) into the pickled format
		 * string rather than the derived conversion (pfd_fmt).
		 */
		if (strcmp(pfc->pfc_ofmt, "s") == 0)
			str = pfc->pfc_name;
		else
			str = pfd->pfd_fmt;

		for (j = 0; str[j] != '\0'; j++)
			*f++ = str[j];
	}

	*f = '\0'; /* insert nul byte; do not count in return value */

	assert(f < format + formatlen);
	strncpy(s, format, len);

	return (size_t)(f - format);
}

static int
dt_fprinta(const dtrace_aggdata_t *adp, void *arg)
{
	const dtrace_aggdesc_t	*agg = adp->dtada_desc;
	const dtrace_recdesc_t	*rec = agg->dtagd_recs;
	uint_t			nrecs = agg->dtagd_nrecs;
	dt_pfwalk_t		*pfw = arg;
	dtrace_hdl_t		*dtp = pfw->pfw_argv->pfv_dtp;

	if (pfw->pfw_aid != agg->dtagd_id)
		return 0;	/* id does not match */

	if (dt_printf_format(dtp, pfw->pfw_fp, pfw->pfw_argv, rec, nrecs,
			     adp->dtada_data, adp->dtada_size, &adp, 1) == -1)
		return (pfw->pfw_err = dtp->dt_errno);

	/*
	 * Cast away the const to set the bit indicating that this aggregation
	 * has been printed.
	 */
	((dtrace_aggdesc_t *)agg)->dtagd_flags |= DTRACE_AGD_PRINTED;

	return 0;
}

static int
dt_fprintas(const dtrace_aggdata_t **aggsdata, int naggvars, void *arg)
{
	const dtrace_aggdata_t	*aggdata = aggsdata[0];
	const dtrace_aggdesc_t	*agg = aggdata->dtada_desc;
	const dtrace_recdesc_t	*rec = agg->dtagd_recs;
	uint_t			nrecs = agg->dtagd_nrecs;
	dt_pfwalk_t		*pfw = arg;
	dtrace_hdl_t		*dtp = pfw->pfw_argv->pfv_dtp;
	int			i;

	if (dt_printf_format(dtp, pfw->pfw_fp, pfw->pfw_argv, rec, nrecs,
			     aggdata->dtada_data, aggdata->dtada_size,
			     aggsdata, naggvars) == -1)
		return (pfw->pfw_err = dtp->dt_errno);

	/*
	 * For each aggregation, indicate that it has been printed, casting
	 * away the const as necessary.
	 */
	for (i = 1; i < naggvars; i++) {
		agg = aggsdata[i]->dtada_desc;
		((dtrace_aggdesc_t *)agg)->dtagd_flags |= DTRACE_AGD_PRINTED;
	}

	return 0;
}

int
dtrace_fprinta(dtrace_hdl_t *dtp, FILE *fp, void *fmtdata,
	       const dtrace_probedata_t *data, const dtrace_recdesc_t *recs,
	       uint_t nrecs, const void *buf, size_t len)
{
	dt_pfwalk_t	pfw;
	int		i, naggvars = 0;
	dtrace_aggid_t	*aggvars;

	aggvars = alloca(nrecs * sizeof(dtrace_aggid_t));

	/*
	 * This might be a printa() with multiple aggregation variables.  We
	 * need to scan forward through the records until we find a record that
	 * does not belong to this printa() statement.
	 */
	for (i = 0; i < nrecs; i++) {
		const dtrace_recdesc_t *nrec = &recs[i];

		if (nrec->dtrd_arg != recs->dtrd_arg)
			break;

		if (nrec->dtrd_action != recs->dtrd_action)
			return dt_set_errno(dtp, EDT_BADAGG);

		aggvars[naggvars++] =
		    /* LINTED - alignment */
		    *((dtrace_aggid_t *)((caddr_t)buf + nrec->dtrd_offset));
	}

	if (naggvars == 0)
		return dt_set_errno(dtp, EDT_BADAGG);

	pfw.pfw_argv = fmtdata;
	pfw.pfw_fp = fp;
	pfw.pfw_err = 0;

	if (naggvars == 1) {
		pfw.pfw_aid = aggvars[0];

		if (dtrace_aggregate_walk_sorted(dtp, dt_fprinta, &pfw) == -1 ||
		    pfw.pfw_err != 0)
			return -1; /* errno is set for us */
	} else {
		if (dtrace_aggregate_walk_joined(dtp, aggvars, naggvars,
						 dt_fprintas, &pfw) == -1 ||
		    pfw.pfw_err != 0)
			return -1; /* errno is set for us */
	}

	return i;
}
