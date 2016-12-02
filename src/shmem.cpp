#include "bladepsgi.hpp"

#define		SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF		0
#define		SHMEM_REQUEST_COUNTER_OFF				SHMEM_SHOULD_EXIT_IMMEDIATELY_OFF + sizeof(int64_t)

#define		SHMEM_WORKER_STATUS_ARRAY_OFF			256

BPSGISharedMemory::BPSGISharedMemory(void *shared_memory_segment, size_t shmem_size)
	: shared_memory_segment_((char *) shared_memory_segment),
	  shmem_size_(shmem_size)
{
	Assert(shared_memory_segment_ != NULL);

	memset(shared_memory_segment, 0, shmem_size_);
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
