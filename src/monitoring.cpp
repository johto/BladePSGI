#include "bladepsgi.hpp"

#include <sys/socket.h>
#include <sys/un.h>

#include <sstream>


static std::string
int64_to_string(int64_t value)
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

static void
monitoring_sigquit_handler(int _unused)
{
	/* behave like worker_sigquit_handler */
	(void) _unused;
	_exit(2);
}


BPSGIMonitoring::BPSGIMonitoring(BPSGIMainApplication *mainapp)
	: mainapp_(mainapp)
{
}

void
BPSGIMonitoring::HandleClient(int listensockfd, std::vector<char> worker_status_array)
{
	struct sockaddr_un their_addr;
	socklen_t addr_size = sizeof(their_addr);

	int clientfd = accept(listensockfd, (struct sockaddr *) &their_addr, &addr_size);
	if (clientfd == -1)
	{
		if (errno == EINTR || errno == ECONNABORTED)
		{
			/* we'll get called again if there's still a client waiting */
			return;
		}
		throw SyscallException("accept", errno);
	}

	auto shmem = mainapp_->shmem();

	auto statdata = std::string(worker_status_array.data(), worker_status_array.size()) + "\n";
	statdata += int64_to_string(shmem->ReadRequestCounter()) + "\n";
	statdata += "\n";
	for (auto && sem : shmem->semaphores_)
		statdata += "sem " + sem->name() + ": " + int64_to_string(sem->Read()) + "\n";
	for (auto && atm : shmem->atomics_)
		statdata += "atomic " + atm->name() + ": " + int64_to_string(atm->Read()) + "\n";

	auto written = write(clientfd, statdata.c_str(), statdata.size());
	(void) written;
	shutdown(clientfd, SHUT_RDWR);
	close(clientfd);
}

int
BPSGIMonitoring::Run()
{
	mainapp_->SubprocessInit("monitoring", SUBP_NO_DEATHSIG);
	mainapp_->SetSignalHandler(SIGINT, SIG_DFL);
	mainapp_->SetSignalHandler(SIGTERM, SIG_DFL);
	mainapp_->SetSignalHandler(SIGQUIT, monitoring_sigquit_handler);
	mainapp_->UnblockSignals();

	int listen_sockfd = mainapp_->stats_sockfd();

	int nworkers = mainapp_->nworkers();
	auto worker_status_data = std::vector<char>(nworkers);
	auto shmem = mainapp_->shmem();

	for (;;)
	{
		struct timeval tv;
		fd_set fds;

		if (mainapp_->RunnerDied())
		{
			if (mainapp_->shmem()->SetShouldExitImmediately())
				mainapp_->Log(LS_FATAL, "monitoring process noticed that the parent process has died; terminating all processes");
			_exit(1);
		}

		memset(&tv, 0, sizeof(tv));
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		FD_ZERO(&fds);
		FD_SET(listen_sockfd, &fds);

		int ret = select(listen_sockfd + 1, &fds, NULL, NULL, &tv);
		if (ret == 0)
			continue;
		else if (ret == -1)
		{
			if (errno != EINTR)
				throw SyscallException("select", errno);
		}
		else if (ret == 1)
		{
			shmem->GetAllWorkerStatuses(nworkers, worker_status_data.data());
			HandleClient(listen_sockfd, worker_status_data);
			continue;
		}
		else
			throw SyscallException("select", "unexpected return value %d", ret);
	}
}
