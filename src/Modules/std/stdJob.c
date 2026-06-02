#include "stdJob.h"
#include "Win95/std.h"
#include "stdPlatform.h"

#ifdef SDL2_RENDER

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "SDL.h"
#include "SDL_atomic.h"
#include "SDL_cpuinfo.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"

int stdJob_bInit = 0;

typedef struct stdJob
{
	stdJob_function_t function;         // The actual job function
	//void*             args;             // arguments for the function
	stdJobGroupArgs   args;
	SDL_mutex*        completionMutex;  // Mutex to protect completion flag
	SDL_cond*         completionCond;   // Condition variable to signal job completion
	SDL_atomic_t      isCompleted;      // Completion flag (1 = completed, 0 = not completed)
} stdJob;

typedef struct stdRingBuffer
{
	stdJob*          buffer;
	uint32_t         size;
	uint32_t         front;
	uint32_t         back;
	SDL_mutex*       lock;     // Mutex for thread safety
	SDL_cond*        notEmpty; // Condition variable to signal buffer is not empty
} stdRingBuffer;

typedef struct stdJobSystem
{
	uint32_t          numThreads;
	stdRingBuffer     jobPool;
	SDL_Thread**      workerThreads;
	SDL_mutex*        wakeMutex;
	SDL_cond*         wakeCondition;
	SDL_atomic_t      currentLabel;
	SDL_atomic_t      finishedLabel;
	SDL_atomic_t      quit; // lazy way to quit
} stdJobSystem;

stdJobSystem stdJob_jobSystem;

void stdJob_InitRingBuffer(stdRingBuffer* rb, uint32_t size)
{
	rb->buffer = (stdJob*)std_pHS->alloc(size * sizeof(stdJob));
	if (!rb->buffer)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Failed to allocate ring buffer\n");
		exit(EXIT_FAILURE);
	}
	memset(rb->buffer, 0, size * sizeof(stdJob));
	rb->size = size;
	rb->front = 0;
	rb->back = 0;

	// Initialize synchronization resources for each job
	rb->lock = SDL_CreateMutex();
	rb->notEmpty = SDL_CreateCond();

	// Initialize mutex and condition variable for each job
	for (uint32_t i = 0; i < size; ++i)
	{
		rb->buffer[i].completionMutex = SDL_CreateMutex();
		rb->buffer[i].completionCond = SDL_CreateCond();
	}
}

int stdJob_PushRingBuffer(stdRingBuffer* rb, stdJob_function_t job, stdJobGroupArgs* args)// void* args)
{
	int result = 0;

	SDL_LockMutex(rb->lock);

	uint32_t next_back = (rb->back + 1) % rb->size;
	if (next_back != rb->front) // Check if buffer is not full
	{
		rb->buffer[rb->back].function = job;
		rb->buffer[rb->back].args = *args;
		SDL_AtomicSet(&rb->buffer[rb->back].isCompleted, 0);
		rb->back = next_back;
		result = 1;

		SDL_CondSignal(rb->notEmpty);
	}

	SDL_UnlockMutex(rb->lock);

	return result;
}

int stdJob_PopRingBuffer(stdRingBuffer* rb, stdJob** job)
{
	int result = 0;

	SDL_LockMutex(rb->lock);

	while (rb->front == rb->back) // Wait if the buffer is empty
		SDL_CondWait(rb->notEmpty, rb->lock);

	if (rb->front != rb->back) // Buffer is not empty
	{
		*job = &rb->buffer[rb->front];
		rb->front = (rb->front + 1) % rb->size;
		result = 1;
	}

	SDL_UnlockMutex(rb->lock);

	return result;
}

void stdJob_FreeRingBuffer(stdRingBuffer* rb)
{
	// Clean up synchronization resources for each job
	for (uint32_t i = rb->front; i != rb->back; i = (i + 1) % rb->size)
	{
		stdJob* job = &rb->buffer[i];
		//job->args = NULL;
		job->args.job = NULL;
		SDL_DestroyMutex(job->completionMutex); // Cleanup mutex
		SDL_DestroyCond(job->completionCond);   // Cleanup condition variable
	}

	if(rb->buffer)
	{
		std_pHS->free(rb->buffer);
		rb->buffer = NULL;
	}

	SDL_DestroyMutex(rb->lock);
	SDL_DestroyCond(rb->notEmpty);
}

