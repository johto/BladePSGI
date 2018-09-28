#include "bladepsgi.hpp"

#include <atomic>

// XXX Not sure if anything depends on this anymore..
static_assert(sizeof(int_fast64_t) == sizeof(int64_t), "sizeof(int_fast64_t) must be sizeof(int64_t)");

#define		SHMEM_ALIGNOF 16
#define		SHMEMALIGN(MEMOFF) (((MEMOFF) % SHMEM_ALIGNOF) == 0 ? (MEMOFF) : ((MEMOFF) + (SHMEM_ALIGNOF - ((MEMOFF) % SHMEM_ALIGNOF))))
#define		SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF		0
#define		SHMEM_REQUEST_COUNTER_OFF				SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF + sizeof(int64_t)

#define		SHMEM_FIRST_USER_AVAILABLE_OFFSET		SHMEMALIGN(SHMEM_REQUEST_COUNTER_OFF + sizeof(int64_t))

static_assert((SHMEM_FIRST_USER_AVAILABLE_OFFSET % SHMEM_ALIGNOF) == 0, "SHMEM_FIRST_USER_AVAILABLE_OFFSET alignment");

#define		SHMEM_WORKER_STATUS_ARRAY_OFF			2048

BPSGISemaphore::BPSGISemaphore(sem_t *sem, std::string name)
	: sem_(sem),
	  name_(name)
{
	// already initialized
}

int64_t
BPSGISemaphore::Read()
{
	int sval;
	if (sem_getvalue(sem_, &sval) != 0)
		throw SyscallException("sem_getvalue", errno);
	return (int64_t) sval;
}

void
BPSGISemaphore::Acquire()
{
again:
	if (sem_wait(sem_) != 0)
	{
		if (errno == EINTR)
			goto again;
		throw SyscallException("sem_wait", errno);
	}
}

bool
BPSGISemaphore::TryAcquire()
{
again:
	if (sem_trywait(sem_) != 0)
	{
		if (errno == EINTR)
			goto again;
		else if (errno == EAGAIN)
			return false;
		throw SyscallException("sem_trywait", errno);
	}
	return true;
}

void
BPSGISemaphore::Release()
{
	if (sem_post(sem_) != 0)
		throw SyscallException("sem_post", errno);
}

BPSGIAtomicInt64::BPSGIAtomicInt64(void *ptr, std::string name, int64_t value)
	: ptr_((std::atomic<int64_t> *) ptr),
	  name_(name)
{
	std::atomic_store(ptr_, value);
}

int64_t
BPSGIAtomicInt64::Read()
{
	return std::atomic_load(ptr_);
}

void
BPSGISharedMemory::LockAllocations()
{
	if (locked_)
		throw std::logic_error("tried to lock a previously locked shared memory segment");
	locked_ = true;
}

void *
BPSGISharedMemory::AllocateUserShmem(size_t size)
{
	if (locked_)
		throw std::string("could not allocate shared memory: shared memory has been locked");

	char *mem = shared_memory_segment_ + next_user_available_offset_;
	next_user_available_offset_ = SHMEMALIGN(next_user_available_offset_ + size);

	if (next_user_available_offset_ >= SHMEM_WORKER_STATUS_ARRAY_OFF)
		throw std::string("could not allocate shared memory: out of shared memory");
	return (void *) mem;
}

BPSGISemaphore *
BPSGISharedMemory::NewSemaphore(std::string name, int64_t value)
{
	for (auto const &sem : semaphores_)
	{
		if (sem->name() == name)
			throw std::string("semaphore with name " + name + " already exists");
	}

	if (value <= 0 || value > 2147483647)
		throw std::string("semaphore init value is outside of allowed range");

	auto ptr = (sem_t *) AllocateUserShmem(sizeof(sem_t));
	memset(ptr, 0, sizeof(sem_t));
	if (sem_init(ptr, 1, (unsigned int) value) != 0)
		throw SyscallException("sem_init", errno);
	semaphores_.push_back(make_unique<BPSGISemaphore>(ptr, name));
	return semaphores_.rbegin()->get();
}

int64_t *
BPSGISharedMemory::NewAtomicInt64(std::string name, int64_t value)
{
	for (auto const &atm : atomics_)
	{
		if (atm->name() == name)
			throw std::string("atomic integer with name " + name + " already exists");
	}

	auto ptr = AllocateUserShmem(sizeof(int64_t));
	atomics_.push_back(make_unique<BPSGIAtomicInt64>(ptr, name, value));
	return (int64_t *) ptr;
}

BPSGISharedMemory::BPSGISharedMemory(void *shared_memory_segment, size_t shmem_size)
	: shared_memory_segment_((char *) shared_memory_segment),
	  shmem_size_(shmem_size),
	  locked_(false)
{
	Assert(shared_memory_segment_ != NULL);

	memset(shared_memory_segment, 0, shmem_size_);

	next_user_available_offset_ = SHMEM_FIRST_USER_AVAILABLE_OFFSET;
}

std::atomic<int_fast8_t> *
BPSGISharedMemory::WorkerArraySegment() const
{
	return (std::atomic<int_fast8_t> *) (shared_memory_segment_ + SHMEM_WORKER_STATUS_ARRAY_OFF);
}

int_fast8_t
BPSGISharedMemory::GetWorkerStatus(WorkerNo workerno) const
{
	auto mem = WorkerArraySegment();
	return std::atomic_load(mem + (ptrdiff_t) workerno);
}

void
BPSGISharedMemory::GetAllWorkerStatuses(int nworkers, char *out) const
{
	auto mem = WorkerArraySegment();
	for (int i = 0; i < nworkers; i++)
	{
		out[i] = (char) std::atomic_load(mem);
		mem++;
	}
}

int_fast64_t
BPSGISharedMemory::IncreaseRequestCounter()
{
	auto m = (std::atomic<int_fast64_t> *) (shared_memory_segment_ + SHMEM_REQUEST_COUNTER_OFF);
	return std::atomic_fetch_add<int_fast64_t>(m, 1);
}

int_fast64_t
BPSGISharedMemory::ReadRequestCounter()
{
	auto m = (std::atomic<int_fast64_t> *) (shared_memory_segment_ + SHMEM_REQUEST_COUNTER_OFF);
	return std::atomic_load(m);
}


/*
 * If SetShouldExitImmediately returns true the caller should log why it
 * thought everyone should exit immediately.
 */
bool
BPSGISharedMemory::SetShouldExitImmediately()
{
	auto m = (std::atomic<int_fast8_t> *) (shared_memory_segment_ + SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF);
	return std::atomic_exchange(m, (int_fast8_t) 1) == (int_fast8_t) 0;
}

bool
BPSGISharedMemory::ShouldExitImmediately() const
{
	auto m = (std::atomic<int_fast8_t> *) (shared_memory_segment_ + SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF);
	return std::atomic_load(m) == (int_fast8_t) 1;
}
