#include "bladepsgi.hpp"

#include <unistd.h>

static sig_atomic_t _worker_terminated = 0;

static void
worker_sigterm_handler(int _unused)
{
	/*
	 * SIGTERM means "finish the request you're currently doing and then quit".
	 * The worker's main loop handles the actual quitting.
	 */
	(void) _unused;
	_worker_terminated = 1;
}

static void
worker_sigquit_handler(int _unused)
{
	/*
	 * On SIGQUIT we just die immediately.  No cleanup is attempted (or
	 * necessary); this should only happen if we really should shut ourselves
	 * down immediately.
	 */
	(void) _unused;
	_exit(2);
}

BPSGIWorker::BPSGIWorker(BPSGIMainApplication *mainapp, WorkerNo workerno)
	: mainapp_(mainapp),
	  workerno_(workerno)
{
}

void
BPSGIWorker::SetWorkerStatus(char status)
{
	mainapp_->SetWorkerStatus(workerno_, status);
}

void
BPSGIWorker::MainLoopIteration(BPSGIPerlCallbackFunction &main_callback)
{
	if (mainapp_->ShouldExitImmediately())
		_exit(1);
	else if (_worker_terminated == 1)
		_exit(0);

	SetWorkerStatus('_');

	main_callback.Call();

	mainapp_->shmem()->IncreaseRequestCounter();
}

int
BPSGIWorker::Run(BPSGIPerlCallbackFunction &main_callback)
{
	char process_title[64];

	snprintf(process_title, sizeof(process_title), "worker %d", (int) workerno_);
	mainapp_->SubprocessInit(process_title, SUBP_DEFAULT_FLAGS);
	mainapp_->SetSignalHandler(SIGCHLD, SIG_DFL);
	mainapp_->SetSignalHandler(SIGINT, SIG_IGN);
	mainapp_->SetSignalHandler(SIGTERM, worker_sigterm_handler);
	mainapp_->SetSignalHandler(SIGQUIT, worker_sigquit_handler);
	mainapp_->UnblockSignals();

	try {
		for (;;)
			MainLoopIteration(main_callback);
	} catch (const PerlInterpreterException &ex) {
		mainapp_->Log(LS_ERROR, "Perl exception: %s", ex.strerror());
		_exit(1);
	}
	abort();
}