int stdJob_IsBusy()
{
	return SDL_AtomicGet(&stdJob_jobSystem.finishedLabel) < SDL_AtomicGet(&stdJob_jobSystem.currentLabel);
}

void stdJob_Complete(stdJob* job)
{
	SDL_LockMutex(job->completionMutex);
	SDL_CondSignal(job->completionCond); // Notify completion
	SDL_UnlockMutex(job->completionMutex);

	SDL_AtomicSet(&job->isCompleted, 1);
	SDL_AtomicIncRef(&stdJob_jobSystem.finishedLabel);
}

void stdJob_WorkerThread(void* param)
{
	stdJobSystem* jobs = (stdJobSystem*)param;
	stdJob* job = NULL;
	while (1)
	{
		if (stdJob_PopRingBuffer(&jobs->jobPool, &job))
		{
			job->function(&job->args);
			stdJob_Complete(job);
		}
		else
		{
			SDL_LockMutex(jobs->wakeMutex);
			if (SDL_AtomicGet(&jobs->quit))
			{
				SDL_UnlockMutex(jobs->wakeMutex);
				break;
			}
			SDL_CondWait(jobs->wakeCondition, jobs->wakeMutex);
			SDL_UnlockMutex(jobs->wakeMutex);
			if (SDL_AtomicGet(&jobs->quit))
				break;
		}
	}
}

void stdJob_TestJob(void* data)
{
	stdPlatform_Printf("Test job run successfully\n");
}

void stdJob_Startup()
{
	SDL_AtomicSet(&stdJob_jobSystem.finishedLabel, 0);
	SDL_AtomicSet(&stdJob_jobSystem.currentLabel, 0);
	SDL_AtomicSet(&stdJob_jobSystem.quit, 0);

	int cores = SDL_GetCPUCount();
	uint32_t numCores = (cores > 0) ? (uint32_t)cores : 1;

	stdPlatform_Printf("Starting job system with %d threads\n", numCores);
	stdJob_jobSystem.numThreads = numCores > 0 ? numCores : 1;

	stdJob_InitRingBuffer(&stdJob_jobSystem.jobPool, 1024);

	stdJob_jobSystem.workerThreads = (SDL_Thread**)std_pHS->alloc(stdJob_jobSystem.numThreads * sizeof(SDL_Thread*));
	if (!stdJob_jobSystem.workerThreads)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Failed to allocate worker thread handles\n");
		exit(EXIT_FAILURE);
	}
	
	stdJob_jobSystem.wakeMutex = SDL_CreateMutex();
	if (!stdJob_jobSystem.wakeMutex)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Failed to initialize wake mutex: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	stdJob_jobSystem.wakeCondition = SDL_CreateCond();
	if (!stdJob_jobSystem.wakeCondition)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Failed to initialize wake condition variable: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	for (uint32_t threadID = 0; threadID < stdJob_jobSystem.numThreads; ++threadID)
	{
		stdJob_jobSystem.workerThreads[threadID] = SDL_CreateThread(stdJob_WorkerThread, "stdJobThread", &stdJob_jobSystem);
		if (!stdJob_jobSystem.workerThreads[threadID])
		{
			stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Failed to create worker thread %u: %s\n", threadID, SDL_GetError());
			exit(EXIT_FAILURE);
		}

		// Set thread affinity
		//DWORD_PTR affinityMask = 1ull << threadID;
		//SetThreadAffinityMask(stdJob_jobSystem.workerThreads[threadID], affinityMask);
	}

	stdPlatform_Printf("Kicking off test job\n");
	stdJob_Execute(stdJob_TestJob, NULL);

	stdJob_bInit = 1;
}

void stdJob_Shutdown()
{
	stdJob_bInit = 0;

	stdJob_Wait();

	SDL_AtomicSet(&stdJob_jobSystem.quit, 1);
	SDL_CondBroadcast(stdJob_jobSystem.wakeCondition); // Wake all threads so they see quit

	for (uint32_t i = 0; i < stdJob_jobSystem.numThreads; ++i)
		SDL_WaitThread(stdJob_jobSystem.workerThreads[i], NULL); // Wait for thread to finish

	stdJob_FreeRingBuffer(&stdJob_jobSystem.jobPool);

	SDL_DestroyMutex(stdJob_jobSystem.wakeMutex);
	SDL_DestroyCond(stdJob_jobSystem.wakeCondition);

	if (stdJob_jobSystem.workerThreads)
	{
		std_pHS->free(stdJob_jobSystem.workerThreads);
		stdJob_jobSystem.workerThreads = 0;
	}
}

