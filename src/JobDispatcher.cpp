#include <thread>

#include <malloc.h>
#include <memory>

#include "JobDispatcher.hpp"

JobDispatcher* g_dispatcher = nullptr;

static ThreadData* createThreadData(uint32_t threadId, bool main)
{
	ThreadData* data = new ThreadData();
	if(!data)
		return nullptr;
	data->main = main;
	data->threadId = threadId;
	memset(data->stacks, 0x00, sizeof(fcontext_stack_t)*MAX_WAIT_STACKS);

	for(int i = 0; i < MAX_WAIT_STACKS; i++)
	{
		data->stacks[i] = create_fcontext_stack(WAIT_STACK_SIZE);
		if(!data->stacks[i].sptr)
			return nullptr;
	}

	return data;
}

static void destroyThreadData(ThreadData* data)
{
	for(int i = 0; i < MAX_WAIT_STACKS; i++)
	{
		if(data->stacks[i].sptr)
			destroy_fcontext_stack(&data->stacks[i]);
	}
	delete data;
}

static fcontext_stack_t* pushWaitStack(ThreadData* data)
{
	if(data->stackIdx == MAX_WAIT_STACKS)
		return nullptr;
	return &data->stacks[data->stackIdx++];
}

static fcontext_stack_t* popWaitStack(ThreadData* data)
{
	if(data->stackIdx == 0)
		return nullptr;
	return &data->stacks[--data->stackIdx];
}

static void jobPusherCallback(fcontext_transfer_t transfer)
{
	ThreadData* data = (ThreadData*)transfer.data;

	while(!g_dispatcher->stop)
	{
		// Wait for a job to be placed in the job queue
		if(!data->main)
			g_dispatcher->semaphore.wait();     // Decreases list counter on continue

		Fiber* fiber = nullptr;
		bool listNotEmpty = false;
		g_dispatcher->jobLock.lock();
		{
			for(int i = 0; i < JobPriority::Count && !fiber; i++)
			{
				List<Fiber*>& list = g_dispatcher->waitList[i];
				Fiber::LNode* node = list.getFirst();
				while(node)
				{
					listNotEmpty = true;
					Fiber* f = node->data;
					if(*f->waitCounter == 0)
					{     // Fiber is not waiting for any child tasks
						if(f->ownerThread == 0 || f->ownerThread == data->threadId)
						{
							// Job is ready to run, pull it from the wait list
							fiber = f;
							list.remove(node);
							break;
						}
					}
					node = node->next;
				}
			}
		}
		g_dispatcher->jobLock.unlock();

		if(fiber)
		{
			// If jobPusher is called within another 'Wait' call, get back to it's context, which should be the same working thread
			// Else just run the job from beginning
			if(fiber->ownerThread)
			{
				fiber->ownerThread = 0;
				jump_fcontext(transfer.ctx, transfer.data);
			}
			else
			{
				jump_fcontext(fiber->context, fiber);
			}
		}
		else if(!data->main && listNotEmpty)
		{
			g_dispatcher->semaphore.post(); // Increase the list counter because we didn't pull any jobs
		}

		// In the main thread, we have to quit the loop and get back to the caller
		if(data->main)
			break;
	}

	// Back to main thread func
	jump_fcontext(transfer.ctx, transfer.data);
}

static int32_t threadFunc(void* userData)
{
	// Initialize thread data
	ThreadData* data = createThreadData(Thread::getTid(), false);
	if(!data)
		return -1;
	g_dispatcher->threadData.set(data);

	fcontext_stack_t* stack = pushWaitStack(data);
	fcontext_t threadCtx = make_fcontext(stack->sptr, stack->ssize, jobPusherCallback);
	jump_fcontext(threadCtx, data);

	destroyThreadData(data);
	return 0;
}

bool initJobDispatcher()
{
	if(g_dispatcher)
	{
		return false;
	}
	g_dispatcher = new JobDispatcher();
	if(!g_dispatcher)
		return false;

	// Main thread data and stack
	g_dispatcher->mainStack = create_fcontext_stack(8 * 1024);
	if(!g_dispatcher->mainStack.sptr)
	{
		return false;
	}

	ThreadData* mainData = createThreadData(Thread::getTid(), true);
	if(!mainData)
		return false;
	g_dispatcher->threadData.set(mainData);

	// Create fibers with stack memories
	uint32_t maxSmallFibers = DEFAULT_MAX_SMALL_FIBERS;
	uint32_t maxBigFibers = DEFAULT_MAX_BIG_FIBERS;
	uint32_t smallFiberStackSize = DEFAULT_SMALL_STACKSIZE;
	uint32_t bigFiberStackSize = DEFAULT_BIG_STACKSIZE;

	if(!g_dispatcher->counterPool.create(maxSmallFibers + maxBigFibers) ||
		!g_dispatcher->bigFibers.create(maxBigFibers, bigFiberStackSize) ||
		!g_dispatcher->smallFibers.create(maxSmallFibers, smallFiberStackSize))
	{
		return false;
	}

	// Create threads

	uint32_t numCores = std::thread::hardware_concurrency();
	uint32_t numWorkerThreads = min(numCores ? (numCores - 1) : 0, UINT8_MAX);

	if(numWorkerThreads > 0)
	{
		g_dispatcher->threads = (Thread**)malloc(sizeof(Thread*)*numWorkerThreads);

		g_dispatcher->numThreads = numWorkerThreads;
		for(uint8_t i = 0; i < numWorkerThreads; i++)
		{
			g_dispatcher->threads[i] = new Thread();
			g_dispatcher->threads[i]->init(threadFunc, nullptr, 8 * 1024);
		}
	}
	return true;
}

