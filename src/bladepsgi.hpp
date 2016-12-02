#ifndef __BLADEPSGI_MAIN_HEADER__
#define __BLADEPSGI_MAIN_HEADER__

// This will probably do for now
#define Assert(cond) assert(cond)

#include <atomic>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <vector>
#include <signal.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "make_unique.hpp"
using std::unique_ptr;


typedef int WorkerNo;

class SyscallException {
public:
	SyscallException(const char *syscall, int s_errno);
	SyscallException(const char *syscall, const char *fmt, ...);
	~SyscallException();

	const char *syscall() const;
	const char *strerror() const;

private:
	const char *syscall_;

	// Only one of these is set
	int s_errno_;
	const char *strerror_;
};

class BPSGISharedMemory {
public:

public:
	BPSGISharedMemory(void *shared_memory_segment, size_t shmem_size);

	std::atomic<int_fast8_t> *WorkerArraySegment() const;
	int_fast8_t GetWorkerStatus(WorkerNo workerno) const;
	void GetAllWorkerStatuses(int nworkers, char *out) const;

	int_fast64_t IncreaseRequestCounter();
	bool SetShouldExitImmediately();
	bool ShouldExitImmediately() const;

private:
	char   *shared_memory_segment_;
	size_t	shmem_size_;
};

enum LogSeverity {
	LS_INFO,
	LS_WARNING,
	LS_ERROR,
	LS_FATAL,
	LS_PANIC,
};

class PerlInterpreterException {
public:
	PerlInterpreterException(const char *fmt, ...);
	~PerlInterpreterException();

	const char * strerror() const { return strerror_; }

private:
	const char *strerror_;
};

struct bladepsgi_perl_callback_t;

class BPSGIPerlCallbackFunction {
public:
	BPSGIPerlCallbackFunction(struct bladepsgi_perl_callback_t *p);

	void Call();
	unique_ptr<BPSGIPerlCallbackFunction> CallAndReceiveCallback();

private:
	struct bladepsgi_perl_callback_t *p_;
};

class BPSGIMainApplication;
class BPSGIWorker;

class BPSGIPerlInterpreter {
public:
	static void PerProcessInit();

	BPSGIPerlInterpreter(BPSGIMainApplication *mainapp);
	~BPSGIPerlInterpreter();

	unique_ptr<BPSGIPerlCallbackFunction> LoadCallback(const char *filename);
	unique_ptr<BPSGIPerlCallbackFunction> LoadCallbackFromCString(const char *perl);

	void WorkerInitialize(BPSGIWorker *worker);

	void Destroy();
private:
	bool destroyed_;

	BPSGIMainApplication *mainapp_;
	void *ctx_;
};

enum BPSGISubprocessInitFlags {
	SUBP_DEFAULT_FLAGS		= 0,
	SUBP_NO_DEATHSIG		= 1,
};

class BPSGIMainApplication {
public:

public:
	BPSGIMainApplication(
		int argc,
		char ** argv,
		const char *psgi_application_path,
		int nworkers,
		const char *application_loader,
		const char *fastcgi_socket_path,
		const char *process_title_prefix
	);

	int Run();

	bool RunnerDied();

	void SetWorkerStatus(WorkerNo workerno, int_fast8_t status);

	int nworkers() const { return nworkers_; }
	pid_t runner_pid() const { return runner_pid_; }
	BPSGISharedMemory * shmem() const { return shmem_.get(); }
	int fastcgi_sockfd() const { return fastcgi_sockfd_; }

	const char *psgi_application_path() const { return psgi_application_path_; }
	const char *psgi_application_loader() const { return application_loader_; }

	void KillProcessGroup(int sig);

	bool ShouldExitImmediately() const { return shmem_->ShouldExitImmediately(); }
	bool SetShouldExitImmediately();

	void Log(LogSeverity severity, const char *fmt, ...);
	void LogPanic(const char *fmt, ...);

	unique_ptr<BPSGIPerlInterpreter> InitializePerlInterpreter();

	void SubprocessInit(const char *new_process_title, BPSGISubprocessInitFlags flags);
	void SetSignalHandler(int signum, void (*handlerfunc)(int));
	void BlockSignals();
	void UnblockSignals();

	void HandleParentSignal(int sig);

protected:
	void SetProcessTitle(const char *value);

	void InitializeSelfPipe();
	void DrainSelfPipe();

	void InitializeSharedMemory();
	void InitializeFastCGISocket();

	void SpawnWorkers();
	void RunWorker(WorkerNo workerno, unique_ptr<BPSGIPerlInterpreter> interpreter, unique_ptr<BPSGIPerlCallbackFunction> main_callback);

	void SpawnMonitoringProcess();
	void RunMonitoringProcess();

	void HandleUnexpectedChildProcessDeath(const char *process, pid_t pid, int status);
	void HandleChildProcessDeath(pid_t pid, int status);

private:
	std::vector<sigset_t>	signal_mask_stack_;

	int		argc_;
	char  **argv_;

	const char *psgi_application_path_;
	int			nworkers_;
	const char *application_loader_;
	const char *fastcgi_socket_path_;
	const char *process_title_prefix_;

	pid_t	runner_pid_;
	pid_t	monitoring_process_pid_;
	std::vector<pid_t> worker_pids_;

	unique_ptr<BPSGISharedMemory> shmem_;
	int fastcgi_sockfd_;
};

class PSGIApplication {
public:

public:

private:
};

class BPSGIMonitoring {
public:
	BPSGIMonitoring(BPSGIMainApplication *mainapp);
	int Run();

private:
	BPSGIMainApplication *mainapp_;
};

class BPSGIWorker {
public:

public:
	BPSGIWorker(BPSGIMainApplication *mainapp, WorkerNo workerno);
	int Run(BPSGIPerlCallbackFunction &main_callback);

	void SetWorkerStatus(char status);

private:
	void MainLoopIteration(BPSGIPerlCallbackFunction &main_callback);

private:
	BPSGIMainApplication *mainapp_;
	WorkerNo workerno_;
};

#endif
