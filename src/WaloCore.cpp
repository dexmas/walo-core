#include <math.h>

#include "JobDispatcher.hpp"

float k_pi = 3.14159265358979323846f;

void smallJob(int _jid, void* _usrData)
{
	float sum = 0;

	for(int i = 0; i < 1024; i++)
	{
		for(int j = 0; j < 1024; j++)
		{
			sum += sinf((k_pi / 1024.0f) * i) + cosf((k_pi / 1024.0f) * i);
		}
	}
}

void bigJob(int _jid, void* _usrData)
{
	float sum = 0;

	for(int i = 0; i < 1024; i++)
	{
		for(int j = 0; j < 1024; j++)
		{
			sum += sinf((k_pi / 1024.0f) * i) + cosf((k_pi / 1024.0f) * i);
		}
	}
}

int main()
{
	bool res = initJobDispatcher();

	if(!res)
		return 1;

	JobDesc smallJobs[128];
	JobDesc bigJobs[32];

	for(int i = 0; i < 128; i++)
	{
		smallJobs[i] = JobDesc(smallJob);
	}

	for(int i = 0; i < 32; i++)
	{
		bigJobs[i] = JobDesc(bigJob);
	}

	while(1)
	{
		int startTime = GetTickCount();

		JobHandle j_small = dispatchSmallJobs(smallJobs, 16);
		JobHandle j_big = dispatchBigJobs(bigJobs, 8);

		waitJobs(j_small);

		int time = GetTickCount() - startTime;

		printf("Finished small cycle in %dms\n", time);

		waitJobs(j_big);

		time = GetTickCount() - startTime;

		printf("Finished big cycle in %dms\n", time);
	}

	shutdownJobDispatcher();

    return 0;
}