void shutdownJobDispatcher()
{
	if(!g_dispatcher)
		return;

	// Command all worker threads to stop
	g_dispatcher->stop = 1;
	g_dispatcher->semaphore.post(g_dispatcher->numThreads + 1);
	for(uint8_t i = 0; i < g_dispatcher->numThreads; i++)
	{
		g_dispatcher->threads[i]->shutdown();
		delete g_dispatcher->threads[i];
	}
	free(g_dispatcher->threads);

	ThreadData* data = (ThreadData*)g_dispatcher->threadData.get();
	destroyThreadData(data);

	g_dispatcher->bigFibers.destroy();
	g_dispatcher->smallFibers.destroy();
	destroy_fcontext_stack(&g_dispatcher->mainStack);

	g_dispatcher->counterPool.destroy();

	delete g_dispatcher;
	g_dispatcher = nullptr;
}

static JobHandle dispatch(const JobDesc* jobs, uint16_t numJobs, FiberPool* pool)
{
	// Get dispatcher counter to assign to jobs
	ThreadData* data = (ThreadData*)g_dispatcher->threadData.get();

	// Get a counter
	g_dispatcher->counterLock.lock();
	JobCounter* counter = &g_dispatcher->counterPool.newInstance()->counter;
	g_dispatcher->counterLock.unlock();
	if(!counter)
	{
		return nullptr;
	}

	// Create N Fibers/Job
	uint32_t count = 0;
	Fiber** fibers = (Fiber**)alloca(sizeof(Fiber*)*numJobs);

	for(uint16_t i = 0; i < numJobs; i++)
	{
		Fiber* fiber = pool->newFiber(jobs[i].callback, jobs[i].userParam, i, jobs[i].priority, pool, counter);
		if(fiber)
		{
			fibers[i] = fiber;
			count++;
		}
	}

	*counter = count;
	if(data->running)
		data->running->waitCounter = counter;

	g_dispatcher->jobLock.lock();
	for(uint32_t i = 0; i < count; i++)
		g_dispatcher->waitList[fibers[i]->priority].addToEnd(&fibers[i]->lnode);
	g_dispatcher->jobLock.unlock();

	// post to semaphore so worker threads can continue and fetch them
	g_dispatcher->semaphore.post(count);
	return counter;
}

JobHandle dispatchSmallJobs(const JobDesc* jobs, uint16_t numJobs)
{
	return dispatch(jobs, numJobs, &g_dispatcher->smallFibers);
}

JobHandle dispatchBigJobs(const JobDesc* jobs, uint16_t numJobs)
{
	return dispatch(jobs, numJobs, &g_dispatcher->bigFibers);
}

void waitJobs(JobHandle handle)
{
	ThreadData* data = (ThreadData*)g_dispatcher->threadData.get();

	while(*handle > 0)
	{
		// Jump back to worker thread (current job is not finished)
		fcontext_stack_t* stack = pushWaitStack(data);
		if(!stack)
		{
			return;
		}

		fcontext_t jobPusherCtx = make_fcontext(stack->sptr, stack->ssize, jobPusherCallback);

		// This means that user has called 'waitJobs' inside another running task
		// So the task is WIP, we have to re-add it to the pending jobs so it can continue later on
		if(data->running)
		{
			Fiber* fiber = data->running;
			data->running = nullptr;
			fiber->ownerThread = data->threadId;

			g_dispatcher->jobLock.lock();
			g_dispatcher->waitList[fiber->priority].addToEnd(&fiber->lnode);
			g_dispatcher->jobLock.unlock();

			g_dispatcher->semaphore.post();
		}

		// Switch to job-pusher To see if we can process any remaining jobs
		jump_fcontext(jobPusherCtx, data);
		popWaitStack(data);
	}

	// Delete the counter
	g_dispatcher->counterLock.lock();
	g_dispatcher->counterPool.deleteInstance((CounterContainer*)handle);
	g_dispatcher->counterLock.unlock();
}