#include "bladepsgi.hpp"

extern "C" {
#include "perl/bladepsgi_perl.h"
#include "perl/XS.h"
}

PerlInterpreterException::PerlInterpreterException(const char *fmt, ...)
{
	va_list ap;
	const size_t bufsize = 4096;
	char *buf = (char *) malloc(bufsize);

	va_start(ap, fmt);
	(void) vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);

	/*
	 * Most errors spat out by the Perl interpreter have newlines appended to
	 * them.  We don't want to keep them, so get rid of them here.
	 */
	char *p = buf + strlen(buf) - 1;
	while (p >= buf && (*p == '\r' || *p == '\n'))
	{
		*p = '\0';
		p--;
	}

	strerror_ = buf;
}

PerlInterpreterException::~PerlInterpreterException()
{
	Assert(strerror_ != NULL);

	free((void *) strerror_);
	strerror_ = NULL;
}

void
BPSGIPerlInterpreter::PerProcessInit()
{
	bladepsgi_perl_per_process_init();
}

BPSGIPerlInterpreter::BPSGIPerlInterpreter(BPSGIMainApplication *mainapp)
	: destroyed_(false),
	  mainapp_(mainapp)
{
	char *error;
	int ret = bladepsgi_perl_interpreter_init(&error);
	if (ret == -1)
		throw PerlInterpreterException("could not initialize a Perl interpreter: %s", error);

	BPSGI_Context *ctx = (BPSGI_Context *) malloc(sizeof(BPSGI_Context));
	memset(ctx, 0, sizeof(BPSGI_Context));
	ctx->mainapp = (void *) mainapp_;
	ctx->worker = (void *) NULL;
	ctx_ = (void *) ctx;
}

unique_ptr<BPSGIPerlCallbackFunction>
BPSGIPerlInterpreter::LoadCallback(const char *filename)
{
	char *error;
	struct bladepsgi_perl_callback_t *callback_p;
	int ret = bladepsgi_perl_callback_init(ctx_, filename, "do", &callback_p, &error);
	if (ret == -1)
		throw PerlInterpreterException("%s", error);

	return make_unique<BPSGIPerlCallbackFunction>(callback_p);
}

unique_ptr<BPSGIPerlCallbackFunction>
BPSGIPerlInterpreter::LoadCallbackFromCString(const char *perl)
{
	char *error;
	struct bladepsgi_perl_callback_t *callback_p;
	int ret = bladepsgi_perl_callback_init(ctx_, perl, "eval", &callback_p, &error);
	if (ret == -1)
		throw PerlInterpreterException("%s", error);

	return make_unique<BPSGIPerlCallbackFunction>(callback_p);
}

void
BPSGIPerlInterpreter::WorkerInitialize(BPSGIWorker *worker)
{
	BPSGI_Context *ctx = (BPSGI_Context *) ctx_;
	Assert(ctx->worker == NULL);
	ctx->worker = worker;
}

BPSGIPerlCallbackFunction::BPSGIPerlCallbackFunction(struct bladepsgi_perl_callback_t *p)
	: p_(p)
{
}

void
BPSGIPerlCallbackFunction::Call()
{
	char *error;
	int ret = bladepsgi_perl_callback_call(p_, &error);
	if (ret == -1)
		throw PerlInterpreterException("%s", error);
}

unique_ptr<BPSGIPerlCallbackFunction>
BPSGIPerlCallbackFunction::CallAndReceiveCallback()
{
	struct bladepsgi_perl_callback_t *callback_p;
	char *error;
	int ret = bladepsgi_perl_callback_call_and_receive_callback(p_, &error, &callback_p);
	if (ret == -1)
		throw PerlInterpreterException("%s", error);
	return make_unique<BPSGIPerlCallbackFunction>(callback_p);
}

void
BPSGIPerlInterpreter::Destroy()
{
	/*
	 * N.B: We must set this even if we throw an exception, or the destructor
	 * will abort uncleanly during stack unwinding.
	 */
	destroyed_ = true;

	char *error;
	int ret = bladepsgi_perl_interpreter_destroy(&error);
	if (ret == -1)
		throw PerlInterpreterException("%s", error);
}

BPSGIPerlInterpreter::~BPSGIPerlInterpreter()
{
	if (!destroyed_)
	{
		fprintf(stderr, "BPSGIPerlInterpreter destroyed without calling Destroy\n");
		_exit(1);
	}
	free(ctx_);
}
