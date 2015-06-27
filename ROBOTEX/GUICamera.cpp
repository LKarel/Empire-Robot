#include "stdafx.h"
#include "GUICamera.h"
#include "qedit.h"

#define ID_EDITCHILD 100

AM_MEDIA_TYPE *g_pmt = (AM_MEDIA_TYPE*)new byte[100];

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hwnd);
void InitVideo(HWND hwnd);
void PrintFilters(IGraphBuilder *pGraph);
void Release();
HRESULT WriteBitmap(LPCWSTR, BITMAPINFOHEADER*, size_t, BYTE *, size_t);
void CALLBACK OnGraphEvent(HWND hwnd);
void prints(wchar_t* text, ...);

const UINT WM_GRAPH_EVENT = WM_APP + 1;

IMediaControl *pControl = NULL;
IMediaEventEx   *pEvent = NULL;
IMediaEventSink   *pEventSink = NULL;
IGraphBuilder *pGraph = NULL;
ICaptureGraphBuilder2 *pBuild = NULL;
IMFVideoDisplayControl *pDisplay = NULL;
HRESULT hr;
BYTE *g_pBuffer = NULL;
BOOL start = TRUE;
BITMAPINFOHEADER bmih;
BITMAPINFO dbmi;
HBITMAP hBitmap = NULL;
HWND hwndEdit = NULL;
extern HANDLE signal;

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
		if (g_pBuffer != NULL) {
			GdiFlush();
			CopyMemory(g_pBuffer, pBuffer, BufferLen);
		}
		//printf("\nBufferCB %ld %ld\n\n", BufferLen, pBuffer);
		//VIDEOINFOHEADER *videoInfoHeader = (VIDEOINFOHEADER*)g_pmt->pbFormat;
		//WriteBitmap(L"testpic.bmp", &videoInfoHeader->bmiHeader,
		//	g_pmt->cbFormat - SIZE_PREHEADER, pBuffer, BufferLen);
		pEventSink->Notify(EC_USER, (LONG_PTR)g_pBuffer, BufferLen);
		return S_OK;
	}
};

SampleGrabberCallback g_GrabberCB;

DWORD WINAPI GUICamera(LPVOID lpParameter)
{

	const wchar_t CLASS_NAME[] = L"Sample Window Class";

	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = CLASS_NAME;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	RegisterClassEx(&wc);

	HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"ROBOPROG",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
	hwndEdit = CreateWindowEx(
		0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
		0, 480, 640 + 640, 200, hwnd, (HMENU)ID_EDITCHILD, (HINSTANCE)GetWindowLong(hwnd, GWL_HINSTANCE), NULL);
	
	ShowWindow(hwnd, SW_SHOWDEFAULT);

	//Signal that the initialization is done
	SetEvent(signal);

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
		InitVideo(hwnd);
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 640 + 640, rc.bottom = 680;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		return 0;
	case WM_PAINT:
		OnPaint(hwnd);
		return 0;
	case WM_SIZE:
		return 0;
	case WM_DESTROY:
		Release();
		PostQuitMessage(0);
		return 0;
	case WM_GRAPH_EVENT:
		OnGraphEvent(hwnd);
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OnPaint(HWND hwnd) {
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);

	if (hBitmap == NULL)
		/*hBitmap = CreateDIBitmap(hdc, (const BITMAPINFOHEADER*)&bmih,
		0, g_pBuffer, (const BITMAPINFO*)&dbmi, DIB_RGB_COLORS);*/
		hBitmap = CreateDIBSection(hdc, (const BITMAPINFO*)&dbmi, DIB_RGB_COLORS, (void**)&g_pBuffer, NULL, NULL);

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
					if (wcscmp(varName.bstrVal, L"Philips SPC 900NC PC Camera") == 0) {
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
	SetFocus(hwndEdit);
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
