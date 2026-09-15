// Copyright (c) 2026 Pun0110. Licensed under the GNU GPL v3.

#include "stdafx.h"
#include "resource.h"
#include "main.h"
#include "common.h"

// For short ComPtr
using Microsoft::WRL::ComPtr;

// From audio_capture.cpp
extern ComPtr<IMMDevice>	g_pSelectedDevice;
extern WAVEFORMATEXTENSIBLE g_captureFormat;

// Из file_writer.cpp
extern WCHAR g_outputPath[MAX_PATH];

// === Элементы UI ===
HWND g_hwndMain = NULL;
HWND g_hwndComboSource = NULL;
HWND g_hwndLabelDirPath = NULL;
HWND g_hwndLabelTime = NULL;
HWND g_hwndLabelSize = NULL;
HWND g_hwndLabelBuff = NULL;
HWND g_hwndBtnRecord = NULL;
HWND g_hwndBtnBrowse = NULL;
HWND g_hwndChkAntiIdle = NULL;

sHandleHolder handleHolder[] = {
	{ &g_hwndComboSource, IDC_CMB_SOURCE },
	{ &g_hwndLabelDirPath, IDC_LBL_DIRPATH },
	{ &g_hwndLabelTime, IDC_LBL_TIMEVAL },
	{ &g_hwndLabelSize, IDC_LBL_SIZEVAL },
	{ &g_hwndLabelBuff, IDC_LBL_BUFFVAL },
	{ &g_hwndBtnRecord, IDC_BTN_REC },
	{ &g_hwndBtnBrowse, IDC_BTN_BROWSE },
	{ &g_hwndChkAntiIdle, IDC_CHK_ANTIIDLE },
};

static const sControlString g_controlStrings[] = {
	{ IDC_LBL_SOURCE,	IDS_LBL_SOURCE },
	{ IDC_LBL_DIR,		IDS_LBL_FOLDER },
	{ IDC_LBL_TIMELBL,	IDS_LBL_DURATION },
	{ IDC_LBL_SIZELBL,	IDS_LBL_SIZE },
	{ IDC_LBL_BUFFLBL,	IDS_LBL_BUFF },
	{ IDC_CHK_ANTIIDLE, IDS_CHK_SILENT },
	{ IDC_BTN_BROWSE,	IDS_BTN_BROWSE },
	{ IDC_BTN_REC,		IDS_BTN_START },  // init state - "Start"
};

HINSTANCE g_hInstance = NULL;

// === Каталог сохранения ===
WCHAR g_outputDir[MAX_PATH] = L"";

// === Устройства ===

DeviceInfo g_devices[16];
int        g_deviceCount = 0;

// === Хэндлы рабочих потоков ===
HANDLE g_hThreadCapture = NULL;
HANDLE g_hThreadWriter = NULL;
HANDLE g_hThreadSilence = NULL;

// Отдельный от g_isRecording флаг для UI
static BOOL g_uiIsRecording = FALSE;

// === Localization ===
WCHAR sStart[16];
WCHAR sStop[16];
WCHAR sFmtB[16];
WCHAR sFmtK[16];
WCHAR sFmtM[16];
WCHAR sFmtG[16];

sStringMapping stringMapping[] = {
	{ sStart,	IDS_BTN_START },
	{ sStop,	IDS_BTN_STOP },
	{ sFmtB,	IDS_LBL_BUFF_FMTB },
	{ sFmtK,	IDS_LBL_BUFF_FMTK },
	{ sFmtM,	IDS_LBL_BUFF_FMTM },
	{ sFmtG,	IDS_LBL_BUFF_FMTG }
};

// === Get HWNDs ===
void GetDlgControlIDs() {
	for (int i = 0; i < sizeof(handleHolder) / sizeof(sHandleHolder); i++)
		*(handleHolder[i].pHandle) = GetDlgItem(g_hwndMain, handleHolder[i].id);
}

// === Localize UI  ===
void LocalizeDialog(HWND hDlg) {

	// locallisation test
	#ifdef _DEBUG
		SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
	#endif
	
	// load strings
	for (int i = 0; i < sizeof(stringMapping) / sizeof(sStringMapping); i++)
		LoadString(g_hInstance, stringMapping[i].stringId, stringMapping[i].string, 16);

	WCHAR buff[64];
	// set UI controls text
	for (int i = 0; i < sizeof(g_controlStrings) / sizeof(sControlString); i++) {
		if (LoadString(g_hInstance, g_controlStrings[i].stringId, buff, 64)) {
			SetDlgItemText(hDlg, g_controlStrings[i].controlId, buff);
		}
	}

	SetWindowText(g_hwndBtnRecord, sStart);
}

