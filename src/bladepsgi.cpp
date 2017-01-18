#include "bladepsgi.hpp"

#include <algorithm>
#include <string>

#include <getopt.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

static unique_ptr<BPSGIMainApplication> mainapp;

/* 1 if we're in the middle of the shutdown process, 0 otherwise */
static sig_atomic_t _mainapp_shutdown = 0;
/* 1 if we should initiate fast shutdown, 0 otherwise */
static sig_atomic_t _mainapp_fast_shutdown = 0;
/* 1 if we should initiate smart shutdown, 0 otherwise */
static sig_atomic_t _mainapp_smart_shutdown = 0;
/* 1 if the next smart shutdown should be a fast one instead, 0 otherwise */
static sig_atomic_t _mainapp_force_fast_shutdown = 0;

/* self-pipe for waking the main loop up from the signal handler */
static int _overseer_self_pipe[2] = { -1, -1 };


static void
overseer_signal_handler(int sig)
{
	mainapp->HandleParentSignal(sig);
}

/*
 * N.B: THIS CODE RUNS IN A SIGNAL HANDLER!
 */
void
BPSGIMainApplication::HandleParentSignal(int sig)
{
	int save_errno = errno;

	if (sig == SIGQUIT)
	{
		/* kill everything and quit ASAP */
		KillProcessGroup(SIGQUIT);
		_exit(1);
	}
	else if (sig == SIGTERM)
	{
		_mainapp_smart_shutdown = 1;
		_mainapp_shutdown = 1;
	}
	else if (sig == SIGINT)
	{
		_mainapp_fast_shutdown = 1;
		_mainapp_shutdown = 1;
	}
	else if (sig == SIGCHLD)
	{
		/* only wake the select() up */
	}
	else
	{
		/* shouldn't happen */
		abort();
	}

	errno = 0;
	if (write(_overseer_self_pipe[1], "j", 1) != 1)
	{
		if (errno != EAGAIN)
		{
			const char *err = "write() to self pipe failed\n";
			auto written = write(STDERR_FILENO, err, strlen(err));
			(void) written;
			_exit(3);
		}
	}

	errno = save_errno;
}

BPSGIMainApplication::BPSGIMainApplication(
	int argc,
	char **argv,
	const char *psgi_application_path,
	int nworkers,
	const char *application_loader,
	const char *fastcgi_socket_path,
	const char *stats_socket_path,
	const char *opt_process_title_prefix
)
	: argc_(argc),
	  argv_(argv),
	  psgi_application_path_(psgi_application_path),
	  nworkers_(nworkers),
	  application_loader_(application_loader),
	  fastcgi_socket_path_(fastcgi_socket_path),
	  stats_socket_path_(stats_socket_path),
	  process_title_prefix_(opt_process_title_prefix),
	  runner_pid_(-1),
	  monitoring_process_pid_(-1),
	  fastcgi_sockfd_(-1)
{
	runner_pid_ = getpid();
	signal_mask_stack_.reserve(2);
}

void
BPSGIMainApplication::KillProcessGroup(int sig)
{
	Assert(sig == SIGQUIT || sig == SIGTERM);
	for (auto pid : worker_pids_)
		(void) kill(pid, sig);

	if (monitoring_process_pid_ != -1)
		(void) kill(monitoring_process_pid_, sig);
}

bool
BPSGIMainApplication::SetShouldExitImmediately()
{
	if (shmem_.get() == nullptr)
		return true;
	return shmem_->SetShouldExitImmediately();
}

void
BPSGIMainApplication::Log(LogSeverity severity, const char *fmt, ...)
{
	va_list ap;

	(void) severity;

	// TODO: Portect the actual write with a semaphore?????  I don't think
	// anyone really does that currently.  We could at least skip it in case the
	// thing is smaller than the whatever number guaranteed for write().

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n"); // TODO
}

void
BPSGIMainApplication::LogPanic(const char *fmt, ...)
{
	/* not implemented */
	(void) fmt;
	abort();
}

