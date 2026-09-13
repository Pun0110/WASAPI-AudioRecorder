// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#include "stdafx.h"
#include "common.h"

// Устройство и формат — устанавливаются из main.cpp перед запуском потока
IMMDevice*          g_pSelectedDevice = NULL;
WAVEFORMATEXTENSIBLE g_captureFormat;

DWORD WINAPI AudioCaptureThreadProc(LPVOID lpParam) {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	IAudioClient*        pClient = NULL;
	IAudioCaptureClient* pCaptureClient = NULL;
	HANDLE               hAudioEvent = NULL;
	WAVEFORMATEX*        pMixFormat = NULL;
	HRESULT				 hr = S_OK;

	// 1. Активируем IAudioClient
	hr = g_pSelectedDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pClient);
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// 2. Получаем формат
	hr = pClient->GetMixFormat(&pMixFormat);
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// Сохраняем формат для writer
	if (pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		memcpy(&g_captureFormat, pMixFormat, sizeof(WAVEFORMATEXTENSIBLE));
	}
	else {
		memset(&g_captureFormat, 0, sizeof(WAVEFORMATEXTENSIBLE));
		memcpy(&g_captureFormat.Format, pMixFormat, sizeof(WAVEFORMATEX));
		g_captureFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		g_captureFormat.Format.cbSize = 22;
		g_captureFormat.Samples.wValidBitsPerSample = g_captureFormat.Format.wBitsPerSample;
		g_captureFormat.dwChannelMask = 0;
		g_captureFormat.SubFormat = (g_captureFormat.Format.wBitsPerSample == 32)
			? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
			: KSDATAFORMAT_SUBTYPE_PCM;
	}

	// 3. Event для WASAPI
	hAudioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!hAudioEvent) { g_captureError = TRUE; goto cleanup; }

	// 4. Инициализация клиента. Работаем только с loopback
	DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_LOOPBACK;

	hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, BUFFER_DURATION, 0, (WAVEFORMATEX*)&g_captureFormat, NULL);
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// 5. Привязываем event
	hr = pClient->SetEventHandle(hAudioEvent);
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// 6. Получаем capture client
	hr = pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// 7. Старт
	hr = pClient->Start();
	if (FAILED(hr)) { g_captureError = TRUE; goto cleanup; }

	// 8. Цикл захвата
	{
		AudioBuffer currentBuf;
		if (!PopFreeBuffer(currentBuf)) { g_captureError = TRUE; goto cleanup; }

		size_t  currentOffset = 0;
		UINT32  bytesPerFrame = g_captureFormat.Format.nBlockAlign;

		while (g_isRecording) {
			HANDLE handles[] = { hAudioEvent, g_hEventStop };
			DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

			if (result == WAIT_OBJECT_0 + 1) break;   // стоп Event

			if (result == WAIT_OBJECT_0) {
				BYTE*  pData;
				UINT32 numFramesAvailable;
				DWORD  flags;

				hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
				if (FAILED(hr)) continue;

				if (g_antiIdleEnabled && (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) {
					// если проскочил разрыв в потоке при активном потоке тишины, то это косяк
					g_unexpectedGap = TRUE;
				}

				// тинина в пакете AUDCLNT_BUFFERFLAGS_SILENT
				bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

				size_t packetSize = numFramesAvailable * bytesPerFrame;
				size_t bytesCopied = 0;
				bool   outOfBuffers = false;

				while (bytesCopied < packetSize && !outOfBuffers) {
					size_t spaceLeft = BUFFER_SIZE - currentOffset;
					size_t toCopy = min(spaceLeft, packetSize - bytesCopied);

					if (isSilent)
						memset(currentBuf.data + currentOffset, 0, toCopy);
					else
						memcpy(currentBuf.data + currentOffset, pData + bytesCopied, toCopy);
					currentOffset += toCopy;
					bytesCopied += toCopy;

					if (currentOffset == BUFFER_SIZE) {
						currentBuf.size = BUFFER_SIZE;
						PushFullBuffer(currentBuf);
						SetEvent(g_hEventNewData);

						// подождать, пока writer освободит буфер
						while (!PopFreeBuffer(currentBuf)) {
							HANDLE waitHandles[] = { g_hEventFreeBuffer, g_hEventStop };
							DWORD  waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, 2000);
							if (waitResult != WAIT_OBJECT_0) {
								// Либо пришёл стоп, либо writer не успевает за 2 секунды
								outOfBuffers = true;
								break;
							}
						}
						if (!outOfBuffers) currentOffset = 0;
					}
				}

				pCaptureClient->ReleaseBuffer(numFramesAvailable);
			}
		}

		// Последний неполный буфер
		if (currentOffset > 0) {
			currentBuf.size = currentOffset;
			PushFullBuffer(currentBuf);
			SetEvent(g_hEventNewData);
		}
	}

cleanup:
	if (pClient)        pClient->Stop();
	if (hAudioEvent)    CloseHandle(hAudioEvent);
	if (pCaptureClient) pCaptureClient->Release();
	if (pClient)        pClient->Release();
	if (pMixFormat)     CoTaskMemFree(pMixFormat);

	CoUninitialize();
	return 0;
}