// === Localized MessageBox ===
void LocMessageBox(HWND hDlg, UINT32 textId, UINT msgType) {
	WCHAR message[256];
	WCHAR caption[32];

	LoadString(g_hInstance, textId, message, 256);
	
	UINT32 msgT;
	switch (msgType)
	{
		case MB_ICONERROR:
			msgT = IDS_MSG_ERR;
			break;

		case MB_ICONWARNING:
			msgT = IDS_MSG_WRN;
			break;

		default:
			msgT = IDS_MSG_WRN;
			break;
	}
		
	LoadString(g_hInstance, msgT, caption, 32);
	MessageBox(g_hwndMain, message, caption, msgType);
}
// === Перечисление render-устройств (источников для loopback) ===
void EnumerateDevices() {
	ComPtr<IMMDeviceEnumerator> pEnumerator;
	ComPtr<IMMDeviceCollection> pCollection;

	// чистим старый список
	for (int i = 0; i < g_deviceCount; i++) g_devices[i].pDevice.Reset();
	
	g_deviceCount = 0;
	SendMessage(g_hwndComboSource, CB_RESETCONTENT, 0, 0);

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnumerator));
	if (FAILED(hr)) return;

	// eRender, приложение работает только с loopback
	hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
	if (FAILED(hr))  return;

	UINT count = 0;
	pCollection->GetCount(&count);

	for (UINT i = 0; i < count && g_deviceCount < 16; i++) {
		ComPtr<IMMDevice> pDevice;
		if (FAILED(pCollection->Item(i, &pDevice))) continue;

		ComPtr<IPropertyStore> pProps;
		if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
			PROPVARIANT varName;
			PropVariantInit(&varName);

			if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
				g_devices[g_deviceCount].pDevice = pDevice;
				StringCchCopy(g_devices[g_deviceCount].name, 256, varName.pwszVal);
				SendMessage(g_hwndComboSource, CB_ADDSTRING, 0, (LPARAM)g_devices[g_deviceCount].name);
				g_deviceCount++;
			}

			PropVariantClear(&varName);
		}
	}

	if (g_deviceCount > 0) SendMessage(g_hwndComboSource, CB_SETCURSEL, 0, 0);
}

// === Обновить выбранное устройство ===
void OnDeviceChanged() {
	int sel = (int)SendMessage(g_hwndComboSource, CB_GETCURSEL, 0, 0);
	if (sel == CB_ERR || sel >= g_deviceCount) return;
	g_pSelectedDevice = g_devices[sel].pDevice;
}

// === Выбор каталога ===
void OnBrowseFolder() {

	ComPtr<IFileOpenDialog> pDlg;

	if (SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
	{
		FILEOPENDIALOGOPTIONS options;
		pDlg->GetOptions(&options);
		pDlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );

		if (SUCCEEDED(pDlg->Show(g_hwndMain)))
		{
			ComPtr<IShellItem> pItem;
			if (SUCCEEDED(pDlg->GetResult(&pItem)))
			{
				WCHAR* pszPath = nullptr;
				if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
				{
					StringCchCopy(g_outputDir, MAX_PATH, pszPath);
					SetWindowText(g_hwndLabelDirPath, g_outputDir);
					CoTaskMemFree(pszPath);
				}
			}
		}
	}
}

// === Форматирование ===
void FormatTime(DWORD ms, WCHAR* buf, int sz) {
	DWORD s = ms / 1000, m = s / 60, h = m / 60;
	s %= 60; m %= 60;
	StringCchPrintf(buf, sz, L"%02d:%02d:%02d", h, m, s);
}

void FormatSize(LONGLONG bytes, WCHAR* buf, int sz) {
	if (bytes < 1536LL)						StringCchPrintf(buf, sz, sFmtB, bytes);
	else if (bytes < 1536LL * 1024)			StringCchPrintf(buf, sz, sFmtK, bytes / 1024.0);
	else if (bytes < 1536LL * 1024 * 1024)	StringCchPrintf(buf, sz, sFmtM, bytes / (1024.0 * 1024.0));
	else									StringCchPrintf(buf, sz, sFmtG, bytes / (1024.0 * 1024.0 * 1024.0));
}