static bool _has_perl_interpreter = false;

/*
 * InitializePerlInterpreter initializes a Perl interpreter for the current
 * subprocess.  It should only be called once per subprocess.
 */
unique_ptr<BPSGIPerlInterpreter>
BPSGIMainApplication::InitializePerlInterpreter()
{
	if (_has_perl_interpreter)
		throw std::logic_error("InitializePerlInterpreter called but _has_perl_interpreter is already set");

	_has_perl_interpreter = true;
	BPSGIPerlInterpreter::PerProcessInit();
	return make_unique<BPSGIPerlInterpreter>(this);
}

void
BPSGIMainApplication::SubprocessInit(const char *new_process_title, BPSGISubprocessInitFlags flags)
{
	(void) flags;

#ifdef __linux__
	Assert(flags == 0 || flags == SUBP_NO_DEATHSIG);

	if (flags != SUBP_NO_DEATHSIG)
	{
		int ret = prctl(PR_SET_PDEATHSIG, (unsigned long) SIGQUIT, 0, 0);
		if (ret == -1)
		{
			/*
			 * This should never happen, but it's not really a problem either.  Log
			 * the incident but keep running as usual.
			 */
			Log(LS_WARNING, "prctl() failed: %s", strerror(errno));
		}
	}
#endif

	SetProcessTitle(new_process_title);
}

void
BPSGIMainApplication::SetSignalHandler(int signum, void (*handlerfunc)(int))
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handlerfunc;
	if (signum == SIGCHLD)
		sa.sa_flags |= SA_NOCLDSTOP;
	int ret = sigaction(signum, &sa, NULL);
	if (ret == -1)
		throw SyscallException("sigaction", errno);
}

void
BPSGIMainApplication::BlockSignals()
{
	sigset_t mask, old;
	sigfillset(&mask);
	if (sigprocmask(SIG_SETMASK, &mask, &old) == -1)
		throw SyscallException("sigprocmask", errno);
	signal_mask_stack_.push_back(old);
}

void
BPSGIMainApplication::UnblockSignals()
{
	if (signal_mask_stack_.empty())
		throw std::logic_error("UnblockSignals called but signal_mask_stack_ is empty");
	auto mask = signal_mask_stack_.front();
	signal_mask_stack_.erase(signal_mask_stack_.begin());
	sigprocmask(SIG_SETMASK, &mask, NULL);
}

void
BPSGIMainApplication::SetProcessTitle(const char *value)
{
	auto prefix = std::string(process_title_prefix_);

#ifdef __linux__
	char *end = NULL;

	// Use the entire argv space as long as it's contiguous.  We could continue
	// into environment as well, but that doesn't seem necessary.
	for (int i = 0; i < argc_; i++)
	{
		if (i == 0 || end + 1 == argv_[i])
			end = argv_[i] + strlen(argv_[i]);
	}
	Assert(end != NULL);

	size_t maxlen = end - argv_[0];
#else
	size_t maxlen = strlen(argv_[0]);
#endif

	std::string full_title = (prefix + ": " + value);
	std::string new_title = full_title.substr(0, maxlen - 1);
	new_title.resize(maxlen, '\0');
	memcpy(argv_[0], new_title.data(), new_title.length());

#ifdef __linux__
	if (prctl(PR_SET_NAME, (unsigned long) full_title.c_str(), 0, 0, 0) != 0)
		throw SyscallException("prctl", errno);
#endif
}

void
BPSGIMainApplication::InitializeSelfPipe()
{
	if (pipe(_overseer_self_pipe) == -1)
		throw SyscallException("pipe", errno);

	auto set_non_blocking = [](int fildes){
		int flags;
		flags = fcntl(fildes, F_GETFL);
		if (flags == -1)
			throw SyscallException("fcntl", errno);
		flags |= O_NONBLOCK;
		if (fcntl(fildes, F_SETFL, flags) == -1)
			throw SyscallException("fcntl", errno);
	};

	set_non_blocking(_overseer_self_pipe[0]);
	set_non_blocking(_overseer_self_pipe[1]);
}

