#include "bladepsgi.hpp"

SyscallException::SyscallException(const char *syscall, int s_errno)
	: syscall_(syscall),
	  s_errno_(s_errno),
	  strerror_(NULL)
{

}

SyscallException::SyscallException(const char *syscall, const char *fmt, ...)
	: syscall_(syscall),
	  s_errno_(-1)
{
	va_list ap;
	const size_t bufsize = 4096;
	auto buf = (char *) malloc(bufsize);

	va_start(ap, fmt);
	(void) vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);

	strerror_ = buf;
}

SyscallException::~SyscallException()
{
	if (strerror_ != NULL)
	{
		free((void *) strerror_);
		strerror_ = NULL;
	}
}

const char *
SyscallException::strerror() const
{
	Assert((strerror_ != NULL) != (s_errno_ != -1));

	if (strerror_ != NULL)
		return strerror_;
	else
		return ::strerror(s_errno_);
}

RuntimeException::RuntimeException(const char *fmt, ...)
{
	va_list ap;
	const size_t bufsize = 4096;
	auto buf = (char *) malloc(bufsize);

	va_start(ap, fmt);
	(void) vsnprintf(buf, bufsize, fmt, ap);
	va_end(ap);

	error_ = buf;
}

RuntimeException::~RuntimeException()
{
	if (error_ != NULL)
	{
		free((void *) error_);
		error_ = NULL;
	}
}
