// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#include "stdafx.h"
#include "common.h"

// For short ComPtr
using Microsoft::WRL::ComPtr;

// Устройство выбирается один раз в main.cpp и используется и для loopback-
// захвата, и для этого тихого render-потока — это должно быть одно и то же
// render-устройство, иначе "костыль" держит бодрым не тот движок.
extern ComPtr<IMMDevice> g_pSelectedDevice;

// === Антипростойный поток (проигрываем тишину) ===
DWORD WINAPI AntiIdleThreadProc(LPVOID lpParam) {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	ComPtr<IAudioClient>       pClient;
	ComPtr<IAudioRenderClient> pRenderClient;
	WAVEFORMATEX*       pMixFormat = NULL;
	HRESULT hr;
	UINT32  bufferFrameCount = 0;

	hr = g_pSelectedDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, &pClient);
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	hr = pClient->GetMixFormat(&pMixFormat);
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	// Обычная (не loopback) render-инициализация, без event callback —
	// нам не важна задержка, только чтобы буфер не пустел (буфер 500мс).
	hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 5000000, 0, pMixFormat, NULL);
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	// смотрим сколько реально выделила система
	hr = pClient->GetBufferSize(&bufferFrameCount);
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	hr = pClient->GetService(IID_PPV_ARGS(&pRenderClient));
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	// Перед стартом сразу заполняем весь буфер тишиной, как рекомендует
	// MSDN для render-клиентов — иначе первая же секунда может дать щелчок.
	{
		BYTE* pData = NULL;
		hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
		if (SUCCEEDED(hr)) pRenderClient->ReleaseBuffer(bufferFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
	}

	hr = pClient->Start();
	if (FAILED(hr)) { g_antiIdleError = TRUE; goto cleanup; }

	{
		// Будим поток вдвое чаще, чем опустошается буфер — небольшой запас
		// на джиттер планировщика. Не критично к точности: если проснёмся
		// чуть позже или раньше, GetCurrentPadding всё равно скажет, сколько
		// реально нужно дописать.
		DWORD waitMs = (DWORD)((double)bufferFrameCount / pMixFormat->nSamplesPerSec * 1000.0 / 2.0);
		if (waitMs < 5) waitMs = 5;

		while (g_isRecording) {
			DWORD waitResult = WaitForSingleObject(g_hEventStop, waitMs);
			if (waitResult == WAIT_OBJECT_0) break;   // стоп

			UINT32 padding = 0;
			hr = pClient->GetCurrentPadding(&padding);
			if (FAILED(hr)) continue;   // временный сбой — не фатально, пробуем на следующем тике

			UINT32 framesAvailable = bufferFrameCount - padding;
			if (framesAvailable == 0) continue;

			BYTE* pData = NULL;
			hr = pRenderClient->GetBuffer(framesAvailable, &pData);
			if (FAILED(hr)) continue;

			pRenderClient->ReleaseBuffer(framesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
		}
	}

cleanup:
	if (pClient)       pClient->Stop();
	if (pMixFormat)    CoTaskMemFree(pMixFormat);

	CoUninitialize();
	return 0;
}
