// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#pragma once

// структура для связи контролов (*handle <-> controlID)
struct sHandleHolder
{
	HWND*	pHandle;
	UINT32	id;
};

// структура для связи строк (controlId <-> stringId)
struct sControlString {
	int controlId;
	int stringId;
};

// Приложение работает только с loopback
struct DeviceInfo {
	IMMDevice* pDevice;
	WCHAR      name[256];
};