void
BPSGIMainApplication::DrainSelfPipe()
{
	for (;;)
	{
		char buf[10];

		ssize_t ret = read(_overseer_self_pipe[0], buf, sizeof(buf));
		if (ret > 0)
			continue;
		else if (ret == -1)
		{
			if (errno == EINTR)
				continue;
			else if (errno == EAGAIN)
				break;
			else
				throw SyscallException("read", "could not read from self-pipe: %s", strerror(errno));
		}
		else
			throw SyscallException("read", "could not read from self-pipe: unexpected return value %ld", (long) ret);
	}
}

/*
 * InitializeSharedMemory initializes the shared memory segment used by BPSGI.
 * It should only be called once for the entire program, in the runner process.
 */
void
BPSGIMainApplication::InitializeSharedMemory()
{
	Assert(shmem_ == NULL);

	const size_t shmem_size = 4096;

	void *mem = mmap(NULL, shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mem == NULL)
		throw SyscallException("mmap", errno);
	shmem_ = make_unique<BPSGISharedMemory>(mem, shmem_size);
}

/*
 * InitializeMainFastCGISocket initializes a socket for serving FastCGI
 * requests.  It should only be called once for the entire program, in the
 * runner process.
 */
void
BPSGIMainApplication::InitializeMainFastCGISocket()
{
	const int listen_backlog_size_ = 16384;

	fastcgi_sockfd_ = InitializeUNIXSocket(fastcgi_socket_path_, listen_backlog_size_);
}

void
BPSGIMainApplication::InitializeStatsSocket()
{
	const int listen_backlog_size_ = 64;

	stats_sockfd_ = InitializeUNIXSocket(stats_socket_path_, listen_backlog_size_);
}

int
BPSGIMainApplication::InitializeUNIXSocket(const char *path, const int listen_backlog_size_)
{
	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd == -1)
		throw SyscallException("socket", errno);

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	(void) snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	struct stat st;
	if (stat(addr.sun_path, &st) == 0)
	{
		if (!S_ISSOCK(st.st_mode))
			throw RuntimeException("file %s already exists and is not a socket", addr.sun_path);
		if (unlink(addr.sun_path) == -1)
			throw RuntimeException("could not remove socket file %s: %s", addr.sun_path, strerror(errno));
	}

	if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1)
		throw SyscallException("bind", errno);
	if (listen(sockfd, listen_backlog_size_) == -1)
		throw SyscallException("listen", errno);

	return sockfd;
}

void
BPSGIMainApplication::SpawnAuxiliaryProcess(BPSGIAuxiliaryProcess &process)
{
	pid_t pid = fork();
	if (pid == -1)
		throw SyscallException("fork", errno);
	else if (pid == 0)
	{
		process.SetPID(getpid());
		process.Run();
		abort();
	}
	else if (pid > 0)
	{
		process.SetPID(pid);
		auxiliary_pids_.push_back(pid);
	}
	else
		throw SyscallException("fork", "unexpected return value %ld", (long) pid);
}

void
BPSGIMainApplication::RunWorker(WorkerNo workerno, unique_ptr<BPSGIPerlInterpreter> interpreter, unique_ptr<BPSGIPerlCallbackFunction> main_callback)
{
	BPSGIWorker worker(this, workerno);

	interpreter->WorkerInitialize(&worker);

	int ret;
	try {
		ret = worker.Run(*main_callback);
	} catch (const std::exception &ex) {
		// TODO
		abort();
	}
	_exit(ret);
}

// fastcgi_wrapper_loader.cpp
extern const char *fastcgi_wrapper_loader;

