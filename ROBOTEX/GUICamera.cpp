#include "stdafx.h"
#include "GUICamera.h"
#include "qedit.h"

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define ID_EDITCHILD 100
#define WM_GRAPH_EVENT (WM_APP + 1)


AM_MEDIA_TYPE *g_pmt = (AM_MEDIA_TYPE*)new byte[100];

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProcCalibrator(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hwnd);
void OnPaintCalibrator(HWND hwnd);
void InitVideo(HWND hwnd);
void PrintFilters(IGraphBuilder *pGraph);
void Release();
HRESULT WriteBitmap(LPCWSTR, BITMAPINFOHEADER*, size_t, BYTE *, size_t);
void CALLBACK OnGraphEvent(HWND hwnd);
void prints(wchar_t* text, ...);
DWORD HSVtoRGB(float, float, float);
void saveToFileColorThresholds();
void readFromFileColorThresholds();


IMediaControl *pControl = NULL;
IMediaEventEx   *pEvent = NULL;
IMediaEventSink   *pEventSink = NULL;
IGraphBuilder *pGraph = NULL;
ICaptureGraphBuilder2 *pBuild = NULL;
IMFVideoDisplayControl *pDisplay = NULL;
HRESULT hr;
BYTE *g_pBuffer = NULL; //buffer of the image displayed on the right side of the screen
BOOL start = TRUE;
BITMAPINFOHEADER bmih;
BITMAPINFO dbmi;
HBITMAP hBitmap = NULL;
HWND hwndEdit = NULL;
HWND hwndCalibrate = NULL;
BOOLEAN calibrating = FALSE;
extern HANDLE readySignal;
extern HANDLE getImageSignal;
extern HANDLE setImageSignal;
extern HANDLE button2Signal;
HANDLE newImageSignal = CreateEvent(NULL, FALSE, FALSE, NULL);
enum { ID_BUTTON1, ID_BUTTON2, ID_BUTTON3, ID_BUTTON_DONE, ID_TRACKBAR_HUE, ID_TRACKBAR_SATURATION, 
	ID_TRACKBAR_VALUE, ID_RADIOBOXGROUP_MINMAX, ID_RADIOBOX_MIN, ID_RADIOBOX_MAX, ID_BUTTON_SAVE};
extern BYTE *editBuffer;
HWND hwndHue;
HWND hwndSaturation;
HWND hwndValue;
float hue = 0;
float hueMin;
float hueMax;
float saturation = 0;
float saturationMin;
float saturationMax;
float value = 0;
float valueMin;
float valueMax;
enum CurrentCalibratorSetting { minimum, maximum };
CurrentCalibratorSetting currentCalibratorSetting = minimum;

