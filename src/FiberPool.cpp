#include "FiberPool.hpp"
#include "JobDispatcher.hpp"

extern JobDispatcher* g_dispatcher;

static void fiberCallback(fcontext_transfer_t transfer)
{
	Fiber* fiber = (Fiber*)transfer.data;
	ThreadData* data = (ThreadData*)g_dispatcher->threadData.get();

	data->running = fiber;

	// Call user task callback
	fiber->callback(fiber->jobIndex, fiber->userData);

	// Job is finished
	atomicFetchAndSub(fiber->counter, 1);

	data->running = nullptr;

	// Delete the fiber
	fiber->ownerPool->deleteFiber(fiber);

	// Go back
	jump_fcontext(transfer.ctx, transfer.data);
}

bool FiberPool::create(uint16_t maxFibers, uint32_t stackSize)
{
	// Create pool structure
	size_t totalSize = sizeof(Fiber)*maxFibers + sizeof(Fiber*)*maxFibers + sizeof(fcontext_stack_t)*maxFibers;

	uint8_t* buff = (uint8_t*)malloc(totalSize);
	if(!buff)
		return false;

	memset(buff, 0x00, totalSize);

	m_fibers = (Fiber*)buff;
	buff += sizeof(Fiber)*maxFibers;
	m_ptrs = (Fiber**)buff;
	buff += sizeof(Fiber*)*maxFibers;
	m_stacks = (fcontext_stack_t*)buff;

	for(uint16_t i = 0; i < maxFibers; i++)
		m_ptrs[maxFibers - i - 1] = &m_fibers[i];
	m_maxFibers = maxFibers;
	m_index = maxFibers;

	// Create contexts and their stack memories
	for(uint16_t i = 0; i < maxFibers; i++)
	{
		m_stacks[i] = create_fcontext_stack(stackSize);
		m_fibers[i].stackIndex = i;
		if(!m_stacks[i].sptr)
			return false;
	}

	return true;
}

void FiberPool::destroy()
{
	for(uint16_t i = 0; i < m_maxFibers; i++)
	{
		if(m_stacks[i].sptr)
			destroy_fcontext_stack(&m_stacks[i]);
	}

	// Free the whole buffer (context+ptrs+stacks)
	if(m_fibers)
		free(m_fibers);
}

Fiber* FiberPool::newFiber(JobCallback callbackFn, void* userData, uint16_t index, JobPriority::Enum priority, FiberPool* pool, JobCounter* counter)
{
	LockScope lk(m_lock);
	if(m_index > 0)
	{
		Fiber* fiber = new(m_ptrs[--m_index]) Fiber();
		fiber->ownerThread = 0;
		fiber->context = make_fcontext(m_stacks[fiber->stackIndex].sptr, m_stacks[fiber->stackIndex].ssize, fiberCallback);
		fiber->callback = callbackFn;
		fiber->userData = userData;
		fiber->jobIndex = index;
		fiber->waitCounter = &g_dispatcher->dummyCounter;
		fiber->counter = counter;
		fiber->priority = priority;
		fiber->ownerPool = pool;
		return fiber;
	}
	else
	{
		return nullptr;
	}
}

void FiberPool::deleteFiber(Fiber* fiber)
{
	LockScope lk(m_lock);
	m_ptrs[m_index++] = fiber;
}