void
BPSGIMainApplication::SpawnWorkersAndAuxiliaryProcesses()
{
	unique_ptr<BPSGIPerlInterpreter> interpreter;
	unique_ptr<BPSGIPerlCallbackFunction> wrapper_loader_callback;
	unique_ptr<BPSGIPerlCallbackFunction> auxiliary_loader_callback;
	unique_ptr<BPSGIPerlCallbackFunction> main_callback;

	try {
		interpreter = InitializePerlInterpreter();
	} catch (const PerlInterpreterException &ex) {
		Log(LS_ERROR, "Could not initialize Perl interpreter: %s", ex.strerror());
		_exit(1);
	}
	try {
		wrapper_loader_callback = interpreter->LoadCallbackFromCString(fastcgi_wrapper_loader);
	} catch (const PerlInterpreterException &ex) {
		/* TODO: ??? */
		Log(LS_ERROR, "Could not initialize PSGI application: %s", ex.strerror());
		_exit(1);
	}

	try {
		main_callback = wrapper_loader_callback->CallAndReceiveCallback();
	} catch (const PerlInterpreterException &ex) {
		Log(LS_ERROR, "Could not initialize PSGI callback: %s", ex.strerror());
		_exit(1);
	}

	shmem_->LockAllocations();

	for (auto && process : auxiliary_processes_)
		SpawnAuxiliaryProcess(*process);

	worker_pids_.reserve(nworkers_);

	for (WorkerNo workerno = 0; workerno < nworkers_; ++workerno)
	{
		pid_t pid = fork();
		if (pid == -1)
			throw SyscallException("fork", errno);
		else if (pid == 0)
		{
			RunWorker(workerno, std::move(interpreter), std::move(main_callback));
			abort();
		}
		else if (pid > 0)
		{
			worker_pids_.push_back(pid);
			Assert(worker_pids_[(int) workerno] == pid);
		}
		else
			throw SyscallException("fork", "unexpected return value %ld", (long) pid);
	}

	main_callback.release();
	interpreter->Destroy();
}


void
BPSGIMainApplication::RequestAuxiliaryProcess(std::string name, unique_ptr<BPSGIPerlCallbackFunction> callback)
{
	auxiliary_processes_.push_back(make_unique<BPSGIAuxiliaryProcess>(this, name, std::move(callback)));
}

void
BPSGIMainApplication::SpawnMonitoringProcess()
{
	pid_t pid = fork();
	if (pid == -1)
		throw SyscallException("fork", errno);
	else if (pid == 0)
		RunMonitoringProcess();
	else if (pid > 0)
		monitoring_process_pid_ = pid;
	else
		throw SyscallException("fork", "unexpected return value %ld", (long) pid);

	close(stats_sockfd_);
}

void
BPSGIMainApplication::RunMonitoringProcess()
{
	BPSGIMonitoring monitoring(this);

	int ret;
	try {
		ret = monitoring.Run();
	} catch (const std::exception &ex) {
		// TODO
		abort();
	}
	_exit(ret);
}

void
BPSGIMainApplication::HandleUnexpectedChildProcessDeath(const std::string process, pid_t pid, int status)
{
	if (shmem()->SetShouldExitImmediately())
	{
		if (WIFSIGNALED(status))
			Log(LS_PANIC, "%s (pid %ld) died to signal %d", process.c_str(), (long) pid, WTERMSIG(status));
		else
			Log(LS_PANIC, "%s (pid %ld) exited with code %d", process.c_str(), (long) pid, WEXITSTATUS(status));
	}
	KillProcessGroup(SIGQUIT);
	_exit(1);
}

