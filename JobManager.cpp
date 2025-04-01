#include "JobManager.h"

#include <atomic>
#include <Windows.h>

#pragma comment(lib, "Synchronization.lib")

SrwLock::SrwLock()
{
	InitializeSRWLock((PSRWLOCK)&impl);
}

void SrwLock::lockRead()
{
	AcquireSRWLockShared((PSRWLOCK)&impl);
}

void SrwLock::lockWrite()
{
	AcquireSRWLockExclusive((PSRWLOCK)&impl);
}

void SrwLock::unlockRead()
{
	ReleaseSRWLockShared((PSRWLOCK)&impl);
}

void SrwLock::unlockWrite()
{
	ReleaseSRWLockExclusive((PSRWLOCK)&impl);
}

void Signal::wait()
{
	i32 undesired = 0;

	volatile i32* address = &flag;
	while (*address == undesired)
	{
		WaitOnAddress(address, &undesired, sizeof(i32), INFINITE);
	}
}

bool Signal::isSet()
{
	return flag == 1;
}

void Signal::set()
{
	i32* address = &flag;
	*address = 1;
	WakeByAddressSingle(address);
}

JobManager::JobManager(Allocator& a)
	: m_allocator(a)
	, m_threads(a)
	, m_jobQueue(a)
{

}
