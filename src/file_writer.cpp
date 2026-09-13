// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#include "stdafx.h"
#include "common.h"

extern WAVEFORMATEXTENSIBLE g_captureFormat;

WCHAR g_outputPath[MAX_PATH] = L"";

void WriteW64Header(HANDLE hFile, UINT64 dataSize) {
	W64Header header = { 0 };

	const UINT64 CHUNK_HEADER_SIZE = 24; // sizeof(GUID) + sizeof(UINT64)

	header.riffGuid = W64_GUID_RIFF;
	header.waveGuid = W64_GUID_WAVE;
	header.fmtGuid = W64_GUID_FMT;
	header.dataGuid = W64_GUID_DATA;
	header.fmtSize = CHUNK_HEADER_SIZE + sizeof(WAVEFORMATEXTENSIBLE);
	header.dataSize = CHUNK_HEADER_SIZE + dataSize;
	header.riffSize = sizeof(W64Header) + dataSize; // = весь файл целиком

	memcpy(&header.format, &g_captureFormat, sizeof(WAVEFORMATEXTENSIBLE));

	if (header.format.Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
		header.format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		header.format.Format.cbSize = 22;
		header.format.Samples.wValidBitsPerSample = header.format.Format.wBitsPerSample;
		if (header.format.Format.wBitsPerSample != 32 ||
			!IsEqualGUID(header.format.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
			header.format.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		}
	}

	DWORD bw = 0;
	if (!WriteFile(hFile, &header, sizeof(header), &bw, NULL) || bw != sizeof(header)) {
		g_writeError = TRUE;
	}
}

void UpdateW64Header(HANDLE hFile, UINT64 dataSize) {
	LARGE_INTEGER offset = { 0 };
	SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);
	WriteW64Header(hFile, dataSize);
}

DWORD WINAPI FileWriterThreadProc(LPVOID lpParam) {
	UINT64 totalDataSize = 0;
	DWORD  bytesWritten = 0;
	bool   stopWriting = false;   // true — была ошибка записи, дальше не продолжаем

	HANDLE hFile = CreateFile(g_outputPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		g_writeError = TRUE;
		SetEvent(g_hEventStop);
		return 1;
	}

	WriteW64Header(hFile, 0);

	while (!stopWriting && (g_isRecording || g_fullCount > 0)) {
		HANDLE handles[] = { g_hEventNewData, g_hEventStop };
		DWORD  timeout = g_isRecording ? INFINITE : 100;
		DWORD  result = WaitForMultipleObjects(2, handles, FALSE, timeout);

		if (result == WAIT_OBJECT_0 + 1) g_isRecording = 0;

		// По любому пробуждению вычерпываем из очереди всё
		AudioBuffer buf;
		while (PopFullBuffer(buf)) {
			BOOL ok = WriteFile(hFile, buf.data, (DWORD)buf.size, &bytesWritten, NULL);
			
			if (!ok || bytesWritten != buf.size) {
				// Диск переполнен/ошибка ввода-вывода.
				g_writeError = TRUE;
				PushFreeBuffer(buf);
				SetEvent(g_hEventFreeBuffer);
				SetEvent(g_hEventStop);
				stopWriting = true;
				break;
			}

			totalDataSize += buf.size;
			InterlockedExchangeAdd64(&g_totalBytesWritten, buf.size);
			PushFreeBuffer(buf);
			SetEvent(g_hEventFreeBuffer);
		}
	}

	UpdateW64Header(hFile, totalDataSize);
	CloseHandle(hFile);
	return 0;
}