void
BPSGIMainApplication::HandleChildProcessDeath(pid_t pid, int status)
{
	auto witer = std::find(worker_pids_.begin(), worker_pids_.end(), pid);
	if (witer != worker_pids_.end())
	{
		if (_mainapp_shutdown == 0)
			HandleUnexpectedChildProcessDeath("worker process", pid, status);
		worker_pids_.erase(witer);
		return;
	}

	auto auxiter = std::find(auxiliary_pids_.begin(), auxiliary_pids_.end(), pid);
	if (auxiter != auxiliary_pids_.end())
	{
		if (_mainapp_shutdown == 0)
		{
			auto prname = std::string("auxiliary process ");

			auto found = false;
			for (auto && process : auxiliary_processes_)
			{
				if (process->pid() == pid)
				{
					prname += process->name();
					found = true;
					break;
				}
			}
			if (!found)
				abort();

			HandleUnexpectedChildProcessDeath(prname, pid, status);
		}
		auxiliary_pids_.erase(auxiter);
		return;
	}

	if (pid == monitoring_process_pid_)
	{
		if (_mainapp_shutdown == 0)
			HandleUnexpectedChildProcessDeath("monitoring process", pid, status);
		monitoring_process_pid_ = -1;
		return;
	}

	(void) shmem()->SetShouldExitImmediately();
	Log(LS_FATAL, "unknown child process %ld exited with code %d", (long) pid, WEXITSTATUS(status));
	_exit(1);
}

int
BPSGIMainApplication::Run()
{
	BlockSignals();
	InitializeSelfPipe();
	InitializeSharedMemory();
	InitializeMainFastCGISocket();
	InitializeStatsSocket();
	SpawnWorkersAndAuxiliaryProcesses();
	SpawnMonitoringProcess();
	SetSignalHandler(SIGCHLD, overseer_signal_handler);
	SetSignalHandler(SIGINT, overseer_signal_handler);
	SetSignalHandler(SIGTERM, overseer_signal_handler);
	SetSignalHandler(SIGQUIT, overseer_signal_handler);
	UnblockSignals();

	SetProcessTitle("overseer");

	for (;;)
	{
		struct timeval tv;
		fd_set fds;
		int nfds;

		memset(&tv, 0, sizeof(tv));
		tv.tv_sec = 3;
		tv.tv_usec = 0;

		FD_ZERO(&fds);
		FD_SET(_overseer_self_pipe[0], &fds);

		nfds = _overseer_self_pipe[0] + 1;

		errno = 0;
		int ret = select(nfds, &fds, NULL, NULL, &tv);
		if (ret == 0)
		{
			/* OK, but check for children anyway */
		}
		else if (ret == -1)
		{
			if (errno != EINTR)
				throw SyscallException("select", errno);
		}
		else if (ret == 1)
		{
			/*
			 * We got woken up because the self-pipe had data in it.  Drain it
			 * and continue downwards to see whether any child processes died.
			 */
			DrainSelfPipe();
		}
		else
			throw SyscallException("select", "unexpected return value %d", ret);

		if (_mainapp_fast_shutdown == 1)
		{
			KillProcessGroup(SIGQUIT);
			_mainapp_fast_shutdown = 0;
		}
		else if (_mainapp_smart_shutdown == 1)
		{
			KillProcessGroup(SIGTERM);
			_mainapp_smart_shutdown = 0;
			/* two smart shutdowns means fast shutdown */
			_mainapp_force_fast_shutdown = 1;
		}

		int status;
waitagain:
		pid_t child = waitpid((pid_t) -1, &status, WNOHANG);
		if (child == -1)
		{
			if (errno == ECHILD)
			{
				/*
				 * All child processes have died.  We're finally free.
				 */
				exit(0);
			}
			else if (errno != EINTR)
				throw SyscallException("waitpid", errno);

			goto waitagain;
		}
		else if (child == 0)
		{
			continue;
		}
		else
		{
			HandleChildProcessDeath(child, status);
			goto waitagain;
		}
	}
	return 1;
}

bool
BPSGIMainApplication::RunnerDied()
{
	return getppid() != runner_pid_;
}

void
BPSGIMainApplication::SetWorkerStatus(WorkerNo workerno, int_fast8_t status)
{
	std::atomic_store(shmem_->WorkerArraySegment() + (ptrdiff_t) workerno, status);
}

