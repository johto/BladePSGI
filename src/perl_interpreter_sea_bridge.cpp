#include "bladepsgi.hpp"

extern "C" {
#include "perl/bladepsgi_perl.h"
#include "perl/XS.h"

void
bladepsgi_perl_interpreter_cb_set_worker_status(BPSGI_Context *ctx, const char *status)
{
	Assert(ctx->mainapp != NULL && ctx->worker != NULL);

	auto worker = (BPSGIWorker *) ctx->worker;
	worker->SetWorkerStatus(status[0]);
}

int
bladepsgi_perl_interpreter_cb_fastcgi_listen_sockfd(BPSGI_Context *ctx)
{
	Assert(ctx->mainapp != NULL);

	auto mainapp = (BPSGIMainApplication *) ctx->mainapp;
	Assert(mainapp->fastcgi_sockfd() != -1);

	return mainapp->fastcgi_sockfd();
}

const char *
bladepsgi_perl_interpreter_cb_psgi_application_path(BPSGI_Context *ctx)
{
	Assert(ctx->mainapp != NULL);

	auto mainapp = (BPSGIMainApplication *) ctx->mainapp;
	Assert(mainapp->psgi_application_path() != NULL);

	return mainapp->psgi_application_path();
}

const char *
bladepsgi_perl_interpreter_cb_psgi_application_loader(BPSGI_Context *ctx)
{
	Assert(ctx->mainapp != NULL);

	auto mainapp = (BPSGIMainApplication *) ctx->mainapp;

	return mainapp->psgi_application_loader();
}


}