class SampleGrabberCallback : public ISampleGrabberCB
{
public:
	// Fake referance counting.
	STDMETHODIMP_(ULONG) AddRef() { return 1; }
	STDMETHODIMP_(ULONG) Release() { return 2; }

	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject)
	{
		if (NULL == ppvObject) return E_POINTER;
		if (riid == __uuidof(IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			return S_OK;
		}
		if (riid == IID_ISampleGrabberCB)
		{
			*ppvObject = static_cast<ISampleGrabberCB*>(this);
			return S_OK;
		}
		return E_NOTIMPL;
	}

	STDMETHODIMP SampleCB(double Time, IMediaSample *pSample)
	{
		printf("\nSampleCB\n\n");
		return E_NOTIMPL;
	}

	STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long BufferLen)
	{
		if (calibrating && g_pBuffer != NULL) {
			CopyMemory(g_pBuffer, pBuffer, BufferLen);
			for (DWORD *pixBuffer = (DWORD*)g_pBuffer; pixBuffer < (DWORD*)g_pBuffer + BufferLen/4; ++pixBuffer) {
				DWORD pixel = *pixBuffer;
				BYTE red = pixel & 0xFF, green = (pixel >> 8) & 0xFF, blue = (pixel >> 16) & 0xFF;
				
				float hue;
				//calculates hue in the range 0 to 6
				if (red >= green && green >= blue && red > blue)	hue = ((float)(green - blue) / (red - blue));
				else if (green > red && red >= blue)	hue = (2 - (float)(red - blue) / (green - blue));
				else if (green >= blue && blue > red)	hue = (2 + (float)(blue - red) / (green - red));
				else if (blue > green && green > red)	hue = (4 - (float)(green - red) / (blue - red));
				else if (blue > red && red >= green)	hue = (4 + (float)(red - green) / (blue - green));
				else if (red >= blue && blue > green)	hue = (6 - (float)(blue - green) / (red - green));
				else hue = 0; //Hue when the image is gray red=green=blue

				float saturation = (float)(max3(red, green, blue) - min3(red, green, blue))
					/ max3(red, green, blue);

				float value = (float)max3(red, green, blue)/255;

				if (hue < hueMin || hue > hueMax || saturation < saturationMin || saturation > saturationMax ||
					value < valueMin || value > valueMax)
					*pixBuffer = 0;
			}
			GdiFlush();
		}
		else 
		{
			if (g_pBuffer != NULL && !WaitForSingleObject(setImageSignal, 0)) {
				CopyMemory(g_pBuffer, editBuffer, BufferLen);
				GdiFlush();
			}
			else if (g_pBuffer != NULL && !WaitForSingleObject(newImageSignal, 0)) {
				CopyMemory(g_pBuffer, pBuffer, BufferLen);
				GdiFlush();
			}
			else if (!WaitForSingleObject(getImageSignal, 0)) {
				CopyMemory(editBuffer, pBuffer, BufferLen);
				SetEvent(readySignal);
			}
			//printf("\nBufferCB %ld %ld\n\n", BufferLen, pBuffer);
			//VIDEOINFOHEADER *videoInfoHeader = (VIDEOINFOHEADER*)g_pmt->pbFormat;
			//WriteBitmap(L"testpic.bmp", &videoInfoHeader->bmiHeader,
			//	g_pmt->cbFormat - SIZE_PREHEADER, pBuffer, BufferLen);
		}
		pEventSink->Notify(EC_USER, 0, BufferLen);
		return S_OK;
	}
};

SampleGrabberCallback g_GrabberCB;

DWORD WINAPI GUICamera(LPVOID lpParameter)
{
	readFromFileColorThresholds();

	//make main window
	const wchar_t CLASS_NAME[] = L"Main Window Class";
	
	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = CLASS_NAME;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);

	RegisterClassEx(&wc);

	HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"ROBOPROG",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);


	ShowWindow(hwnd, SW_SHOWDEFAULT);

	//make calibration window

	const wchar_t CLASS_NAME_CALIBRATOR[] = L"Calibrator Window Class";

	WNDCLASSEX wcCalibrator = {};
	wcCalibrator.cbSize = sizeof(WNDCLASSEX);
	wcCalibrator.lpfnWndProc = WindowProcCalibrator;
	wcCalibrator.hInstance = hInstance;
	wcCalibrator.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcCalibrator.lpszClassName = CLASS_NAME_CALIBRATOR;
	wcCalibrator.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wcCalibrator.hbrBackground = (HBRUSH)(COLOR_WINDOW);

	RegisterClassEx(&wcCalibrator);

	hwndCalibrate = CreateWindowEx(WS_EX_COMPOSITED, CLASS_NAME_CALIBRATOR, L"Calibrate",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

	ShowWindow(hwndCalibrate, SW_HIDE);	

	//Signal that the initialization is done
	SetEvent(readySignal);

	// Run the message loop.
	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	//Close all threads when the GUI window closes
	ExitProcess(0);
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		HINSTANCE hInstance = GetModuleHandle(0);
		hwndEdit = CreateWindowEx(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
			0, 480, 640 + 640, 200, hwnd, (HMENU)ID_EDITCHILD, hInstance, NULL);

		HWND button1 = CreateWindowEx(0, L"BUTTON", L"N_IMG",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 0, 65, 20, hwnd, (HMENU)ID_BUTTON1, hInstance, NULL);
		HWND button2 = CreateWindowEx(0, L"BUTTON", L"PROC",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 20, 65, 20, hwnd, (HMENU)ID_BUTTON2, hInstance, NULL);
		HWND button3 = CreateWindowEx(0, L"BUTTON", L"CALIB",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 40, 65, 20, hwnd, (HMENU)ID_BUTTON3, hInstance, NULL);

		InitVideo(hwnd);
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 640 + 640 + 65, rc.bottom = 680;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		//SendMessage(hwndEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
		return 0;
	}
	case WM_PAINT:
		OnPaint(hwnd);
		return 0;
	case WM_SIZE:
		return 0;
	case WM_DESTROY:
		ShowWindow(hwndCalibrate, SW_HIDE);
		Release();
		PostQuitMessage(0);
		return 0;
	case WM_GRAPH_EVENT:
		OnGraphEvent(hwnd);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_BUTTON1:
			SetEvent(newImageSignal);
			prints(L"New Image clicked\n");
			return 0;
		case ID_BUTTON2:
			SetEvent(button2Signal);
			prints(L"Starting processing\n");
			return 0;
		case ID_BUTTON3:
			ShowWindow(hwndCalibrate, SW_SHOW);
			calibrating = TRUE;
			return 0;
		}
		break;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OnPaint(HWND hwnd) {
	RECT rc{ 640 + 640,0,640 + 640 + 65,680 };
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);
	
	FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW));
	if (hBitmap == NULL) {
		/*hBitmap = CreateDIBitmap(hdc, (const BITMAPINFOHEADER*)&bmih,
		0, g_pBuffer, (const BITMAPINFO*)&dbmi, DIB_RGB_COLORS);*/
		hBitmap = CreateDIBSection(hdc, (const BITMAPINFO*)&dbmi, DIB_RGB_COLORS, (void**)&g_pBuffer, NULL, NULL);
	}
	HDC hdcMem = CreateCompatibleDC(hdc);
	HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hBitmap);
	int a = BitBlt(hdc, 640, 0, 640, 480, hdcMem, 0, 0, SRCCOPY);
	if (hbmOld == NULL) printf("\n\nERROR here\n\n");
	SelectObject(hdcMem, hbmOld);
	DeleteDC(hdcMem);
	DeleteObject(hbmOld);

	EndPaint(hwnd, &ps);
	pDisplay->RepaintVideo();
}

