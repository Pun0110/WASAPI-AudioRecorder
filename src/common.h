// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#pragma once

#ifndef PKEY_Device_FriendlyName
#include <wtypes.h>
const PROPERTYKEY PKEY_Device_FriendlyName = {
	{ 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } },
	2
};
#endif

// === Константы ===
const size_t		BUFFER_SIZE = 256 * 1024;		// 256 КБ на буфер
const INT32			BUFFER_COUNT = 8;				// 8 буферов = 2Мб
const size_t		BUFFER_DURATION = 10000000;		// длительность WASAPI буфера в 100нс интервалах
const UINT_PTR		TIMER_ID = 1;

// === Структура буфера ===
struct AudioBuffer {
	BYTE*  data;
	size_t size;
};

// === GUID W64 ===
const GUID W64_GUID_RIFF = { 0x66666972, 0x912E, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 } };
const GUID W64_GUID_WAVE = { 0x65766177, 0xACF3, 0x11D3, { 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A } };
const GUID W64_GUID_FMT = { 0x20746D66, 0xACF3, 0x11D3, { 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A } };
const GUID W64_GUID_DATA = { 0x61746164, 0xACF3, 0x11D3, { 0x8C, 0xD1, 0x00, 0xC0, 0x4F, 0x8E, 0xDB, 0x8A } };

// === W64 заголовок ===
#pragma pack(push, 1)
struct W64Header {
	GUID                 riffGuid;
	UINT64               riffSize;
	GUID                 waveGuid;
	GUID                 fmtGuid;
	UINT64               fmtSize;
	WAVEFORMATEXTENSIBLE format;
	GUID                 dataGuid;
	UINT64               dataSize;
};
#pragma pack(pop)

// === Синхронизация ===
extern HANDLE g_hMutexFullQueue;
extern HANDLE g_hMutexFreeQueue;
extern HANDLE g_hEventNewData;
extern HANDLE g_hEventFreeBuffer;
extern HANDLE g_hEventStop;

// === Очереди ===
extern AudioBuffer g_fullBuffers[BUFFER_COUNT];
extern volatile int g_fullCount, g_fullHead, g_fullTail;
extern AudioBuffer g_freeBuffers[BUFFER_COUNT];
extern int g_freeCount, g_freeHead, g_freeTail;

// === Пул памяти ===
extern BYTE* g_pBufferPool;  // Указатель на начало выделенного блока

// === Состояние ===
extern volatile BOOL     g_isRecording;
extern volatile LONGLONG g_totalBytesWritten;
extern volatile DWORD    g_startTickCount;
extern volatile LONG	 g_peakFullCount;		// пик заполненности файлового буфера

// Флаги неожиданного завершения (диск/устройство отвалились)
extern volatile BOOL     g_writeError;     // WriteFile упал
extern volatile BOOL     g_captureError;   // не удалось инициализировать/открыть устройство захвата

// === "Костыль" (тихий render-поток) против простоя аудио-движка ===
extern volatile BOOL     g_antiIdleEnabled;  // выбор пользователя на эту сессию записи
extern volatile BOOL     g_antiIdleError;    // не удалось поднять тихий поток
extern volatile BOOL     g_unexpectedGap;    // DATA_DISCONTINUITY - разрыв в потоке

// === Функции синхронизации ===
void InitializeSync();
void CleanupSync();

// === Очереди ===
bool PushFullBuffer(AudioBuffer buf);
bool PopFullBuffer(AudioBuffer& buf);
bool PushFreeBuffer(AudioBuffer buf);
bool PopFreeBuffer(AudioBuffer& buf);

// === Управление пулом памяти ===
bool  AllocateBufferPool();  // (пере)выделяет пул и полностью сбрасывает обе очереди
void  FreeBufferPool();

// === Потоки ===
DWORD WINAPI AudioCaptureThreadProc(LPVOID lpParam);
DWORD WINAPI FileWriterThreadProc(LPVOID lpParam);
DWORD WINAPI AntiIdleThreadProc(LPVOID lpParam);