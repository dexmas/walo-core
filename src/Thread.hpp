#pragma once

#include "Platform.hpp"

#ifndef WALO_PLATFORM_WINDOWS
#include <pthread.h>
#else
#include <Windows.h>
#endif

#include <stdint.h>

typedef int32_t(*ThreadFn)(void* _userData);

class Semaphore
{
public:
	Semaphore()
	{
#ifdef WALO_PLATFORM_WINDOWS
		m_handle = CreateSemaphoreA(NULL, 0, LONG_MAX, NULL);
#else
		int result;
		result = pthread_mutex_init(&m_mutex, NULL);
		result = pthread_cond_init(&m_cond, NULL);
#endif
	}


	~Semaphore()
	{
#ifdef WALO_PLATFORM_WINDOWS
		CloseHandle(m_handle);
#else
		int result;
		result = pthread_cond_destroy(&m_cond);
		result = pthread_mutex_destroy(&m_mutex);
#endif
	}

	void post(uint32_t _count = 1)
	{
#ifdef WALO_PLATFORM_WINDOWS
		ReleaseSemaphore(m_handle, _count, NULL);
#else
		int result = pthread_mutex_lock(&m_mutex);
		for(uint32_t ii = 0; ii < _count; ++ii)
		{
			result = pthread_cond_signal(&m_cond);
		}

		m_count += _count;

		result = pthread_mutex_unlock(&m_mutex);
#endif
	}

	bool wait(int32_t _msecs = -1)
	{
#ifdef WALO_PLATFORM_WINDOWS
		DWORD milliseconds = (0 > _msecs) ? INFINITE : _msecs;

		return WAIT_OBJECT_0 == WaitForSingleObject(m_handle, milliseconds);
#else
		int result = pthread_mutex_lock(&m_mutex);
		if(-1 == _msecs)
		{
			while(0 == result
				&& 0 >= m_count)
			{
				result = pthread_cond_wait(&m_cond, &m_mutex);
			}
		}
		else
		{
			timespec ts;
			ts.tv_sec = _msecs / 1000;
			ts.tv_nsec = (_msecs % 1000) * 1000;

			while(0 == result
				&& 0 >= m_count)
			{
				result = pthread_cond_timedwait_relative_np(&m_cond, &m_mutex, &ts);
			}
		}

		bool ok = 0 == result;

		if(ok)
		{
			--m_count;
		}

		result = pthread_mutex_unlock(&m_mutex);

		return ok;
#endif
	}

private:
#ifndef WALO_PLATFORM_WINDOWS
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
	int32_t m_count;
#else
	HANDLE m_handle;
#endif
};


class Thread
{
public:
	Thread()
#ifdef WALO_PLATFORM_WINDOWS
		: m_handle(INVALID_HANDLE_VALUE)
		, m_threadId(UINT32_MAX)
#else
		: m_handle(0)
#endif
		, m_fn(NULL)
		, m_userData(NULL)
		, m_stackSize(0)
		, m_exitCode(0 /*EXIT_SUCCESS*/)
		, m_running(false)
	{
	}

	virtual ~Thread()
	{
		if(m_running)
		{
			shutdown();
		}
	}

	void init(ThreadFn _fn, void* _userData = NULL, uint32_t _stackSize = 0)
	{
		m_fn = _fn;
		m_userData = _userData;
		m_stackSize = _stackSize;
		m_running = true;

#ifdef WALO_PLATFORM_WINDOWS
		m_handle = ::CreateThread(NULL, m_stackSize, (LPTHREAD_START_ROUTINE)threadFunc, this, 0, NULL);
#else
		int result;

		pthread_attr_t attr;
		result = pthread_attr_init(&attr);

		if(0 != m_stackSize)
		{
			result = pthread_attr_setstacksize(&attr, m_stackSize);
		}

		result = pthread_create(&m_handle, &attr, &threadFunc, this);
#endif

		m_sem.wait();
	}

	void shutdown()
	{
#ifdef WALO_PLATFORM_WINDOWS
		WaitForSingleObject(m_handle, INFINITE);
		GetExitCodeThread(m_handle, (DWORD*)&m_exitCode);
		CloseHandle(m_handle);
		m_handle = INVALID_HANDLE_VALUE;
#else
		union
		{
			void* ptr;
			int32_t i;
		} cast;
		pthread_join(m_handle, &cast.ptr);
		m_exitCode = cast.i;
		m_handle = 0;
#endif
		m_running = false;
	}

	bool isRunning() const
	{
		return m_running;
	}

	int32_t getExitCode() const
	{
		return m_exitCode;
	}

	static uint32_t getTid()
	{
#ifdef WALO_PLATFORM_WINDOWS
		return ::GetCurrentThreadId();
#else
		return (mach_port_t)::pthread_mach_thread_np(pthread_self());
#endif
	}

	static void yield()
	{
#ifdef WALO_PLATFORM_WINDOWS
		::SwitchToThread();
#else
		::sched_yield();
#endif
	}

private:
	int32_t entry()
	{
#ifdef WALO_PLATFORM_WINDOWS
		m_threadId = ::GetCurrentThreadId();
#endif

		m_sem.post();
		return m_fn(m_userData);
	}

#ifdef WALO_PLATFORM_WINDOWS
	static DWORD WINAPI threadFunc(LPVOID _arg)
	{
		Thread* thread = (Thread*)_arg;
		int32_t result = thread->entry();
		return result;
	}

	HANDLE m_handle;
	DWORD  m_threadId;
#else
	static void* threadFunc(void* _arg)
	{
		Thread* thread = (Thread*)_arg;
		union
		{
			void* ptr;
			int32_t i;
		} cast;
		cast.i = thread->entry();
		return cast.ptr;
	}

	pthread_t m_handle;
#endif

	ThreadFn  m_fn;
	void*     m_userData;
	Semaphore m_sem;
	uint32_t  m_stackSize;
	int32_t   m_exitCode;
	bool      m_running;
};

class TlsData
{
public:
	TlsData()
	{
#ifdef WALO_PLATFORM_WINDOWS
		m_id = TlsAlloc();
#else
		int result = pthread_key_create(&m_id, NULL);
#endif
	}

	~TlsData()
	{
#ifdef WALO_PLATFORM_WINDOWS
		BOOL result = TlsFree(m_id);
#else
		int result = pthread_key_delete(m_id);
#endif
	}

	void* get() const
	{
#ifdef WALO_PLATFORM_WINDOWS
		return TlsGetValue(m_id);
#else
		return pthread_getspecific(m_id);
#endif
	}

	void set(void* _ptr)
	{
#ifdef WALO_PLATFORM_WINDOWS
		TlsSetValue(m_id, _ptr);
#else
		int result = pthread_setspecific(m_id, _ptr);
#endif
	}

private:
#ifdef WALO_PLATFORM_WINDOWS
	uint32_t m_id;
#else
	pthread_key_t m_id;
#endif
};