LRESULT CALLBACK WindowProcCalibrator(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE: 
	{
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 400, rc.bottom = 400;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 300, 100, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		HINSTANCE hInstance = GetModuleHandle(0);
		CreateWindowEx(0, L"BUTTON", L"DONE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			100, 350, 100, 30, hwnd, (HMENU)ID_BUTTON_DONE, hInstance, NULL);
		CreateWindowEx(0, L"BUTTON", L"SAVE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			200, 350, 100, 30, hwnd, (HMENU)ID_BUTTON_SAVE, hInstance, NULL);
		
		hwndHue = CreateWindowEx(0, TRACKBAR_CLASS, L"Hue", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 20, 250, 20,hwnd,(HMENU)ID_TRACKBAR_HUE ,hInstance,0);
		hue = hueMin;
		SendMessage(hwndHue,TBM_SETRANGE, TRUE, (1000 << 16) );
		SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(hueMin * 1000 / 6));
		SendMessage(hwndHue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndHue, TBM_SETSELSTART, TRUE, (int)(hueMin*1000/6));
		SendMessage(hwndHue, TBM_SETSELEND, TRUE, (int)(hueMax * 1000 / 6));
		hwndSaturation = CreateWindowEx(0, TRACKBAR_CLASS, L"Saturation", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 60, 250, 20, hwnd, (HMENU)ID_TRACKBAR_SATURATION, hInstance, 0);
		saturation = saturationMin;
		SendMessage(hwndSaturation, TBM_SETRANGE, TRUE, (1000 << 16) );
		SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndSaturation, TBM_SETSELSTART, TRUE, (int)(saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETSELEND, TRUE, (int)(saturationMax * 1000));
		hwndValue = CreateWindowEx(0, TRACKBAR_CLASS, L"Value", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 100, 250, 20, hwnd, (HMENU)ID_TRACKBAR_VALUE, hInstance, 0);
		value = valueMin;
		SendMessage(hwndValue, TBM_SETRANGE, TRUE, (1000 << 16) );
		SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(valueMin * 1000));
		SendMessage(hwndValue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndValue, TBM_SETSELSTART, TRUE, (int)(valueMin * 1000));
		SendMessage(hwndValue, TBM_SETSELEND, TRUE, (int)(valueMax * 1000));

		 CreateWindowEx(0, L"BUTTON", L"Set thresholds for the desired color:", 
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
			10, 130, 350, 90, hwnd, (HMENU)ID_RADIOBOXGROUP_MINMAX, hInstance, NULL);
		HWND minRadioButton = CreateWindowEx(0, L"BUTTON", L"Minimum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10+10, 130+20, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MIN, hInstance, NULL);
		SendMessage(minRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		currentCalibratorSetting = minimum;
		CreateWindowEx(0, L"BUTTON", L"Maximum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10+10, 130+50, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MAX, hInstance, NULL);
		return 0;
	}
	case WM_PAINT:
		OnPaintCalibrator(hwnd);
		return 0;
	case WM_SIZE:
		return 0;
	case WM_DESTROY:
		ShowWindow(hwnd, SW_HIDE);
		calibrating = FALSE;
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		calibrating = FALSE;
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_BUTTON_DONE:
			ShowWindow(hwnd, SW_HIDE);
			calibrating = FALSE;
			return 0;
		case ID_BUTTON_SAVE:
			saveToFileColorThresholds();
			return 0;
		case ID_RADIOBOX_MIN:
			currentCalibratorSetting = minimum;
			SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(hueMin * 1000 / 6));
			SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(saturationMin * 1000));
			SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(valueMin * 1000));
			return 0;
		case ID_RADIOBOX_MAX:
			currentCalibratorSetting = maximum;
			SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(hueMax * 1000 / 6));
			SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(saturationMax * 1000));
			SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(valueMax * 1000));
			return 0;
		}

		break;
	case WM_HSCROLL:
		if (LOWORD(wParam) == TB_THUMBTRACK || LOWORD(wParam) == TB_ENDTRACK 
				|| LOWORD(wParam) == TB_PAGEDOWN || LOWORD(wParam) == TB_PAGEUP) {
			RECT rc{ 120,250,280,330 };
			InvalidateRect(hwnd, &rc, FALSE);
			rc = { 0,0,150,120 };
			InvalidateRect(hwnd, &rc, FALSE);
			prints(L"H %.2f %.2f S %.2f %.2f V %.2f %.2f\n", hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax);
			if ((HWND)lParam == hwndHue) {
				int currentPosition = SendMessage(hwndHue, TBM_GETPOS, 0, 0);
				hue = (float)currentPosition * 6 / 1000;
				if (currentCalibratorSetting == minimum) {
					SendMessage((HWND)lParam, TBM_SETSELSTART, TRUE, currentPosition);
					hueMin = hue;
					if (hue > hueMax) hueMax = hue;
				}
				else {
					SendMessage((HWND)lParam, TBM_SETSELEND, TRUE, currentPosition);
					hueMax = hue;
					if (hue < hueMin) hueMin = hue;
				}
				return 0;
			}
			else if ((HWND)lParam == hwndSaturation) {
				int currentPosition = SendMessage(hwndSaturation, TBM_GETPOS, 0, 0);
				saturation = (float)currentPosition / 1000;
				if (currentCalibratorSetting == minimum) {
					SendMessage((HWND)lParam, TBM_SETSELSTART, TRUE, currentPosition);
					saturationMin = saturation;
					if (saturation > saturationMax) saturationMax = saturation;
				}
				else {
					SendMessage((HWND)lParam, TBM_SETSELEND, TRUE, currentPosition);
					saturationMax = saturation;
					if (saturation < saturationMin) saturationMin = saturation;
				}
				return 0;
			}
			else if ((HWND)lParam == hwndValue) {
				int currentPosition = SendMessage(hwndValue, TBM_GETPOS, 0, 0);
				value = (float)currentPosition / 1000;
				if (currentCalibratorSetting == minimum) {
					SendMessage((HWND)lParam, TBM_SETSELSTART, TRUE, currentPosition);
					valueMin = value;
					if (value > valueMax) valueMax = value;
				}
				else {
					SendMessage((HWND)lParam, TBM_SETSELEND, TRUE, currentPosition);
					valueMax = value;
					if (value < valueMin) valueMin = value;
				}
				return 0;
			}	
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OnPaintCalibrator(HWND hwnd) {
	wchar_t buf[24];
	int len = 0;
	RECT rc;
	GetClientRect(hwnd, &rc);
	RECT rc2 = { 120,250,280,330 };		//rectangle where the current color is drawn
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);

	//ExcludeClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);
	FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW));
	//IntersectClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);

	SetBkMode(hdc, TRANSPARENT); //for text printing, doesn't draw a box around the text

	TextOutW(hdc, 10, 20, buf, swprintf(buf, 20, L"Hue:        %.4f", hue));
	TextOutW(hdc, 10, 60, buf, swprintf(buf, 20, L"Saturation: %.4f", saturation));
	TextOutW(hdc, 10, 100, buf, swprintf(buf, 20, L"Value:     %.4f", value));

	//HRGN hrgn = CreateRectRgn(120, 250, 280, 330);
	//SelectClipRgn(hdc, hrgn);
	HBRUSH rectBrush = CreateSolidBrush(HSVtoRGB(hue, saturation, value));
	FillRect( hdc, &rc2, rectBrush );
	DeleteObject(rectBrush);

	EndPaint(hwnd, &ps);
}

