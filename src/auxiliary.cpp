#include "bladepsgi.hpp"

#include <unistd.h>

static void
auxiliary_sigquit_handler(int _unused)
{
	/* behave like worker_sigquit_handler */
	(void) _unused;
	_exit(2);
}

BPSGIAuxiliaryProcess::BPSGIAuxiliaryProcess(BPSGIMainApplication *mainapp, std::string name, unique_ptr<BPSGIPerlCallbackFunction> callback)
	: mainapp_(mainapp),
	  name_(name),
	  callback_(std::move(callback)),
	  pid_(-1)
{
}

void
BPSGIAuxiliaryProcess::SetPID(pid_t pid)
{
	Assert(pid != -1);

	if (pid_ != -1)
		throw std::logic_error("pid set more than once for auxiliary process " + name_);
	pid_ = pid;
}

int
BPSGIAuxiliaryProcess::Run()
{
	mainapp_->SubprocessInit(name_.c_str(), SUBP_DEFAULT_FLAGS);
	mainapp_->SetSignalHandler(SIGCHLD, SIG_IGN);
	mainapp_->SetSignalHandler(SIGINT, SIG_DFL);
	mainapp_->SetSignalHandler(SIGTERM, SIG_DFL);
	mainapp_->SetSignalHandler(SIGQUIT, auxiliary_sigquit_handler);
	mainapp_->UnblockSignals();

	try {
		callback_->Call();
		mainapp_->Log(LS_ERROR, "Auxiliary process %s returned unexpectedly", name_.c_str());
	} catch (const PerlInterpreterException &ex) {
		mainapp_->Log(LS_ERROR, "Auxiliary process died with a Perl exception: %s", ex.strerror());
		_exit(1);
	}
	abort();
}
