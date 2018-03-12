#pragma once

#include "Fiber.hpp"
#include "Lock.hpp"

class FiberPool
{
private:
	Fiber * m_fibers;
	Fiber** m_ptrs;
	fcontext_stack_t* m_stacks;

	uint16_t m_maxFibers;
	int32_t m_index;

	Lock m_lock;

public:
	FiberPool()
	{
		m_fibers = nullptr;
		m_stacks = nullptr;
		m_maxFibers = 0;
		m_index = 0;
		m_ptrs = nullptr;
	}

	bool create(uint16_t maxFibers, uint32_t stackSize);

	void destroy();

	Fiber* newFiber(JobCallback callbackFn, void* userData, uint16_t index, JobPriority::Enum priority, FiberPool* pool, JobCounter* counter);

	void deleteFiber(Fiber* fiber);

	inline uint16_t getMax() const
	{
		return m_maxFibers;
	}
};
