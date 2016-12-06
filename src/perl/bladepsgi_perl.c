#include <stdio.h>
#include <stdlib.h>
#include <EXTERN.h>
#include <perl.h>

#include "XS.h"

#include "bladepsgi_perl.h"

#define NUM_PERL_ARGS       3

static char *perl_args[NUM_PERL_ARGS];
extern char **environ;

void
bladepsgi_perl_per_process_init(void)
{
	int nargs = NUM_PERL_ARGS;

	perl_args[0] = strdup("");
	perl_args[1] = strdup("-e");
	perl_args[2] = strdup("0");

	PERL_SYS_INIT3(&nargs, (char ***) &perl_args, &environ);
}

static PerlInterpreter *my_perl = NULL;

EXTERN_C void boot_DynaLoader(pTHX_ CV *cv);
EXTERN_C void boot_BPSGI(pTHX_ CV *cv);

static void
bladepsgi_perl_xs_init(pTHX)
{
	char *file = __FILE__;
	dXSUB_SYS;

	newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
	newXS("BPSGI::bootstrap", boot_BPSGI, file);
}

int
bladepsgi_perl_interpreter_init(char **error_out)
{
	my_perl = perl_alloc();
	if (!my_perl)
	{
		*error_out = "could not allocate perl interpreter\n";
		return -1;
	}
	/*
	 * Run END blocks in perl_destruct instead of perl_run.  This allows us to
	 * keep the interpreter alive for the entire program.
	 */
	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
	perl_construct(my_perl);
	/* disable $0 assignments (see perlembed) */
	PL_origalen = 1;
	if (perl_parse(my_perl, bladepsgi_perl_xs_init, NUM_PERL_ARGS, perl_args, NULL) != 0)
	{
		*error_out = "perl_parse failed\n";
		return -1;
	}
	if (perl_run(my_perl) != 0)
	{
		*error_out = "perl_run failed\n";
		return -1;
	}
	SV *has_plack_util = eval_pv("eval { require Plack::Util; }; die $@ if ($@);", 0);
	if (SvTRUE(ERRSV))
	{
		const char *msg = SvPV_nolen(ERRSV);
		size_t buflen = strlen(msg) + 128;
		char *buf = malloc(buflen);

		snprintf(buf, buflen, "could not load Plack::Util: %s", msg);
		*error_out = buf;
		return -1;
	}
	eval_pv("BPSGI::bootstrap", 1);
	if (SvTRUE(ERRSV))
	{
		const char *msg = SvPV_nolen(ERRSV);
		size_t buflen = strlen(msg) + 128;
		char *buf = malloc(buflen);

		snprintf(buf, buflen, "could not load Plack::Util: %s", msg);
		*error_out = buf;
		return -1;
	}
	eval_pv("sub _bladepsgi_do_wrapper { return do $_[0]; }", 0);
	if (SvTRUE(ERRSV))
	{
		const char *msg = SvPV_nolen(ERRSV);
		size_t buflen = strlen(msg) + 128;
		char *buf = malloc(buflen);

		snprintf(buf, buflen, "could not load Plack::Util: %s", msg);
		*error_out = buf;
		return -1;
	}


	return 0;
}

int
bladepsgi_perl_interpreter_destroy(char **error_out)
{
	perl_destruct(my_perl);
	perl_free(my_perl);
	PERL_SYS_TERM();
	return 0;
}

int
bladepsgi_perl_callback_init(void *bladepsgictx, const char *source, const char *loader, struct bladepsgi_perl_callback_t **callback_out, char **error_out)
{
	int save_errno;
	SV *callback;
	int ok;

	{
		dSP;
		ENTER;
		SAVETMPS;
		PUSHMARK(sp);
		mXPUSHs(newSVpv(source, 0));
		PUTBACK;
		errno = 0;
		if (strcmp(loader, "do") == 0)
		{
			perl_call_pv("_bladepsgi_do_wrapper", G_SCALAR);
			save_errno = errno;
			SPAGAIN;

			callback = POPs;
		}
		else if (strcmp(loader, "eval") == 0)
		{
			callback = perl_eval_pv(source, 0);
			save_errno = errno;
			SPAGAIN;
		}
		else
		{
			fprintf(stderr, "internal error: invalid loader %s in call to bladepsgi_perl_callback_init\n", loader);
			abort();
		}

		if (SvOK(callback))
		{
			ok = 1;
			SvREFCNT_inc(callback);
		}
		else
		{
			ok = 0;
			callback = NULL;
		}

		PUTBACK;
		FREETMPS;
		LEAVE;
	}

	if (!ok)
	{
		if (SvTRUE(ERRSV))
		{
			const char *msg = SvPV_nolen(ERRSV);
			size_t buflen = strlen(msg) + 128;
			char *buf = malloc(buflen);
			char *p;

			snprintf(buf, buflen, "%s", msg);
			*error_out = buf;
			return -1;
		}
		else if (save_errno != 0)
		{
			*error_out = strerror(save_errno);
			return -1;
		}
		else
		{
			fprintf(stderr, "poopfeast420\n");
			_exit(EXIT_FAILURE);
		}
	}
	*callback_out = malloc(sizeof(struct bladepsgi_perl_callback_t));
	memset(*callback_out, 0, sizeof(struct bladepsgi_perl_callback_t));
	(*callback_out)->bladepsgictx = (void *) bladepsgictx;
	(*callback_out)->sv = (void *) callback;
	return 0;
}

int
bladepsgi_perl_callback_call(struct bladepsgi_perl_callback_t *cbs, char **error_out)
{
	SV *callback = (SV *) cbs->sv;
	SV *ctxsv;

	{
		dSP;
		ENTER;
		SAVETMPS;
		PUSHMARK(sp);
		ctxsv = newSViv(0);
		ctxsv = sv_setref_pv(ctxsv, "BPSGI::Context", cbs->bladepsgictx);
		mXPUSHs(ctxsv);
		PUTBACK;
		perl_call_sv(callback, G_SCALAR | G_EVAL | G_DISCARD);
		SPAGAIN;

		if (SvTRUE(ERRSV))
		{
			*error_out = strdup(SvPV_nolen(ERRSV));
			return -1;
		}

		FREETMPS;
		LEAVE;
	}
	return 0;
}

int
bladepsgi_perl_callback_call_and_receive_callback(struct bladepsgi_perl_callback_t *cbs, char **error_out, struct bladepsgi_perl_callback_t **cbs_out)
{
	SV *callback = (SV *) cbs->sv;
	SV *ctxsv;
	SV *received;

	{
		dSP;
		ENTER;
		SAVETMPS;
		PUSHMARK(sp);
		ctxsv = newSViv(0);
		ctxsv = sv_setref_pv(ctxsv, "BPSGI::Context", cbs->bladepsgictx);
		mXPUSHs(ctxsv);
		PUTBACK;
		perl_call_sv(callback, G_SCALAR | G_EVAL);
		SPAGAIN;

		received = POPs;
		if (SvTRUE(ERRSV))
		{
			*error_out = strdup(SvPV_nolen(ERRSV));
			return -1;
		}
		else if (SvTYPE(SvRV(received)) != SVt_PVCV)
		{
			*error_out = "return value is not a callback";
			return -1;
		}
		SvREFCNT_inc(received);

		FREETMPS;
		LEAVE;
	}

	*cbs_out = malloc(sizeof(struct bladepsgi_perl_callback_t));
	memset(*cbs_out, 0, sizeof(struct bladepsgi_perl_callback_t));
	(*cbs_out)->bladepsgictx = cbs->bladepsgictx;
	(*cbs_out)->sv = (void *) received;

	return 0;
}