void InitVideo(HWND hwnd) {
	// Initialize the COM library.
	hr = CoInitialize(NULL);

	// Create the Capture Graph Builder.
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&pBuild);
	// Create the Filter Graph Manager.
	hr = CoCreateInstance(CLSID_FilterGraph, 0, CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder, (void**)&pGraph);
	// Initialize the Capture Graph Builder.
	pBuild->SetFiltergraph(pGraph);

	hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pControl);
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pEvent);
	hr = pGraph->QueryInterface(IID_IMediaEventSink, (void **)&pEventSink);
	hr = pEvent->SetNotifyWindow((OAHWND)hwnd, WM_GRAPH_EVENT, NULL);

	// Create the System Device Enumerator.
	ICreateDevEnum *pSysDevEnum = NULL;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&pSysDevEnum);

	// Obtain a class enumerator for the video input category.
	IEnumMoniker *pEnumCat = NULL;
	hr = pSysDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnumCat, 0);
	IBaseFilter *pCap = NULL;

	if (hr == S_OK)
	{
		// Enumerate the monikers.
		IMoniker *pMoniker = NULL;
		ULONG cFetched;
		while (pEnumCat->Next(1, &pMoniker, &cFetched) == S_OK)
		{
			IPropertyBag *pPropBag;
			hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag,
				(void **)&pPropBag);
			if (SUCCEEDED(hr))
			{
				// To retrieve the filter's friendly name, do the following:
				VARIANT varName;
				VariantInit(&varName);
				hr = pPropBag->Read(L"FriendlyName", &varName, 0);
				if (SUCCEEDED(hr))
				{
					// Add the filter if the name is appropriate.
					if (wcscmp(varName.bstrVal, L"Philips SPC 900NC PC Camera") == 0 ||1) {
						VariantClear(&varName);
						hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pCap);
						if (SUCCEEDED(hr))
						{
							hr = pGraph->AddFilter(pCap, L"Capture Filter");
						}
					}
				}
				VariantClear(&varName);
			}
			pMoniker->Release();
		}
		pEnumCat->Release();
	}
	pSysDevEnum->Release();

	//List media types and get the correct one to pmt
	IEnumPins *pEnum = NULL;
	IPin *pPin = NULL;
	hr = pCap->EnumPins(&pEnum);
	pEnum->Next(1, &pPin, NULL);
	IEnumMediaTypes *ppEnum;
	pPin->EnumMediaTypes(&ppEnum);
	AM_MEDIA_TYPE *pmt;
	VIDEOINFOHEADER* videoInfoHeader = NULL;
	while (hr = ppEnum->Next(1, &pmt, NULL), hr == 0) {
		videoInfoHeader = (VIDEOINFOHEADER*)pmt->pbFormat;
		//        printf("Width: %d, Height: %d, BitCount: %d, Compression: %X\n",
		//               videoInfoHeader->bmiHeader.biWidth,videoInfoHeader->bmiHeader.biHeight,
		//               videoInfoHeader->bmiHeader.biBitCount,videoInfoHeader->bmiHeader.biCompression);
		//        printf("Image size: %d, Bitrate KB: %.2f, FPS: %.2f\n",
		//               videoInfoHeader->bmiHeader.biSizeImage,videoInfoHeader->dwBitRate/(8.0*1000),
		//               10000000.0/(videoInfoHeader->AvgTimePerFrame));
		if (videoInfoHeader->bmiHeader.biWidth == 640 &&
			videoInfoHeader->bmiHeader.biBitCount == 16) break;
	}
	ppEnum->Release();
	pPin->Release();
	pEnum->Release();

	//set the output of the camera to 640x480px and 16 bit YUY2 format.
	IAMStreamConfig *pConfig = NULL;
	hr = pBuild->FindInterface(
		&PIN_CATEGORY_CAPTURE, // Preview pin.
		NULL,    // Any media type.
		pCap, // Pointer to the capture filter.
		IID_IAMStreamConfig, (void**)&pConfig);
	hr = pConfig->SetFormat(pmt);
	pConfig->Release();

	//set up the grabber for grabbing frames from the data stream:
	IBaseFilter *pGrabberF = NULL;
	ISampleGrabber *pGrabber = NULL;

	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pGrabberF));

	ZeroMemory(pmt, sizeof(*pmt));
	pmt->majortype = MEDIATYPE_Video;
	pmt->subtype = MEDIASUBTYPE_RGB32;

	hr = pGraph->AddFilter(pGrabberF, L"Sample Grabber");
	hr = pGrabberF->QueryInterface(IID_ISampleGrabber, (void**)&pGrabber);
	hr = pGrabber->SetOneShot(FALSE);
	hr = pGrabber->SetBufferSamples(TRUE);
	hr = pGrabber->SetCallback(&g_GrabberCB, 1);
	hr = pGrabber->SetMediaType(pmt);

	//Video renderer setup

	IBaseFilter *pEVR = NULL;
	hr = CoCreateInstance(CLSID_EnhancedVideoRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pEVR));
	IMFGetService *pGS = NULL;

	hr = pEVR->QueryInterface(IID_PPV_ARGS(&pGS));
	hr = pGS->GetService(MR_VIDEO_RENDER_SERVICE, IID_PPV_ARGS(&pDisplay));
	hr = pDisplay->SetVideoWindow(hwnd);
	hr = pDisplay->SetAspectRatioMode(MFVideoARMode_PreservePicture);
	pGraph->AddFilter(pEVR, L"Video renderer");

	//this connects the three filters together and adds whatever else filters are necessary to get RGB32 for the grabber
	hr = pBuild->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, pCap, pGrabberF, pEVR); //returns VFW_S_NOPREVIEWPIN

																							   //store the media type and set up the bitmap headers, so that we can later create a bitmap
	pGrabber->GetConnectedMediaType(g_pmt);
	bmih = (((VIDEOINFOHEADER *)g_pmt->pbFormat)->bmiHeader);
	ZeroMemory(&dbmi, sizeof(dbmi));
	dbmi.bmiHeader = bmih;

	//run the thing
	pControl->Run();

	//set the video position in the window
	RECT rc = { 0,0,640,480 };
	pDisplay->SetVideoPosition(NULL, &rc);

	//release the object no longer needed
	pControl->Release();
	pCap->Release();
	pGrabberF->Release();
	pGrabber->Release();
	pBuild->Release();
}