/* command line options */
void
print_usage(FILE *fh, const char *argv0)
{
	fprintf(fh, "%s exposes a PSGI application over FastCGI using a process-based architecture\n", argv0);
	fprintf(fh, "\n");
	fprintf(fh, "Usage:\n");
	fprintf(fh, "  %s [OPTION]... APPLICATION_PATH NUM_WORKERS FASTCGI_SOCKET_PATH STATS_SOCKET_PATH\n", argv0);
	fprintf(fh, "\n");
	fprintf(fh, "Arguments:\n");
	fprintf(fh, "  APPLICATION_PATH             filesystem path to the PSGI application\n");
	fprintf(fh, "  NUM_WORKERS                  the number of workers processes to spawn\n");
	fprintf(fh, "  FASTCGI_SOCKET_PATH          the file system path at which to create the FastCGI socket\n");
	fprintf(fh, "  STATS_SOCKET_PATH            the file system path at which to create the statistics socket\n");
	fprintf(fh, "\n");
	fprintf(fh, "Options\n");
	fprintf(fh, "  --loader=LOADER              uses the Perl module LOADER as a loader\n");
	fprintf(fh, "  --proctitle-prefix=PREFIX    sets the prefix used for process titles\n");
	fprintf(fh, "\n");
}

int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"help", no_argument, NULL, '?'},
		{"version", no_argument, NULL, 'v'},
		{"loader", required_argument, NULL, 'l'},
		{"proctitle-prefix", required_argument, NULL, 'p'},
		{NULL, 0, NULL, 0}
	};

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 ||
			strcmp(argv[1], "-?") == 0 ||
			strcmp(argv[1], "-h") == 0)
		{
			print_usage(stdout, argv[0]);
			exit(0);
		}
		else if (strcmp(argv[1], "-v") == 0 ||
				 strcmp(argv[1], "--version") == 0)
		{
			puts("bladepsgi version 1.0alpha1");
			exit(0);
		}
	}

	const char *opt_application_loader = NULL;
	const char *opt_process_title_prefix = "Blade";

	int c, option_index;
	while ((c = getopt_long(argc, argv, "l:p:hv",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case '?':
				print_usage(stdout, argv[0]);
				exit(0);
			case 'l':
				opt_application_loader = strdup(optarg);
				break;
			case 'p':
				opt_process_title_prefix = strdup(optarg);
				break;
			default:
				/*
				 * getopt_long already printed an error
				 */
				fprintf(stderr, "Try \"%s --help\" for more information.\n", argv[0]);
				exit(1);
		}
	}

	if (optind != argc - 4)
	{
		print_usage(stderr, argv[0]);
		exit(1);
	}

	auto psgi_application_path = argv[argc - 4];
	auto nworkers_str = argv[argc - 3];
	char *endptr;
	long nworkers = strtol(nworkers_str, &endptr, 10);
	if (*endptr != '\0')
	{
		fprintf(stderr, "NUM_WORKERS value \"%s\" is not a valid integer\n\n", nworkers_str);
		print_usage(stderr, argv[0]);
		exit(1);
	}
	else if (nworkers <= 0 || nworkers > 65536)
	{
		fprintf(stderr, "the number of workers must be between 1 and 65536\n");
		exit(1);
	}
	auto fastcgi_socket_path = argv[argc - 2];
	auto stats_socket_path = argv[argc - 1];

	mainapp = make_unique<BPSGIMainApplication>(
		argc,
		argv,
		psgi_application_path,
		nworkers,
		opt_application_loader,
		fastcgi_socket_path,
		stats_socket_path,
		opt_process_title_prefix
	);

	try
	{
		mainapp->Run();
	}
	catch (const SyscallException &ex)
	{
		if (mainapp->SetShouldExitImmediately())
			mainapp->Log(LS_FATAL, "system call %s failed: %s", ex.syscall(), ex.strerror());
	}
	catch (const RuntimeException &ex)
	{
		if (mainapp->SetShouldExitImmediately())
			mainapp->Log(LS_FATAL, "%s", ex.error());
	}

	mainapp->KillProcessGroup(SIGQUIT);
	_exit(1);
}
