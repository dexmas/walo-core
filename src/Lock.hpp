#pragma once

#include <stdint.h>

#include "Platform.hpp"
#include "Thread.hpp"

#ifdef WALO_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#ifdef WALO_COMPILER_MSVC
extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)
#endif

inline int32_t atomicFetchAndAdd(volatile int32_t* _ptr, int32_t _add)
{
#ifdef WALO_COMPILER_MSVC
	return InterlockedExchangeAdd((volatile long*)_ptr, _add);
#else
	return __sync_fetch_and_add(_ptr, _add);
#endif
}

inline int64_t atomicFetchAndAdd(volatile int64_t* _ptr, int64_t _add)
{
#ifdef WALO_COMPILER_MSVC
	return InterlockedExchangeAdd64((volatile int64_t*)_ptr, _add);
#else
	return __sync_fetch_and_add(_ptr, _add);
#endif
}

inline int64_t atomicCompareAndSwap(volatile int64_t* _ptr, int64_t _old, int64_t _new)
{
#ifdef WALO_COMPILER_MSVC
	return _InterlockedCompareExchange64((volatile LONG64*)(_ptr), _new, _old);
#else
	return __sync_val_compare_and_swap((volatile int64_t*)_ptr, _old, _new);
#endif
}

inline int32_t atomicFetchAndSub(volatile int32_t* _ptr, int32_t _sub)
{
#ifdef WALO_COMPILER_MSVC
	return atomicFetchAndAdd(_ptr, -_sub);
#else
	return __sync_fetch_and_sub(_ptr, _sub);
#endif // BX_COMPILER_
}

inline void readWriteBarrier()
{
#ifdef WALO_COMPILER_MSVC
	_ReadWriteBarrier();
#else
	asm volatile("":::"memory");
#endif
}

class Lock
{
public:
	Lock()
	{
	}

	void lock()
	{
		int32_t me = atomicFetchAndAdd(&m_data.s.users, 1);
		while(m_data.s.ticket != me)
			Thread::yield();
	}

	void unlock()
	{
		readWriteBarrier();
		m_data.s.ticket++;
	}

	bool tryLock()
	{
		int32_t me = m_data.s.users;
		int32_t menew = me + 1;
		int64_t cmp = (int64_t(me) << 32) + me;
		int64_t cmpnew = (int64_t(menew) << 32) + me;

		if(atomicCompareAndSwap(&m_data.u, cmp, cmpnew) == cmp)
			return true;

		return false;
	}

	bool canLock()
	{
		Data d = m_data;
		readWriteBarrier();
		return d.s.ticket == d.s.users;
	}

private:
	union Data
	{
		volatile int64_t u;
		struct
		{
			volatile int32_t ticket;
			volatile int32_t users;
		} s;

		Data()
		{
			this->u = 0;
		}

		Data(const Data& d)
		{
			this->u = d.u;
		}

		Data& operator=(const Data& d)
		{
			this->u = d.u;
			return *this;
		}
	};

	Data m_data;
};

class LockScope
{
public:
	explicit LockScope(Lock& _lock): m_lock(_lock)
	{
		m_lock.lock();
	}

	~LockScope()
	{
		m_lock.unlock();
	}

private:
	Lock & m_lock;
};