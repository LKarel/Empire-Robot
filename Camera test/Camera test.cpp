// Camera test.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "qedit.h"

const wchar_t CLASS_NAME[] = L"Main Window Class";
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void InitVideo(HWND hwnd);
HWND hwndMain = NULL;
HWND hwndVideo = NULL;
BITMAPINFOHEADER bmih;
BITMAPINFO dbmi;
AM_MEDIA_TYPE *g_pmt = (AM_MEDIA_TYPE*)new byte[100];
#define WM_GRAPH_EVENT (WM_APP + 1)

IMFVideoDisplayControl *pDisplay = NULL;

void prints(wchar_t* text, ...) {
	va_list args;
	va_start(args, text);
	vwprintf(text, args);
	va_end(args);
}

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
		GdiFlush();
		return S_OK;
	}
};

SampleGrabberCallback g_GrabberCB;


int main()
{
	printf("new version3\n");
	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = CLASS_NAME;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	//wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);

	RegisterClassEx(&wc);

	hwndMain = CreateWindowEx(0, CLASS_NAME, L"ROBOPROG",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

	ShowWindow(hwndMain, SW_SHOWDEFAULT);

	//make video child window for EVR player:

	const wchar_t CLASS_NAME_VIDEO[] = L"Video Window Class";

	WNDCLASSEX wcVideo;
	CopyMemory(&wcVideo, &wc, sizeof(WNDCLASSEX));
	wcVideo.lpfnWndProc = VideoWindowProc;
	wcVideo.lpszClassName = CLASS_NAME_VIDEO;

	RegisterClassEx(&wcVideo);

	hwndVideo = CreateWindowEx(WS_EX_COMPOSITED, CLASS_NAME_VIDEO, L"Video Window",
		WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, hwndMain, NULL, hInstance, NULL);

	ShowWindow(hwndVideo, SW_SHOW);

	MSG msg = {};
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 640 + 640 + 65, rc.bottom = 680;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		return 0;
	}
	case WM_PAINT:
		RECT rc{ 700,0,800,170 };
		PAINTSTRUCT ps;
		HDC hdc;
		hdc = BeginPaint(hwnd, &ps);
		FillRect(hdc, &rc, (HBRUSH)(COLOR_MENUBAR));
		EndPaint(hwnd, &ps);
		InvalidateRect(hwndVideo, &rc, FALSE);
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE:
		SetWindowPos(hwnd, HWND_TOP, 0, 0, 640, 480, NULL);
		printf("Initializing camera yeah\n");
		//InitVideo(hwnd);
		printf("Camera initialized\n");
		return 0;
	case WM_PAINT:
	{
		RECT rc{ 100,100,200,200 };
		PAINTSTRUCT ps;
		HDC hdc;
		hdc = BeginPaint(hwnd, &ps);
		printf("painting child\n");
		//pDisplay->RepaintVideo();
		FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW));
		EndPaint(hwnd, &ps);

		return 0;
	}
	case WM_GRAPH_EVENT:
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void InitVideo(HWND hwnd) {
	IMediaControl *pControl = NULL;
	IMediaEventEx   *pEvent = NULL;
	IMediaEventSink   *pEventSink = NULL;
	IGraphBuilder *pGraph = NULL;
	ICaptureGraphBuilder2 *pBuild = NULL;
	HRESULT hr;

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
				prints(varName.bstrVal);
				if (SUCCEEDED(hr))
				{
					// Add the filter if the name is appropriate.
					if (wcscmp(varName.bstrVal, L"HD Pro Webcam C920") == 0 ||
						wcscmp(varName.bstrVal, L"Philips SPC 900NC PC Camera") == 0 ||
						wcscmp(varName.bstrVal, L"Integrated Webcam") == 0 || 1) {
						VariantClear(&varName);
						hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pCap);
						if (SUCCEEDED(hr))
						{
							hr = pGraph->AddFilter(pCap, L"Capture Filter");
							pMoniker->Release();
							break;
						}
						else {
							prints(L"camera error");
						}
					}
					else {
						prints(L"camera error");
					}
				}
				else {
					prints(L"camera error");
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
			videoInfoHeader->bmiHeader.biBitCount == 16) {
			break;
		}
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
	hr = pGraph->AddFilter(pEVR, L"Video renderer");

	//this connects the three filters together and adds whatever else filters are necessary to get RGB32 for the grabber
	hr = pBuild->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, pCap, pGrabberF, pEVR); //returns VFW_S_NOPREVIEWPIN

	//store the media type and set up the bitmap headers, so that we can later create a bitmap
	pGrabber->GetConnectedMediaType(g_pmt);
	bmih = (((VIDEOINFOHEADER *)g_pmt->pbFormat)->bmiHeader);
	ZeroMemory(&dbmi, sizeof(dbmi));
	dbmi.bmiHeader = bmih;

	//set the video position in the window
	RECT rc = { 0,0,640,480 };
	hr = pDisplay->SetVideoPosition(NULL, &rc);
	if (!SUCCEEDED(hr))
		prints(L"error: %d\n", hr);

	//run the thing
	pControl->Run();

	//release the objects no longer needed
	pControl->Release();
	pCap->Release();
	pGrabberF->Release();
	pGrabber->Release();
	pBuild->Release();
}