void Release() {
	DeleteObject(hBitmap);
	pEventSink->Release();
	pEvent->Release();
	pGraph->Release();
	CoUninitialize();
}

void PrintFilters(IGraphBuilder *pGraph) {
	IEnumFilters *pEnum = NULL;
	IBaseFilter *pFilter;
	ULONG cFetched;
	HRESULT hr = pGraph->EnumFilters(&pEnum);
	while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
	{
		FILTER_INFO FilterInfo;
		hr = pFilter->QueryFilterInfo(&FilterInfo);
		wprintf(L"%s\n", FilterInfo.achName);
		FilterInfo.pGraph->Release();
		pFilter->Release();
	}
	pEnum->Release();
}

//this writes a buffer to a .bmp file
HRESULT WriteBitmap(LPCWSTR pszFileName, BITMAPINFOHEADER *pBMI, size_t cbBMI,
	BYTE *pData, size_t cbData)
{
	HANDLE hFile = CreateFile(pszFileName, GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, 0, NULL);
	if (hFile == NULL)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	BITMAPFILEHEADER bmf = {};

	bmf.bfType = 'MB';
	bmf.bfSize = cbBMI + cbData + sizeof(bmf);
	bmf.bfOffBits = sizeof(bmf) + cbBMI;

	DWORD cbWritten = 0;
	BOOL result = WriteFile(hFile, &bmf, sizeof(bmf), &cbWritten, NULL);
	if (result)
	{
		result = WriteFile(hFile, pBMI, cbBMI, &cbWritten, NULL);
	}
	if (result)
	{
		result = WriteFile(hFile, pData, cbData, &cbWritten, NULL);
	}

	HRESULT hr = result ? S_OK : HRESULT_FROM_WIN32(GetLastError());

	CloseHandle(hFile);

	return hr;
}