//  === Статистика время, размер ===
void UpdateStatsLabels() {
	WCHAR t[64], s[64];
	FormatTime(GetTickCount() - g_startTickCount, t, 64);
	FormatSize(g_totalBytesWritten, s, 64);
	SetWindowText(g_hwndLabelTime, t);
	SetWindowText(g_hwndLabelSize, s);

	WCHAR b[32];
	StringCchPrintf(b, 32, L"%d/%d/%d", g_fullCount, g_peakFullCount, BUFFER_COUNT);
	SetWindowText(g_hwndLabelBuff, b);
}

// === Старт записи ===
void StartRecording() {
	if (!g_pSelectedDevice) {
		// device not selected
		LocMessageBox(g_hwndMain, IDS_MSG_ERRDEVICE, MB_ICONERROR);
		return;
	}

	if (g_outputDir[0] == L'\0') {
		// folder not set
		LocMessageBox(g_hwndMain, IDS_MSG_ERRFOLDER, MB_ICONERROR);
		return;
	}

	// Генерируем имя файла
	SYSTEMTIME st;
	GetLocalTime(&st);
	StringCchPrintf(g_outputPath, MAX_PATH, L"%s\\%04d%02d%02d_%02d%02d%02d.w64",
		g_outputDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	// Инициализация синхронизации
	InitializeSync();

	// Выделяем пул буферов одним блоком
	if (!AllocateBufferPool()) {
		// can't allocate memory
		LocMessageBox(g_hwndMain, IDS_MSG_ERRMEMORY, MB_ICONERROR);
		CleanupSync();
		return;
	}

	// Сбрасываем счетчики
	g_totalBytesWritten = 0;
	g_startTickCount = GetTickCount();
	g_isRecording = TRUE;

	// Состояние чекбокса фиксируем один раз на всю сессию записи
	g_antiIdleEnabled = (SendMessage(g_hwndChkAntiIdle, BM_GETCHECK, 0, 0) == BST_CHECKED);

	// Запускаем потоки захвата и дампа
	g_hThreadCapture = CreateThread(NULL, 0, AudioCaptureThreadProc, NULL, 0, NULL);
	g_hThreadWriter = CreateThread(NULL, 0, FileWriterThreadProc, NULL, 0, NULL);

	if (!g_hThreadCapture || !g_hThreadWriter) {
		// can't create worker threads
		LocMessageBox(g_hwndMain, IDS_MSG_ERRTHREADS, MB_ICONERROR);
		g_isRecording = FALSE;
		SetEvent(g_hEventStop);
		if (g_hThreadCapture) { WaitForSingleObject(g_hThreadCapture, INFINITE); CloseHandle(g_hThreadCapture); g_hThreadCapture = NULL; }
		if (g_hThreadWriter)  { WaitForSingleObject(g_hThreadWriter, INFINITE); CloseHandle(g_hThreadWriter); g_hThreadWriter = NULL; }
		FreeBufferPool();
		CleanupSync();
		return;
	}

	// Антипростойный поток — опциональный
	// сбой потока выставляет флаг, который увидит пользователь после остановки записи.
	if (g_antiIdleEnabled) {
		g_hThreadSilence = CreateThread(NULL, 0, AntiIdleThreadProc, NULL, 0, NULL);
		if (!g_hThreadSilence) g_antiIdleError = TRUE;
	}

	SetTimer(g_hwndMain, TIMER_ID, 500, NULL);
	g_uiIsRecording = TRUE;

	// UI
	SetWindowText(g_hwndBtnRecord, sStop);
	EnableWindow(g_hwndComboSource, FALSE);
	EnableWindow(g_hwndBtnBrowse, FALSE);
	EnableWindow(g_hwndChkAntiIdle, FALSE);
}

// === Стоп записи ===
// g_uiIsRecording не даёт вызвать очистку дважды и позволяет WM_TIMER
// безопасно вызывать StopRecording() автоматически, когда рабочий поток сам
// остановился из-за ошибки (см. объявление флага выше).
void StopRecording() {
	if (!g_uiIsRecording) return;

	g_uiIsRecording = FALSE;
	g_isRecording = FALSE;
	SetEvent(g_hEventStop);

	KillTimer(g_hwndMain, TIMER_ID);

	// Ждём потоки с разумным таймаутом на случай, если
	// поток всё же завис (например, драйвер устройства не отвечает).
	HANDLE waitHandles[3];
	int    waitCount = 0;
	if (g_hThreadCapture) waitHandles[waitCount++] = g_hThreadCapture;
	if (g_hThreadWriter)  waitHandles[waitCount++] = g_hThreadWriter;
	if (g_hThreadSilence) waitHandles[waitCount++] = g_hThreadSilence;

	if (waitCount > 0) {
		DWORD wr = WaitForMultipleObjects(waitCount, waitHandles, TRUE, 5000);
		if (wr == WAIT_TIMEOUT) {
			// Threads stuck
			LocMessageBox(g_hwndMain, IDS_MSG_WRNTHREADS, MB_ICONWARNING);
		}
	}

	if (g_hThreadCapture) { CloseHandle(g_hThreadCapture); g_hThreadCapture = NULL; }
	if (g_hThreadWriter)  { CloseHandle(g_hThreadWriter);  g_hThreadWriter = NULL; }
	if (g_hThreadSilence) { CloseHandle(g_hThreadSilence); g_hThreadSilence = NULL; }

	// Освобождаем пул буферов
	FreeBufferPool();
	CleanupSync();

	// UI
	SetWindowText(g_hwndBtnRecord, sStart);
	EnableWindow(g_hwndComboSource, TRUE);
	EnableWindow(g_hwndBtnBrowse, TRUE);
	EnableWindow(g_hwndChkAntiIdle, TRUE);

	UpdateStatsLabels();

	// При ошибке записи на диск или сбое инициализации устройства
	// захвата сообщаем, что-то пошло не так.
	if (g_writeError) {
		// write error
		LocMessageBox(g_hwndMain, IDS_MSG_ERRWRITE, MB_ICONERROR);
	}
	else if (g_captureError) {
		// capture error
		LocMessageBox(g_hwndMain, IDS_MSG_ERRCAPTURE, MB_ICONERROR);
	}

	if (g_antiIdleError) {
		// silence renderer
		LocMessageBox(g_hwndMain, IDS_MSG_WRNSILENCE, MB_ICONWARNING);
	}

	if (g_unexpectedGap) {
		// flag AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
		LocMessageBox(g_hwndMain, IDS_MSG_WRNGAP, MB_ICONWARNING);
	}
}

// === Dialog Proc ===
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_INITDIALOG:
		g_hwndMain = hDlg;

		GetDlgControlIDs();
		LocalizeDialog(hDlg);

		EnumerateDevices();
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BTN_BROWSE:
			// выбор каталога для сохранения
			OnBrowseFolder();
			return TRUE;

		case IDC_BTN_REC:
			// Переключаем по g_uiIsRecording
			if (!g_uiIsRecording) {
				OnDeviceChanged();
				StartRecording();
			}
			else {
				StopRecording();
			}
			return TRUE;

		case IDC_CMB_SOURCE:
			if (HIWORD(wParam) == CBN_SELCHANGE) OnDeviceChanged();
			return TRUE;
		}
		return TRUE;

	case WM_TIMER:
		// обновление статистики по таймеру
		// проверям синхронность флагов
		if (g_uiIsRecording && !g_isRecording) {
			StopRecording();
		}
		else {
			UpdateStatsLabels();
		}
		return TRUE;

	case WM_CLOSE:
		if (g_uiIsRecording) StopRecording();
		DestroyWindow(hDlg);
		return TRUE;

	case WM_DESTROY:
		for (int i = 0; i < g_deviceCount; i++) g_devices[i].pDevice.Reset();
		g_pSelectedDevice.Reset();

		PostQuitMessage(0);
		return TRUE;
	}
	return FALSE;
}

// === Точка входа ===
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
	g_hInstance = hInstance;

	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	InitCommonControls();

	HWND hDlg = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, DialogProc, 0);

	if (!hDlg) {
		LocMessageBox(g_hwndMain, IDS_MSG_ERRWINDOW, MB_ICONERROR);
		CoUninitialize();
		return 1;
	}

	ShowWindow(hDlg, nCmdShow);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		if (!IsDialogMessage(hDlg, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	CoUninitialize();
	return (int)msg.wParam;
}