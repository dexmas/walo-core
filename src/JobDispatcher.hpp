#pragma once

#include "Thread.hpp"
#include "FiberPool.hpp"
#include "Pool.hpp"

#define DEFAULT_MAX_SMALL_FIBERS 128
#define DEFAULT_MAX_BIG_FIBERS 32
#define DEFAULT_SMALL_STACKSIZE 65536   // 64kb
#define DEFAULT_BIG_STACKSIZE 524288   // 512kb

#define MAX_WAIT_STACKS 32
#define WAIT_STACK_SIZE 8192    // 8kb

struct ThreadData
{
	Fiber* running;     // Current running fiber
	fcontext_stack_t stacks[MAX_WAIT_STACKS];
	int stackIdx;
	bool main;
	uint32_t threadId;

	ThreadData()
	{
		running = nullptr;
		stackIdx = 0;
		main = false;
		threadId = 0;
		memset(stacks, 0x00, sizeof(stacks));
	}
};

struct JobDesc
{
	JobCallback callback;
	JobPriority::Enum priority;
	void* userParam;

	JobDesc()
	{
		callback = nullptr;
		priority = JobPriority::Normal;
		userParam = nullptr;
	}

	explicit JobDesc(JobCallback _callback, void* _userParam = nullptr, JobPriority::Enum _priority = JobPriority::Normal)
	{
		callback = _callback;
		userParam = _userParam;
		priority = _priority;
	}
};

struct JobDispatcher
{
	Thread** threads;
	uint8_t numThreads;
	FiberPool smallFibers;
	FiberPool bigFibers;

	List<Fiber*> waitList[JobPriority::Count];  // 3 lists for each priority
	Lock jobLock;
	Lock counterLock;
	TlsData threadData;
	volatile int32_t stop;

	fcontext_stack_t mainStack;
	FixedPool<CounterContainer> counterPool;
	JobCounter dummyCounter;

	Semaphore semaphore;

	JobDispatcher()
	{
		threads = nullptr;
		numThreads = 0;
		stop = 0;
		dummyCounter = 0;
		memset(&mainStack, 0x00, sizeof(mainStack));
	}
};

bool initJobDispatcher();
void shutdownJobDispatcher();
JobHandle dispatchSmallJobs(const JobDesc* jobs, uint16_t numJobs);
JobHandle dispatchBigJobs(const JobDesc* jobs, uint16_t numJobs);
void waitJobs(JobHandle handle);