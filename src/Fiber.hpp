#pragma once

#include <stdint.h>

#include "fcontext.h"
#include "List.hpp"

struct JobPriority
{
	enum Enum
	{
		High = 0,
		Normal,
		Low,
		Count
	};
};

typedef volatile int32_t JobCounter;
typedef JobCounter* JobHandle;

struct CounterContainer
{
	JobCounter counter;
};

typedef void(*JobCallback)(int jobIndex, void* userParam);

class FiberPool;

struct Fiber
{
	typedef List<Fiber*>::Node LNode;

	uint32_t ownerThread;    // by default, owner thread is 0, which indicates that thread owns this fiber
							 // If we wait on a job (fiber), owner thread gets a valid value
	uint16_t jobIndex;
	uint16_t stackIndex;
	JobCounter* counter;
	JobCounter* waitCounter;
	fcontext_t context;
	FiberPool* ownerPool;

	LNode lnode;

	JobCallback callback;
	JobPriority::Enum priority;
	void* userData;

	Fiber():lnode(this)
	{
	}
};