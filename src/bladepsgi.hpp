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
#include <string>
#include <semaphore.h>
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

	const char *syscall() const { return syscall_; }
	const char *strerror() const;

private:
	const char *syscall_;

	// Only one of these is set
	int s_errno_;
	char *strerror_;
};

class RuntimeException {
public:
	RuntimeException(const char *fmt, ...);
	~RuntimeException();

	const char *error() const { return error_; }

private:
	char *error_;
};


class BPSGISemaphore {
public:
	BPSGISemaphore(sem_t *sem, std::string name);

	int64_t Read();
	void Acquire();
	bool TryAcquire();
	void Release();

	std::string name() const { return name_; }

private:
	sem_t *sem_;
	std::string name_;
};

class BPSGIAtomicInt64 {
public:
	BPSGIAtomicInt64(void *ptr, std::string name, int64_t value);

	int64_t Read();

	std::string name() const { return name_; }

private:
	std::atomic<int64_t> *ptr_;
	std::string name_;
};

class BPSGISharedMemory {
	friend class BPSGIMainApplication;
	friend class BPSGIMonitoring;

public:
	BPSGISharedMemory(void *shared_memory_segment, size_t shmem_size);

	void *AllocateUserShmem(size_t size);
	BPSGISemaphore *NewSemaphore(std::string name, int64_t value);
	int64_t *NewAtomicInt64(std::string name, int64_t value);

	std::atomic<int_fast8_t> *WorkerArraySegment() const;
	int_fast8_t GetWorkerStatus(WorkerNo workerno) const;
	void GetAllWorkerStatuses(int nworkers, char *out) const;

	int_fast64_t IncreaseRequestCounter();
	int_fast64_t ReadRequestCounter();
	bool SetShouldExitImmediately();
	bool ShouldExitImmediately() const;

protected:
	void LockAllocations();

	std::vector<unique_ptr<BPSGISemaphore>> semaphores_;
	std::vector<unique_ptr<BPSGIAtomicInt64>> atomics_;
private:
	char   *shared_memory_segment_;
	size_t	shmem_size_;
	bool	locked_;
	size_t	next_user_available_offset_;
};

enum LogSeverity {
	LS_LOG,
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
class BPSGIAuxiliaryProcess;

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
		const char *stats_socket_path,
		const char *process_title_prefix
	);

	int Run();

	bool RunnerDied();

	void SetWorkerStatus(WorkerNo workerno, int_fast8_t status);

	int nworkers() const { return nworkers_; }
	pid_t runner_pid() const { return runner_pid_; }
	BPSGISharedMemory * shmem() const { return shmem_.get(); }
	int fastcgi_sockfd() const { return fastcgi_sockfd_; }
	int stats_sockfd() const { return stats_sockfd_; }

	const char *psgi_application_path() const { return psgi_application_path_; }
	const char *psgi_application_loader() const { return application_loader_; }

	void RequestAuxiliaryProcess(std::string name, unique_ptr<BPSGIPerlCallbackFunction> callback);

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
	void InitializeMainFastCGISocket();
	void InitializeStatsSocket();

	int InitializeUNIXSocket(const char *path, const int listen_backlog_size_);

	void SpawnWorkersAndAuxiliaryProcesses();
	void SpawnAuxiliaryProcess(BPSGIAuxiliaryProcess &process);
	void RunWorker(WorkerNo workerno, unique_ptr<BPSGIPerlInterpreter> interpreter, unique_ptr<BPSGIPerlCallbackFunction> main_callback);

	void SpawnMonitoringProcess();
	void RunMonitoringProcess();

	void HandleUnexpectedChildProcessDeath(const std::string process, pid_t pid, int status);
	void HandleChildProcessDeath(pid_t pid, int status);

private:
	std::vector<sigset_t>	signal_mask_stack_;

	int		argc_;
	char  **argv_;

	const char *psgi_application_path_;
	int			nworkers_;
	const char *application_loader_;
	const char *fastcgi_socket_path_;
	const char *stats_socket_path_;
	const char *process_title_prefix_;

	pid_t	runner_pid_;
	pid_t	monitoring_process_pid_;
	std::vector<pid_t> worker_pids_;
	std::vector<pid_t> auxiliary_pids_;

	std::vector<unique_ptr<BPSGIAuxiliaryProcess>> auxiliary_processes_;

	unique_ptr<BPSGISharedMemory> shmem_;
	int fastcgi_sockfd_;
	int stats_sockfd_;
};

class BPSGIMonitoring {
public:
	BPSGIMonitoring(BPSGIMainApplication *mainapp);
	int Run();

protected:
	void HandleClient(int listensockfd, int64_t bladepsgi_start_time, std::vector<char> worker_status_array);

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

class BPSGIAuxiliaryProcess {
public:
	BPSGIAuxiliaryProcess(BPSGIMainApplication *mainapp, std::string name, unique_ptr<BPSGIPerlCallbackFunction> callback);

	std::string name() const { return name_; }
	pid_t pid() const { return pid_; }
	void SetPID(pid_t pid);

	int Run();

private:
	BPSGIMainApplication *mainapp_;
	std::string name_;
	unique_ptr<BPSGIPerlCallbackFunction> callback_;

	pid_t pid_;
};


#endif
