// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#include "stdafx.h"
#include "common.h"

// === Синхронизация ===
HANDLE g_hMutexFullQueue = NULL;
HANDLE g_hMutexFreeQueue = NULL;
HANDLE g_hEventNewData = NULL;
HANDLE g_hEventFreeBuffer = NULL;
HANDLE g_hEventStop = NULL;

// === Очереди ===
AudioBuffer g_fullBuffers[BUFFER_COUNT];
volatile int g_fullCount = 0, g_fullHead = 0, g_fullTail = 0;
AudioBuffer g_freeBuffers[BUFFER_COUNT];
int g_freeCount = 0, g_freeHead = 0, g_freeTail = 0;

// === Пул памяти ===
BYTE* g_pBufferPool = NULL;

// === Состояние ===
volatile BOOL     g_isRecording = 0;
volatile LONGLONG g_totalBytesWritten = 0;
volatile DWORD    g_startTickCount = 0;
volatile LONG	  g_peakFullCount = 0;
volatile BOOL     g_writeError = FALSE;
volatile BOOL     g_captureError = FALSE;
volatile BOOL     g_antiIdleEnabled = FALSE;
volatile BOOL     g_antiIdleError = FALSE;
volatile BOOL     g_unexpectedGap = FALSE;

void InitializeSync() {
	g_hMutexFullQueue = CreateMutex(NULL, FALSE, NULL);
	g_hMutexFreeQueue = CreateMutex(NULL, FALSE, NULL);
	g_hEventNewData = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_hEventFreeBuffer = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_hEventStop = CreateEvent(NULL, TRUE, FALSE, NULL);
}

void CleanupSync() {
	if (g_hMutexFullQueue)  { CloseHandle(g_hMutexFullQueue);  g_hMutexFullQueue = NULL; }
	if (g_hMutexFreeQueue)  { CloseHandle(g_hMutexFreeQueue);  g_hMutexFreeQueue = NULL; }
	if (g_hEventNewData)    { CloseHandle(g_hEventNewData);    g_hEventNewData = NULL; }
	if (g_hEventFreeBuffer) { CloseHandle(g_hEventFreeBuffer); g_hEventFreeBuffer = NULL; }
	if (g_hEventStop)       { CloseHandle(g_hEventStop);       g_hEventStop = NULL; }
}

bool PushFullBuffer(AudioBuffer buf) {
	WaitForSingleObject(g_hMutexFullQueue, INFINITE);
	if (g_fullCount >= BUFFER_COUNT) { ReleaseMutex(g_hMutexFullQueue); return false; }
	g_fullBuffers[g_fullTail] = buf;
	g_fullTail = (g_fullTail + 1) % BUFFER_COUNT;
	g_fullCount++;
	if(g_fullCount > g_peakFullCount) g_peakFullCount = g_fullCount;		// обновление пика заполненности дисоковго буфера
	ReleaseMutex(g_hMutexFullQueue);
	return true;
}

bool PopFullBuffer(AudioBuffer& buf) {
	WaitForSingleObject(g_hMutexFullQueue, INFINITE);
	if (g_fullCount == 0) { ReleaseMutex(g_hMutexFullQueue); return false; }
	buf = g_fullBuffers[g_fullHead];
	g_fullHead = (g_fullHead + 1) % BUFFER_COUNT;
	g_fullCount--;
	ReleaseMutex(g_hMutexFullQueue);
	return true;
}

bool PushFreeBuffer(AudioBuffer buf) {
	WaitForSingleObject(g_hMutexFreeQueue, INFINITE);
	if (g_freeCount >= BUFFER_COUNT) { ReleaseMutex(g_hMutexFreeQueue); return false; }
	g_freeBuffers[g_freeTail] = buf;
	g_freeTail = (g_freeTail + 1) % BUFFER_COUNT;
	g_freeCount++;
	ReleaseMutex(g_hMutexFreeQueue);
	return true;
}

bool PopFreeBuffer(AudioBuffer& buf) {
	WaitForSingleObject(g_hMutexFreeQueue, INFINITE);
	if (g_freeCount == 0) { ReleaseMutex(g_hMutexFreeQueue); return false; }
	buf = g_freeBuffers[g_freeHead];
	g_freeHead = (g_freeHead + 1) % BUFFER_COUNT;
	g_freeCount--;
	ReleaseMutex(g_hMutexFreeQueue);
	return true;
}

// === Выделение одного блока памяти и нарезка на буферы ===
bool AllocateBufferPool() {
	size_t totalSize = BUFFER_SIZE * BUFFER_COUNT;

	g_pBufferPool = (BYTE*)VirtualAlloc(NULL, totalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!g_pBufferPool) return false;

	// Явно обнуляем обе очереди перед заполнением
	g_fullCount = g_fullHead = g_fullTail = 0;
	g_freeCount = g_freeHead = g_freeTail = 0;
	g_peakFullCount = 0;
	g_writeError = FALSE;
	g_captureError = FALSE;
	g_antiIdleError = FALSE;
	g_unexpectedGap = FALSE;

	// Нарезаем блок на отдельные буферы и добавляем в очередь свободных
	for (int i = 0; i < BUFFER_COUNT; i++) {
		AudioBuffer buf;
		buf.data = g_pBufferPool + (i * BUFFER_SIZE);
		buf.size = 0;
		PushFreeBuffer(buf);
	}

	return true;
}

// === Освобождение всего блока памяти ===
void FreeBufferPool() {
	if (g_pBufferPool) {
		VirtualFree(g_pBufferPool, 0, MEM_RELEASE);
		g_pBufferPool = NULL;
	}
}