//this method is called when the window message loop gets a 
//notification that there is a message from the filter graph manager
void CALLBACK OnGraphEvent(HWND hwnd)
{
	long evCode;
	LONG_PTR param1, param2;
	// Get the events from the queue.
	while (SUCCEEDED(pEvent->GetEvent(&evCode, &param1, &param2, 0)))
	{
		// Process the message.
		switch (evCode)
		{
		case EC_COMPLETE:
		case EC_USERABORT:
		case EC_ERRORABORT:
			printf("GRAPH EVENT ERROR");
			PostQuitMessage(1);
			Release();
			break;
		case EC_USER:
			const RECT rc{ 640,0,640 + 640,480 };
			InvalidateRect(hwnd, &rc, FALSE);
			break;
		}

		// Free the event data.
		hr = pEvent->FreeEventParams(evCode, param1, param2);
		if (FAILED(hr))
		{
			break;
		}
	}

}

//appends text to the end of the edit control
void AppendToEditControl(wchar_t * text) {
	int outLength = GetWindowTextLength(hwndEdit);
	//SetFocus(hwndEdit);
	SendMessage(hwndEdit, EM_SETSEL, (WPARAM)outLength, (LPARAM)outLength);
	SendMessage(hwndEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
}

//prints text to the edit control, has the wprintf syntax
void prints(wchar_t* text, ...) {
	wchar_t buffer[1000];
	va_list args;
	va_start(args, text);
	vswprintf_s(buffer, text, args);
	AppendToEditControl(buffer);
	va_end(args);
}

DWORD HSVtoRGB(float hue, float saturation, float value) {
	float red, green, blue;
	if (hue < 1) {
		red = value*255; //max
		green = (hue*saturation*value + value*(1 - saturation))*255; //med
		blue = value*(1 - saturation) * 255; //min
	}
	else if (hue < 2) {
		hue = 2 - hue;
		green = value * 255;
		red = (hue*saturation*value + value*(1 - saturation)) * 255;
		blue = value*(1 - saturation) * 255;
	}
	else if (hue < 3) {
		hue = hue-2;
		green = value * 255;
		blue = (hue*saturation*value + value*(1 - saturation)) * 255;
		red = value*(1 - saturation) * 255;
	}
	else if (hue < 4) {
		hue = 4 - hue;
		blue = value * 255;
		green = (hue*saturation*value + value*(1 - saturation)) * 255;
		red = value*(1 - saturation) * 255;
	}
	else if (hue < 5) {
		hue = hue - 4;
		blue = value * 255;
		red = (hue*saturation*value + value*(1 - saturation)) * 255;
		green = value*(1 - saturation) * 255;
	}
	else if (hue <= 6) {
		hue = 6 - hue;
		red = value * 255;
		blue = (hue*saturation*value + value*(1 - saturation)) * 255;
		green = value*(1 - saturation) * 255;
	}
	return (int)red + ((int)green << 8) + ((int)blue << 16);
}

void saveToFileColorThresholds() {
	HANDLE dataFile = CreateFile(L"data.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD numberOfBytesRead;
	WriteFile(dataFile, &hueMin, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &hueMax, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &saturationMin, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &saturationMax, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &valueMin, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &valueMax, 4, &numberOfBytesRead, NULL);
	CloseHandle(dataFile);
}

void readFromFileColorThresholds() {
	HANDLE dataFile = CreateFile(L"data.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD numberOfBytesRead;
	ReadFile(dataFile, &hueMin, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &hueMax, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &saturationMin, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &saturationMax, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &valueMin, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &valueMax, 4, &numberOfBytesRead, NULL);
	CloseHandle(dataFile);
}