void stdJob_Wait()
{
	// Polling
	while (stdJob_IsBusy())
		SDL_CondSignal(stdJob_jobSystem.wakeCondition); // Signal workers
}

uint32_t stdJob_Execute(stdJob_function_t job, stdJobGroupArgs* args)
{
	if(!stdJob_bInit)
		return 0;

	SDL_AtomicIncRef(&stdJob_jobSystem.currentLabel);

	while (!stdJob_PushRingBuffer(&stdJob_jobSystem.jobPool, job, args))
		SDL_CondSignal(stdJob_jobSystem.wakeCondition); // Signal workers

	SDL_CondSignal(stdJob_jobSystem.wakeCondition); // Notify worker threads

	return (stdJob_jobSystem.jobPool.back + stdJob_jobSystem.jobPool.size - 1) % stdJob_jobSystem.jobPool.size;
}

void stdJob_WaitForHandle(uint32_t handle)
{
	if (handle == UINT32_MAX)
		return;

	stdJob* job = &stdJob_jobSystem.jobPool.buffer[handle];

	SDL_LockMutex(job->completionMutex);
	while (!SDL_AtomicGet(&job->isCompleted))
		SDL_CondWait(job->completionCond, job->completionMutex);
	SDL_UnlockMutex(job->completionMutex);
}

void stdJob_ExecuteGroup(void* param)
{
	stdJobGroupArgs* args = (stdJobGroupArgs*)param;
	if (!args)
	{
		stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Null arguments for ExecutGroup\n");
		exit(EXIT_FAILURE);		
		return;
	}

	uint32_t groupJobOffset = args->groupIndex * args->groupSize;
	uint32_t groupJobEnd = (groupJobOffset + args->groupSize > args->jobCount) ? args->jobCount : (groupJobOffset + args->groupSize);

	for (uint32_t i = groupJobOffset; i < groupJobEnd; ++i)
		args->job(i, args->groupIndex);

	//std_pHS->free(args);
}

void stdJob_Dispatch(uint32_t jobCount, uint32_t groupSize, void (*job)(uint32_t, uint32_t))
{
	if (!stdJob_bInit)
		return;

	if (jobCount == 0 || groupSize == 0)
		return;

	uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
	SDL_AtomicAdd(&stdJob_jobSystem.currentLabel, groupCount);

	for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
	{
		//stdJobGroupArgs* args = (stdJobGroupArgs*)std_pHS->alloc(sizeof(stdJobGroupArgs));
		//if (!args)
		//{
		//	stdPrintf(std_pHS->errorPrint, ".\\General\\stdJob.c", __LINE__, "Memory allocation failed for job group arguments\n");
		//	exit(EXIT_FAILURE);
		//}
		//args->jobs = &stdJob_jobSystem;
		//args->jobCount = jobCount;
		//args->groupSize = groupSize;
		//args->job = job;
		//args->groupIndex = groupIndex;
		stdJobGroupArgs args;
		args.jobs = &stdJob_jobSystem;
		args.jobCount = jobCount;
		args.groupSize = groupSize;
		args.job = job;
		args.groupIndex = groupIndex;

		stdJob_function_t jobGroup = stdJob_ExecuteGroup;
		while (!stdJob_PushRingBuffer(&stdJob_jobSystem.jobPool, jobGroup, &args))
			SDL_CondSignal(stdJob_jobSystem.wakeCondition); // Notify worker threads

		SDL_CondSignal(stdJob_jobSystem.wakeCondition); // Notify worker threads
	}
}

#else

void     stdJob_Startup() {}
void     stdJob_Shutdown() {}
int      stdJob_IsBusy() { return 0; }
void     stdJob_Wait() {}
uint32_t stdJob_Execute(stdJob_function_t job, void* args) { return -1; }
void     stdJob_Dispatch(uint32_t jobCount, uint32_t groupSize, void (*job)(uint32_t, uint32_t)) {}

#endif