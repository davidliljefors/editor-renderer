#pragma once

#include "Core/Allocator.h"
#include "Core/Array.h"


extern class JobManager* g_jobManager;

class SrwLock
{
public:
	SrwLock();

	void lockRead();
	void lockWrite();

	void unlockRead();
	void unlockWrite();

private:
	void* impl;
};

class Signal
{
public:
	void wait();
	bool isSet();
	void set();

private:
	i32 flag = 0;
};

class Thread
{
	struct ThreadUserData;
	Thread(Allocator& a, const char* name);
};



using JobFunc = void(*)(void*);

void createJobManager();
void destroyJobManager();

class JobManager
{
public:
	JobManager(Allocator& a);

	struct JobData
	{
		JobFunc userFn;
		void* userData;
		Signal done;
	};

	struct WorkerThread
	{
		Thread* thread;
		Array<JobData> queue;

	};

	Signal push(JobFunc func, void* userData)
	{
		JobData* jobData = create<JobData>(m_allocator);
		jobData->userFn = func;
		jobData->userData = userData;
		jobData->done = {};

		m_queueLock.lockWrite();
		m_jobQueue.push_back(jobData);
		m_queueLock.unlockWrite();

		return jobData->done;
	}

	Allocator& m_allocator;
	Array<Thread> m_threads;
	SrwLock m_queueLock;
	Array<JobData*> m_jobQueue;
};