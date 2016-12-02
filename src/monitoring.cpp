#include "bladepsgi.hpp"

#include <cstdio>
#include <unistd.h>

static sig_atomic_t _monitoring_terminated = 0;

static void
monitoring_sigterm_handler(int _unused)
{
	(void) _unused;
	_monitoring_terminated = 1;
}

static void
monitoring_sigquit_handler(int _unused)
{
	/*
	 * On SIGQUIT we just die immediately.  No cleanup is attempted (or
	 * necessary); this should only happen if we really should shut ourselves
	 * down immediately.
	 */
	(void) _unused;
	_exit(2);
}


BPSGIMonitoring::BPSGIMonitoring(BPSGIMainApplication *mainapp)
	: mainapp_(mainapp)
{
}

int
BPSGIMonitoring::Run()
{
	mainapp_->SubprocessInit("monitoring", SUBP_NO_DEATHSIG);
	mainapp_->SetSignalHandler(SIGINT, SIG_IGN);
	mainapp_->SetSignalHandler(SIGTERM, monitoring_sigterm_handler);
	mainapp_->SetSignalHandler(SIGQUIT, monitoring_sigquit_handler);
	mainapp_->UnblockSignals();

	int nworkers = mainapp_->nworkers();
	auto worker_status_data = std::vector<char>(nworkers + 1);
	worker_status_data[nworkers] = '\0';
	auto shmem = mainapp_->shmem();

	for (;;)
	{
		/* TODO: do more here! */
		if (mainapp_->RunnerDied())
		{
			if (mainapp_->shmem()->SetShouldExitImmediately())
				mainapp_->Log(LS_FATAL, "monitoring process noticed that the parent process has died; terminating all processes");
			_exit(1);
		}

		sleep(1);

		shmem->GetAllWorkerStatuses(nworkers, worker_status_data.data());
		fprintf(stdout, "%s\n", worker_status_data.data());

		if (_monitoring_terminated == 1)
			_exit(0);
	}
}
