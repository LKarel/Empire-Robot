#include "stdafx.h"
#include "GUICamera.h"
#include "qedit.h"

#define PI (3.1415927f)
#define MAX3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define MIN3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define OBJECTINDEX(x) ((x)&0x7FFFFFFF)	//first 31 bits, gives the index of the current ball or goal
#define BALLORGOAL(x) (((x)>>31)&1)	//32rd bit, the identifier for whether the pixel represents a ball or a goal
#define BIT32 (1<<31)
#define HUE(red,green,blue) ((red >= green && green >= blue && red > blue) ?	hue = ((float)(green - blue) / (red - blue)) :\
			(green > red && red >= blue) ?	hue = (2 - (float)(red - blue) / (green - blue)):\
			(green >= blue && blue > red) ?	hue = (2 + (float)(blue - red) / (green - red)):\
			(blue > green && green > red) ?	hue = (4 - (float)(green - red) / (blue - red)):\
			(blue > red && red >= green) ?	hue = (4 + (float)(red - green) / (blue - green)):\
			(red >= blue && blue > green) ?	hue = (6 - (float)(blue - green) / (red - green)):\
			0.0f) //Hue when the image is gray red=green=blue
#define SATURATION(red,green,blue) ((float)(MAX3(red, green, blue) - MIN3(red, green, blue)) / \
									MAX3(red, green, blue))
#define VALUE(red,green,blue) ((float)MAX3(red, green, blue)/255)

//constants for events and ID-s of GUI elements
#define ID_EDITCHILD 100
#define WM_GRAPH_EVENT (WM_APP + 1)

//used for video media type to save the type and find the right one when initializing the camera
AM_MEDIA_TYPE *g_pmt = (AM_MEDIA_TYPE*)new byte[100];

//window procedures, used in standard Windows GUI programming
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProcCalibrator(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OnPaint(HWND hwnd);
void OnPaintCalibrator(HWND hwnd);

void InitVideo(HWND hwnd);		//the main function that sets up the camera and the frame grabber
void PrintFilters(IGraphBuilder *pGraph);	//for testing the filter graph while setting up the camera
void Release();				//releases everything before quitting
HRESULT WriteBitmap(LPCWSTR, BITMAPINFOHEADER*, size_t, BYTE *, size_t); //writes a buffer to a file, for testing
void CALLBACK OnGraphEvent(HWND hwnd);	//
void prints(wchar_t* text, ...);
DWORD HSVtoRGB(float, float, float);
void saveToFileColorThresholds();
void readFromFileColorThresholds();
void drawCross(int x, int y, int color, BYTE* buffer);

//variables specific to the way the camera is set up, it's what Windows DirectShow uses
IMediaControl *pControl = NULL;
IMediaEventEx   *pEvent = NULL;
IMediaEventSink   *pEventSink = NULL;
IGraphBuilder *pGraph = NULL;
ICaptureGraphBuilder2 *pBuild = NULL;
IMFVideoDisplayControl *pDisplay = NULL;
HRESULT hr;

BYTE *g_pBuffer = NULL; //buffer of the image displayed on the right side of the screen
DWORD *pBufferCopy;		//temporary buffer used by the image analysis function, tags pixels by object index
DWORD *houghTransformBuffer;	//buffer for the values of the Hough transform, x axis is radius, y is angle in radians
BOOL start = TRUE;

//headers for the bitmap, used if you want to write an image to a file or create a bitmap for displaying
BITMAPINFOHEADER bmih;
BITMAPINFO dbmi;
HBITMAP hBitmap = NULL;

//the handles for the GUI elements
HWND hwndEdit = NULL;
HWND hwndCalibrate = NULL;
HWND hwndVideo = NULL;
HWND hwndMain = NULL;
HWND hwndHue;	//handles for the sliders in the calibrating window
HWND hwndSaturation;
HWND hwndValue;
HWND ballsRadioButton;
HWND goalsRadioButton;
HWND linesRadioButton;
HWND whitenCheckBox;
HWND minRadioButton;
HWND maxRadioButton;

BOOLEAN calibrating = FALSE;	//state variable
BOOLEAN whitenThresholdPixels;

//signals for communicating between threads
extern HANDLE readySignal;
extern HANDLE newImageAnalyzed;
HANDLE writeMutex = CreateMutex(NULL, FALSE, NULL);

enum {
	ID_BUTTON1, ID_BUTTON2, ID_BUTTON3, ID_BUTTON_DONE, ID_TRACKBAR_HUE, ID_TRACKBAR_SATURATION,
	ID_TRACKBAR_VALUE, ID_RADIOBOXGROUP_MINMAX, ID_RADIOBOX_MIN, ID_RADIOBOX_MAX, ID_BUTTON_SAVE,
	ID_BUTTON_RESET, ID_RADIOBOXGROUP_OBJECTSELECTOR, ID_RADIOBOX_BALLS, ID_RADIOBOX_GOALS, ID_RADIOBOX_LINES,
	ID_CHECKBOX_WHITEN
};

objectCollection balls, goals, ballsShare, goalsShare; //local data structure for holding objects and shared structures
lineCollection lines, linesShare; //data for the lines

//structure for storing the threshold color values of different objects
struct colorValues {
	float hue = 0; //from 0 to 6
	float hueMin;
	float hueMax;
	float saturation = 0;	//from 0 to 1
	float saturationMin;
	float saturationMax;
	float value = 0;	//from 0 to 1
	float valueMin;
	float valueMax;
} ballColors, goalColors, lineColors;
colorValues *activeColors;	//the current active color values in the calibrator

enum CurrentCalibratorSetting { minimum, maximum };
CurrentCalibratorSetting currentCalibratorSetting = minimum;

//temporary for the weekly assignment, so that we could show something that drives the engines
//void rotate(HANDLE hComm, int speed, float time) {
//	char writeBuffer[32] = {};
//	DWORD bytesWritten;
//	int len = sprintf_s(writeBuffer, "sd%d\n", speed);
//
//	WriteFile(hComm, writeBuffer, len, &bytesWritten, NULL);
//	Sleep((DWORD)(time*1000-100));
//	WriteFile(hComm, "sd0\n", 4, &bytesWritten, NULL);
//	time = time > 0.1 ? time : 0.1;
//	Sleep(100);
//}
//
//void driveTest() {
//	HANDLE hComm = CreateFile(L"\\\\.\\COM12", GENERIC_READ | GENERIC_WRITE, 0, 0,
//		OPEN_EXISTING, 0, 0);
//	if (hComm == INVALID_HANDLE_VALUE) {
//		prints(L"INVALID COM PORT HANDLE\n");
//		return;
//	}
//
//	char writeBuffer[32] = {};
//	DWORD bytesWritten;
//	WriteFile(hComm, "st\n", 3, &bytesWritten, NULL);
//
//	rotate(hComm, 30, 1);
//	rotate(hComm, 50, 0.5);
//	rotate(hComm, 80, 0.5);
//	rotate(hComm, 0, 0.5);
//	rotate(hComm, -60, 0.5);
//
//	char *readBuffer[128]{};
//	DWORD bytesRead;
//	for (int i = 0;i < 10;++i) {
//		if(!ReadFile(hComm, readBuffer, 1, &bytesRead, NULL))
//			prints(L"read error\n");
//		prints(L"%s\n", readBuffer);
//	}
//	CloseHandle(hComm);
//}

//this analyzes the image to find all the balls, the goals and the black lines
void analyzeImage(double Time, BYTE *pBuffer, long BufferLen) {
	int ballCount = 0, goalCount = 0;
	DWORD* pixBuffer = (DWORD*)pBuffer;
	ZeroMemory(pBufferCopy, 640 * 480 * 4); //zero the buffer with the indexes and object information
	ZeroMemory(houghTransformBuffer, 150 * 150 * 4);
	ZeroMemory(balls.data, balls.size*sizeof(objectInfo));
	ZeroMemory(goals.data, goals.size*sizeof(objectInfo));
	ZeroMemory(lines.data, lines.size*sizeof(lineInfo));
	balls.count = 0;
	goals.count = 0;
	lines.count = 0;
	for (int y = 0; y < 480; ++y) {
		for (int x = 0; x < 640; ++x) {
			int red = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
				blue = (pixBuffer[x + y * 640] >> 16) & 0xFF;

			float hue = HUE(red, green, blue);
			float saturation = SATURATION(red, green, blue);
			float value = VALUE(red, green, blue);

			//now the HSV values are calculated and we can check if they fit the proper criteria
			//we will check if the color values fit the color range of balls, goals or lines

			//if it is a ball pixel
			if (ballColors.hueMax >= hue && hue >= ballColors.hueMin &&
				ballColors.saturationMax >= saturation && saturation >= ballColors.saturationMin &&
				ballColors.valueMax >= value && value >= ballColors.valueMin) {
				//check if there is already a ball in the pixel below it, then group it together with it
				if (y > 0 && !BALLORGOAL(pBufferCopy[x + 640 * (y - 1)]) && 
					OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)])) {
					int index = OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)]);
					//remove the new group if it was made and decrease the ballcount
					if (x >= 1 && pBufferCopy[x - 1 + 640 * y] &&
						!BALLORGOAL(pBufferCopy[x - 1 + 640 * y])) {
						ballCount -= 1;
						balls.data[OBJECTINDEX(pBufferCopy[x - 1 + 640 * y])] = {};
					}
					//change all the pixels before to the new object index
					pBufferCopy[x + 640 * y] = index;
					balls.data[index].pixelcount += 1;
					balls.data[index].x += x;
					balls.data[index].y += y;
					for (int x2 = x - 1; x2 >= 0 && pBufferCopy[x2 + 640 * y] &&
						!BALLORGOAL(pBufferCopy[x2 + 640 * y]); --x2) {
						pBufferCopy[x2 + 640 * y] = index;
						balls.data[index].pixelcount += 1;
						balls.data[index].x += x;
						balls.data[index].y += y;
					}
					//change all the pixels forward in the line to the object index,
					//merge the objects if it meets another ball
					for (++x; x < 640; ++x) {
						int red = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
							blue = (pixBuffer[x + y * 640] >> 16) & 0xFF;
						float hue = HUE(red, green, blue);
						float saturation = SATURATION(red, green, blue);
						float value = VALUE(red, green, blue);

						if (!(ballColors.hueMax >= hue && hue >= ballColors.hueMin &&
							ballColors.saturationMax >= saturation && saturation >= ballColors.saturationMin &&
							ballColors.valueMax >= value && value >= ballColors.valueMin)) {
							--x;
							break;
						}
						balls.data[index].pixelcount += 1;
						balls.data[index].x += x;
						balls.data[index].y += y;
						pBufferCopy[x + 640 * y] = index;
						int index2 = OBJECTINDEX(pBufferCopy[x + (y - 1) * 640]);
						//there are objects with different indexes on the current pixel and below it
						if (!BALLORGOAL(pBufferCopy[x + (y-1) * 640]) && 
							index2 && index2 != index) {
							//copy the pixels and coordinates from the object under the line to the new object
							balls.data[index].pixelcount += balls.data[index2].pixelcount;
							balls.data[index].x += balls.data[index2].x;
							balls.data[index].y += balls.data[index2].y;
							balls.data[index2] = {};
							//set the last line of the object from the line under to the proper index, so that when
							//it is encoutered again, it is interpreted as the correct object
							for (int x2 = x; x2 <= 640 && !BALLORGOAL(pBufferCopy[x2 + (y - 1) * 640]) && 
									OBJECTINDEX(pBufferCopy[x2 + (y - 1) * 640]) == index2; ++x2) {
								pBufferCopy[x2 + (y - 1) * 640] = index;
							}
						}
					}
				}
				else {
					//if there is a ball pixel to the left
					if (x > 0 && !BALLORGOAL(pBufferCopy[x - 1 + 640 * y]) && pBufferCopy[x - 1 + 640 * y]){
						int index = OBJECTINDEX(pBufferCopy[x - 1 + 640 * y]);
						balls.data[index].x += x;
						balls.data[index].y += y;
						balls.data[index].pixelcount += 1;
						pBufferCopy[x + 640 * y] = index;
					}
					//start a new object
					else {
						++ballCount;
						balls.count = ballCount;
						pBufferCopy[x + 640 * y] = ballCount;
						if (ballCount == balls.size)
							doubleObjectBufferSize(&balls);
						balls.data[ballCount].pixelcount += 1;
						balls.data[ballCount].x += x;
						balls.data[ballCount].y += y;
					}
				}
			}
			//if it is a goal pixel
			else if (goalColors.hueMax >= hue && hue >= goalColors.hueMin &&
				goalColors.saturationMax >= saturation && saturation >= goalColors.saturationMin &&
				goalColors.valueMax >= value && value >= goalColors.valueMin) {
				//check if there is already a goal in the pixel below it, then group it together with it
				if (y > 0 && BALLORGOAL(pBufferCopy[x + 640 * (y - 1)]) &&
					OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)])) {
					int index = OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)]);
					//remove the new group if it was made and decrease the ballcount
					if (x >= 1 && pBufferCopy[x - 1 + 640 * y] &&
						BALLORGOAL(pBufferCopy[x - 1 + 640 * y])) {
						goalCount -= 1;
						goals.data[OBJECTINDEX(pBufferCopy[x - 1 + 640 * y])] = {};
					}
					//change all the pixels before to the new object index
					pBufferCopy[x + 640 * y] = index + BIT32;
					goals.data[index].pixelcount += 1;
					goals.data[index].x += x;
					goals.data[index].y += y;
					for (int x2 = x - 1; x2 >= 0 && pBufferCopy[x2 + 640 * y] &&
						BALLORGOAL(pBufferCopy[x2 + 640 * y]); --x2) {
						pBufferCopy[x2 + 640 * y] = index + BIT32;
						goals.data[index].pixelcount += 1;
						goals.data[index].x += x;
						goals.data[index].y += y;
					}
					//change all the pixels forward in the line to the object index,
					//merge the objects if it meets another ball
					for (++x; x < 640; ++x) {
						int red = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
							blue = (pixBuffer[x + y * 640] >> 16) & 0xFF;
						float hue = HUE(red, green, blue);
						float saturation = SATURATION(red, green, blue);
						float value = VALUE(red, green, blue);

						if (!(goalColors.hueMax >= hue && hue >= goalColors.hueMin &&
							goalColors.saturationMax >= saturation && saturation >= goalColors.saturationMin &&
							goalColors.valueMax >= value && value >= goalColors.valueMin)) {
							--x;
							break;
						}
						goals.data[index].pixelcount += 1;
						goals.data[index].x += x;
						goals.data[index].y += y;
						pBufferCopy[x + 640 * y] = index + BIT32;
						int index2 = OBJECTINDEX(pBufferCopy[x + (y - 1) * 640]);
						//there are objects with different indexes on the current pixel and below it
						if (BALLORGOAL(pBufferCopy[x + (y - 1) * 640]) &&
							index2 && index2 != index) {
							//copy the pixels and coordinates from the object under the line to the new object
							goals.data[index].pixelcount += goals.data[index2].pixelcount;
							goals.data[index].x += goals.data[index2].x;
							goals.data[index].y += goals.data[index2].y;
							goals.data[index2] = {};
							//set the last line of the object from the line under to the proper index, so that when
							//it is encoutered again, it is interpreted as the correct object
							for (int x2 = x; x2 <= 640 && BALLORGOAL(pBufferCopy[x2 + (y - 1) * 640]) &&
								OBJECTINDEX(pBufferCopy[x2 + (y - 1) * 640]) == index2; ++x2) {
								pBufferCopy[x2 + (y - 1) * 640] = index + BIT32;
							}
						}
					}
				}
				else {
					//if there is a ball pixel to the left
					if (x > 0 && BALLORGOAL(pBufferCopy[x - 1 + 640 * y]) && pBufferCopy[x - 1 + 640 * y]) {
						int index = OBJECTINDEX(pBufferCopy[x - 1 + 640 * y]);
						goals.data[index].x += x;
						goals.data[index].y += y;
						goals.data[index].pixelcount += 1;
						pBufferCopy[x + 640 * y] = index + BIT32;
					}
					//start a new object
					else {
						++goalCount;
						goals.count = goalCount;
						pBufferCopy[x + 640 * y] = goalCount + BIT32;
						if (goalCount == goals.size)
							doubleObjectBufferSize(&goals);
						goals.data[ballCount].pixelcount += 1;
						goals.data[ballCount].x += x;
						goals.data[ballCount].y += y;
					}
				}
			}
			//if it is a line pixel
			else if (lineColors.hueMax >= hue && hue >= lineColors.hueMin &&
				lineColors.saturationMax >= saturation && saturation >= lineColors.saturationMin &&
				lineColors.valueMax >= value && value >= lineColors.valueMin) {
				//wprintf(L"line x: %d, y: %d\n", x, y);
				for (int i = 0; i < 150; ++i) {
					float angle = i * 2 * PI / 150;
					float radiusIndex = ((x-320)*cosf(angle) + (y-240)*sinf(angle))/4; //divide by 4 because the radius array is in steps of 4 pixels
					if (radiusIndex >= 0 && radiusIndex < 150) {
						houghTransformBuffer[(int)floorf(radiusIndex)+ 150 * i] += 1;
						houghTransformBuffer[(int)ceilf(radiusIndex) + 150 * i] += 1;
					}
				}
			}
		}
	}
	//check if there are enough pixels in an object, get rid of the other ones and count the balls
	ballCount = 0;
	//printf("balls count before: %d\n", balls.count);
	for (int i = 0; i <= balls.count; ++i) {
		if (balls.data[i].pixelcount >= 100) {
			balls.data[ballCount] = balls.data[i];
			balls.data[ballCount].x /= balls.data[ballCount].pixelcount;
			balls.data[ballCount].y /= balls.data[ballCount].pixelcount;
			//printf("x %d y %d pix %d\n", balls.data[ballCount].x, 
			//	balls.data[ballCount].y, balls.data[ballCount].pixelcount);
			++ballCount;
		}
	}
	balls.count = ballCount;
	//printf("balls count: %d\n", balls.count);

	//printf("herenow\n");
	goalCount = 0;
	for (int i = 0; i <= goals.count; ++i) {
		if (goals.data[i].pixelcount >= 100) {
			goals.data[goalCount] = goals.data[i];
			goals.data[goalCount].x /= goals.data[goalCount].pixelcount;
			goals.data[goalCount].y /= goals.data[goalCount].pixelcount;
			//printf("x %d y %d pix %d\n", balls.data[ballCount].x, 
			//	balls.data[ballCount].y, balls.data[ballCount].pixelcount);
			++goalCount;
		}
	}
	goals.count = goalCount;
	//printf("herenow2\n");

	//get the lines from the Hough transform accumulator
	for (int angle = 0; angle < 150; ++angle) {
		for (int r = 0; r < 150; ++r) {
			int current = houghTransformBuffer[r + 150 * angle] / 2;
			int left = r > 0 ? houghTransformBuffer[r - 1 + 150 * angle] / 2 : 0;
			int up = angle < 150-1 ? houghTransformBuffer[r + 150 * (angle + 1)] / 2 : 0;
			int right = r < 150-1 ? houghTransformBuffer[r + 1 + 150 * angle] / 2 : 0;
			int down = angle > 0 ? houghTransformBuffer[r + 150 * (angle - 1)] / 2 : 0;
			if (current > 100 && current >= left && current >= down && current > right && current > up) {
				lines.data[lines.count].radius = 4*r;
				lines.data[lines.count].angle = angle*2*PI/150;
				lines.data[lines.count].pixelcount = current;
				lines.count++;
				if (lines.count == lines.size)
					doubleObjectBufferSize((objectCollection*)&lines);
				//wprintf(L"current: %d, pixelcount: %d\n", current, lines.data[lines.count - 1].pixelcount);
			}
		}
	}

	//send the data to the other thread
	WaitForSingleObject(writeMutex, INFINITE);
	while (balls.count >= ballsShare.size)
		doubleObjectBufferSize(&ballsShare);
	ballsShare.count = balls.count;
	CopyMemory(ballsShare.data, balls.data, balls.count*sizeof(objectInfo));

	while (goals.count >= goalsShare.size)
		doubleObjectBufferSize(&goalsShare);
	goalsShare.count = goals.count;
	CopyMemory(goalsShare.data, goals.data, goals.count*sizeof(objectInfo));

	while (lines.count >= linesShare.size)
		doubleObjectBufferSize((objectCollection*)&linesShare);
	linesShare.count = lines.count;
	CopyMemory(linesShare.data, lines.data, lines.count*sizeof(lineInfo));
	ReleaseMutex(writeMutex);
	SetEvent(newImageAnalyzed);
}

//draws a cross of the color #0xAABBGGRR to coordinates x, y starting from the bottom left corner (Windows bitmap uses that)
void drawCross(int x, int y, int color, BYTE* buffer) {
	DWORD *pixBuffer = (DWORD *)buffer;
	for (int j = -1;j <= 1;++j)
		for (int i = -10;i <= 10;++i)
			if (0 <= x + i && x + i < 640 && 0 <= y + j && y + j < 480)
				pixBuffer[x + i + 640 * (y + j)] = color;
	for (int j = -1;j <= 1;++j)
		for (int i = -10;i <= 10;++i)
			if (0 <= x + j && x + j < 640 && 0 <= y + i && y + i < 480)
				pixBuffer[x + j + 640 * (y + i)] = color;
}

void drawLine(float angle, int radius, int color, BYTE* buffer) {
	//x*cos+y*sin=r is the equation for the points x,y
	DWORD *pixBuffer = (DWORD *)buffer;
	float sin = sinf(angle);
	float cos = cosf(angle);
	if (sin < 1/1.41 && sin > -1/1.41) { //if the line is more vertical than horizontal
		for (int y = 0; y < 480; ++y) {
			int x = int(((float)radius - (float)(y - 240)*sin) / cos) + 320;
			if(x < 640 && x >= 0)
				pixBuffer[y * 640 + x] = color;
		}
	}
	else{
		for (int x = 0; x < 640; ++x) {
			int y = int(((float)radius - (float)(x - 320)*cos) / sin) + 240;
			if(y < 480 && y >= 0)
				pixBuffer[y * 640 + x] = color;
		}
	}
}

//this is the class that is needed for grabbing frames, the method BufferCB gets called everytime
//there is a new frame ready with a pointer to the data
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

	//this method receives the buffer from Windows DirectShow every time a new frame has arrived
	STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long BufferLen)
	{
		//if the calibrator is open, black out the pixels that aren't in the range of the current selection
		//in the calibrator window
		if (calibrating && g_pBuffer != NULL) {
			CopyMemory(g_pBuffer, pBuffer, BufferLen);
			for (DWORD *pixBuffer = (DWORD*)g_pBuffer; pixBuffer < (DWORD*)g_pBuffer + BufferLen/4; ++pixBuffer) {
				DWORD pixel = *pixBuffer;
				BYTE red = pixel & 0xFF, green = (pixel >> 8) & 0xFF, blue = (pixel >> 16) & 0xFF;
				
				float hue = HUE(red, green, blue);
				float saturation = SATURATION(red,green,blue);
				float value = VALUE(red, green, blue);

				if (hue < activeColors->hueMin || hue > activeColors->hueMax ||
					saturation < activeColors->saturationMin || saturation > activeColors->saturationMax ||
					value < activeColors->valueMin || value > activeColors->valueMax)
					*pixBuffer = 0;
				else if (whitenThresholdPixels)
					*pixBuffer = 0xFFFFFF;
			}
			GdiFlush();
		}
		//if not calibrating, analyze each new image and draw crosses where the balls are at
		else if(g_pBuffer != NULL)
		{
			CopyMemory(g_pBuffer, pBuffer, BufferLen);
			analyzeImage(Time, pBuffer, BufferLen);
			//prints(L"analyzed %d\n", balls.count);
			for (int i = 0; i < balls.count; ++i) {
				drawCross(balls.data[i].x, balls.data[i].y, 0xFFFFFF, g_pBuffer);
			}
			for (int i = 0; i < goals.count; ++i) {
				drawCross(goals.data[i].x, goals.data[i].y, 0x0, g_pBuffer);
			}
			for (int i = 0; i < lines.count; ++i) {
				drawLine(lines.data[i].angle, lines.data[i].radius, 0x000000FF, g_pBuffer);
			}
			drawCross(320, 240, 0, g_pBuffer);
			//drawLine(4, 52, 0x000000FF, g_pBuffer);
			SetEvent(newImageAnalyzed);
			GdiFlush();
			//printf("\nBufferCB %ld %ld\n\n", BufferLen, pBuffer);
			//VIDEOINFOHEADER *videoInfoHeader = (VIDEOINFOHEADER*)g_pmt->pbFormat;
			//WriteBitmap(L"testpic.bmp", &videoInfoHeader->bmiHeader,
			//	g_pmt->cbFormat - SIZE_PREHEADER, pBuffer, BufferLen);
		}
		//notify that a new image has arrived
		pEventSink->Notify(EC_USER, 0, BufferLen);
		return S_OK;
	}
};

SampleGrabberCallback g_GrabberCB;

const wchar_t CLASS_NAME[] = L"Main Window Class";

void analyzeTest() {
	DWORD *pixels = (DWORD*) HeapAlloc(GetProcessHeap(), NULL, 640 * 480 * 4);
	DWORD color = HSVtoRGB((lineColors.hueMin + lineColors.hueMax) / 2, 
		(lineColors.saturationMin + lineColors.saturationMax) / 2,
		(lineColors.valueMin + lineColors.valueMax) / 2);

	drawLine(7, 560, color, (BYTE*)pixels);
	analyzeImage(0, (BYTE*)pixels, 640 * 480 * 4);
	for (int i = 0; i < lines.count; ++i) {
		wprintf(L"line: %d, radius = %d, angle = %.2f, pixelcount = %d\n", i + 1, lines.data[i].radius, 
			lines.data[i].angle, lines.data[i].pixelcount);
	}
	HeapFree(GetProcessHeap(), NULL, pixels);
}

DWORD WINAPI GUICamera(LPVOID lpParameter)
{
	readFromFileColorThresholds();

	//initialize structs and buffers:
	balls.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	balls.size = 128, balls.count = 0;
	goals.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goals.size = 128, goals.count = 0;
	lines.data = (lineInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(lineInfo) * 128);
	lines.size = 128, lines.count = 0;
	ballsShare.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	ballsShare.size = 128, ballsShare.count = 0;
	goalsShare.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goalsShare.size = 128, goalsShare.count = 0;
	linesShare.data = (lineInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(lineInfo) * 128);
	linesShare.size = 128, linesShare.count = 0;
	pBufferCopy = (DWORD*)HeapAlloc(GetProcessHeap(), NULL, 640*480*4);
	houghTransformBuffer = (DWORD*)HeapAlloc(GetProcessHeap(), NULL, 150 * 150 * 4);
	activeColors = &ballColors;

	//analyzeTest();
	//Sleep(-1);

	//make main window
	
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

	hwndMain = CreateWindowEx(0, CLASS_NAME, L"ROBOPROG",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, NULL, NULL, hInstance, NULL);


	ShowWindow(hwndMain, SW_SHOWDEFAULT);

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

	//make video child window for EVR player:

	const wchar_t CLASS_NAME_VIDEO[] = L"Video Window Class";

	WNDCLASSEX wcVideo;
	CopyMemory(&wcVideo, &wc, sizeof(WNDCLASSEX));
	wcVideo.lpfnWndProc = VideoWindowProc;
	wcVideo.lpszClassName = CLASS_NAME_VIDEO;

	RegisterClassEx(&wcVideo);

	hwndVideo = CreateWindowEx(0, CLASS_NAME_VIDEO, L"Video Window",
		WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, hwndMain, NULL, hInstance, NULL);

	ShowWindow(hwndVideo, SW_SHOWDEFAULT);

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

//main window windowprocess
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
	{
		HINSTANCE hInstance = GetModuleHandle(0);
		//make the editbox where information can be written using the prints() function with wprintf format
		hwndEdit = CreateWindowEx(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
			0, 480, 640 + 640, 200, hwnd, (HMENU)ID_EDITCHILD, hInstance, NULL);

		//buttons in the upper right
		HWND button1 = CreateWindowEx(0, L"BUTTON", L"N_IMG",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 0, 65, 20, hwnd, (HMENU)ID_BUTTON1, hInstance, NULL);
		HWND button2 = CreateWindowEx(0, L"BUTTON", L"PROC",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 20, 65, 20, hwnd, (HMENU)ID_BUTTON2, hInstance, NULL);
		HWND button3 = CreateWindowEx(0, L"BUTTON", L"CALIB",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 40, 65, 20, hwnd, (HMENU)ID_BUTTON3, hInstance, NULL);

		//set the proper window position
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
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_BUTTON1:
			prints(L"New Image clicked\n");
			return 0;
		case ID_BUTTON2:
			prints(L"Button 2 clicked\n");
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

	pDisplay->RepaintVideo();

	EndPaint(hwnd, &ps);
}

void setSlidersToValues(colorValues *colors) { //when the object or the slider values change, update the slider regions
	if (colors->hue > colors->hueMax)
		colors->hueMax = colors->hue;
	else if (colors->hue < colors->hueMin)
		colors->hueMin = colors->hue;
	if (colors->saturation > colors->saturationMax)
		colors->saturationMax = colors->saturation;
	else if (colors->saturation < colors->saturationMin)
		colors->saturationMin = colors->saturation;
	if (colors->value > colors->valueMax)
		colors->valueMax = colors->value;
	else if (colors->value < colors->valueMin)
		colors->valueMin = colors->value;

	if (currentCalibratorSetting == minimum) {
		colors->hueMin = colors->hue;
		colors->saturationMin = colors->saturation;
		colors->valueMin = colors->value;
	}
	else {
		colors->hueMax = colors->hue;
		colors->saturationMax = colors->saturation;
		colors->valueMax = colors->value;
	}

	SendMessage(hwndHue, TBM_SETSELSTART, TRUE, (int)(colors->hueMin * 1000 / 6));
	SendMessage(hwndHue, TBM_SETSELEND, TRUE, (int)(colors->hueMax * 1000 / 6));
	SendMessage(hwndSaturation, TBM_SETSELSTART, TRUE, (int)(colors->saturationMin * 1000));
	SendMessage(hwndSaturation, TBM_SETSELEND, TRUE, (int)(colors->saturationMax * 1000));
	SendMessage(hwndValue, TBM_SETSELSTART, TRUE, (int)(colors->valueMin * 1000));
	SendMessage(hwndValue, TBM_SETSELEND, TRUE, (int)(colors->valueMax * 1000));

	SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(colors->hue * 1000 / 6));
	SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(colors->saturation * 1000));
	SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(colors->value * 1000));

	RECT rc{ 120,270,280,350 };
	InvalidateRect(hwndCalibrate, &rc, FALSE); //redraw the rectangle with the current color
	rc = { 0,0,150,180 };
	InvalidateRect(hwndCalibrate, &rc, FALSE); //redraw the text area, where the current values are displayed
}

//the window process for the calibrator window
LRESULT CALLBACK WindowProcCalibrator(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE: //make all the sliders and buttons in the calibrator window
	{
		//first adjust the window position, rc is the size of the client area
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 400, rc.bottom = 400;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 300, 100, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		HINSTANCE hInstance = GetModuleHandle(0);

		//the selections to calibrate colors for the balls, the goals or the lines
		CreateWindowEx(0, L"BUTTON", L"Select object to calibrate:",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
			10, 10, 230, 50, hwnd, (HMENU)ID_RADIOBOXGROUP_OBJECTSELECTOR, hInstance, NULL);
		ballsRadioButton = CreateWindowEx(0, L"BUTTON", L"Balls",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
			10 + 10, 25, 70, 30, hwnd, (HMENU)ID_RADIOBOX_BALLS, hInstance, NULL);
		if(activeColors == &ballColors)
			SendMessage(ballsRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		goalsRadioButton = CreateWindowEx(0, L"BUTTON", L"Goals",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 70, 25, 70, 30, hwnd, (HMENU)ID_RADIOBOX_GOALS, hInstance, NULL);
		if (activeColors == &goalColors)
			SendMessage(goalsRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		linesRadioButton = CreateWindowEx(0, L"BUTTON", L"Lines",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 140, 25, 70, 30, hwnd, (HMENU)ID_RADIOBOX_LINES, hInstance, NULL);
		if (activeColors == &lineColors)
			SendMessage(linesRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		whitenCheckBox = CreateWindowEx(0, L"BUTTON", L"Whiten selection",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10 + 10 + 230, 25, 130, 30, hwnd, (HMENU)ID_CHECKBOX_WHITEN, hInstance, NULL);
		if (whitenThresholdPixels)
			SendMessage(linesRadioButton, BM_SETCHECK, BST_CHECKED, 0);

		//the sliders
		hwndHue = CreateWindowEx(0, TRACKBAR_CLASS, L"Hue", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 70, 250, 20, hwnd, (HMENU)ID_TRACKBAR_HUE, hInstance, 0);
		activeColors->hue = activeColors->hueMin;
		SendMessage(hwndHue, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(activeColors->hueMin * 1000 / 6));
		SendMessage(hwndHue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndHue, TBM_SETSELSTART, TRUE, (int)(activeColors->hueMin * 1000 / 6));
		SendMessage(hwndHue, TBM_SETSELEND, TRUE, (int)(activeColors->hueMax * 1000 / 6));
		hwndSaturation = CreateWindowEx(0, TRACKBAR_CLASS, L"Saturation", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 110, 250, 20, hwnd, (HMENU)ID_TRACKBAR_SATURATION, hInstance, 0);
		activeColors->saturation = activeColors->saturationMin;
		SendMessage(hwndSaturation, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(activeColors->saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndSaturation, TBM_SETSELSTART, TRUE, (int)(activeColors->saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETSELEND, TRUE, (int)(activeColors->saturationMax * 1000));
		hwndValue = CreateWindowEx(0, TRACKBAR_CLASS, L"Value", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 150, 250, 20, hwnd, (HMENU)ID_TRACKBAR_VALUE, hInstance, 0);
		activeColors->value = activeColors->valueMin;
		SendMessage(hwndValue, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(activeColors->valueMin * 1000));
		SendMessage(hwndValue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndValue, TBM_SETSELSTART, TRUE, (int)(activeColors->valueMin * 1000));
		SendMessage(hwndValue, TBM_SETSELEND, TRUE, (int)(activeColors->valueMax * 1000));

		//the selections for minimum or maximum
		CreateWindowEx(0, L"BUTTON", L"Set thresholds for the desired color:",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX | WS_GROUP,
			10, 175, 350, 90, hwnd, (HMENU)ID_RADIOBOXGROUP_MINMAX, hInstance, NULL);
		minRadioButton = CreateWindowEx(0, L"BUTTON", L"Minimum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10, 175 + 20, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MIN, hInstance, NULL);
		if(currentCalibratorSetting == minimum)
			SendMessage(minRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		maxRadioButton = CreateWindowEx(0, L"BUTTON", L"Maximum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10, 175 + 50, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MAX, hInstance, NULL);
		if (currentCalibratorSetting == maximum)
			SendMessage(maxRadioButton, BM_SETCHECK, BST_CHECKED, 0);

		//the buttons
		CreateWindowEx(0, L"BUTTON", L"DONE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			50, 360, 100, 30, hwnd, (HMENU)ID_BUTTON_DONE, hInstance, NULL);
		CreateWindowEx(0, L"BUTTON", L"SAVE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			150, 360, 100, 30, hwnd, (HMENU)ID_BUTTON_SAVE, hInstance, NULL);
		CreateWindowEx(0, L"BUTTON", L"RESET", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			250, 360, 100, 30, hwnd, (HMENU)ID_BUTTON_RESET, hInstance, NULL);
		
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
	case WM_COMMAND: //if a button is pressed
		switch (LOWORD(wParam)) {
		case ID_RADIOBOX_BALLS:
			activeColors = &ballColors;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_GOALS:
			activeColors = &goalColors;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_LINES:
			activeColors = &lineColors;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_CHECKBOX_WHITEN:
			if (SendMessage(whitenCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
				whitenThresholdPixels = true;
			}
			else {
				whitenThresholdPixels = false;
			}
			return 0;
		case ID_BUTTON_DONE:
			ShowWindow(hwnd, SW_HIDE);
			calibrating = FALSE;
			return 0;
		case ID_BUTTON_SAVE:
			saveToFileColorThresholds();
			return 0;
		case ID_BUTTON_RESET:
			activeColors->hueMin = 0, activeColors->hueMax = 6;
			
			activeColors->saturationMin = 0, activeColors->saturationMax = 1;
			activeColors->valueMin = 0, activeColors->valueMax = 1;
			if (currentCalibratorSetting == minimum) {
				activeColors->hue = 0;
				activeColors->saturation = 0;
				activeColors->value = 0;
			}
			else {
				activeColors->hue = 6;
				activeColors->saturation = 1;
				activeColors->value = 1;
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_MIN:
			currentCalibratorSetting = minimum;
			activeColors->hue = activeColors->hueMin;
			activeColors->saturation = activeColors->saturationMin;
			activeColors->value = activeColors->valueMin;
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_MAX:
			currentCalibratorSetting = maximum;
			activeColors->hue = activeColors->hueMax;
			activeColors->saturation = activeColors->saturationMax;
			activeColors->value = activeColors->valueMax;
			setSlidersToValues(activeColors);
			return 0;
		}

		break;
	case WM_HSCROLL: //if a slider is moved
		if (LOWORD(wParam) == TB_THUMBTRACK || LOWORD(wParam) == TB_ENDTRACK 
				|| LOWORD(wParam) == TB_PAGEDOWN || LOWORD(wParam) == TB_PAGEUP) {
			if ((HWND)lParam == hwndHue) {
				int currentPosition = SendMessage(hwndHue, TBM_GETPOS, 0, 0);
				activeColors->hue = (float)currentPosition * 6 / 1000;
			}
			else if ((HWND)lParam == hwndSaturation) {
				int currentPosition = SendMessage(hwndSaturation, TBM_GETPOS, 0, 0);
				activeColors->saturation = (float)currentPosition / 1000;
			}
			else if ((HWND)lParam == hwndValue) {
				int currentPosition = SendMessage(hwndValue, TBM_GETPOS, 0, 0);
				activeColors->value = (float)currentPosition / 1000;
			}
			setSlidersToValues(activeColors);
			RECT rc{ 120,270,280,350 };
			InvalidateRect(hwnd, &rc, FALSE); //redraw the rectangle with the current color
			rc = { 0,0,150,180 };
			InvalidateRect(hwnd, &rc, FALSE); //redraw the text area, where the current values are displayed
			//prints(L"H %.2f %.2f S %.2f %.2f V %.2f %.2f\n", 
			//	activeColors->hueMin, activeColors->hueMax, activeColors->saturationMin, 
			//	activeColors->saturationMax, activeColors->valueMin, activeColors->valueMax);
		}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OnPaintCalibrator(HWND hwnd) {
	wchar_t buf[24];
	int len = 0;
	RECT rc;
	GetClientRect(hwnd, &rc);
	RECT rc2 = { 120,270,280,350 };		//rectangle where the current color is drawn
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);

	//ExcludeClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);
	FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW));
	//IntersectClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);

	SetBkMode(hdc, TRANSPARENT); //for text printing, doesn't draw a box around the text

	TextOutW(hdc, 10, 70, buf, swprintf(buf, 20, L"Hue:        %.4f", activeColors->hue));
	TextOutW(hdc, 10, 110, buf, swprintf(buf, 20, L"Saturation: %.4f", activeColors->saturation));
	TextOutW(hdc, 10, 150, buf, swprintf(buf, 20, L"Value:     %.4f", activeColors->value));

	//HRGN hrgn = CreateRectRgn(120, 250, 280, 330);
	//SelectClipRgn(hdc, hrgn);
	HBRUSH rectBrush = CreateSolidBrush(HSVtoRGB(activeColors->hue, activeColors->saturation, activeColors->value));
	FillRect( hdc, &rc2, rectBrush );
	DeleteObject(rectBrush);

	EndPaint(hwnd, &ps);
}


LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CREATE:
		SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOZORDER);
		prints(L"Initializing camera\n");
		InitVideo(hwnd);
		prints(L"Camera initialized\n");
		return 0;
	case WM_PAINT:
		pDisplay->RepaintVideo();
		return 0;
	case WM_GRAPH_EVENT:
		OnGraphEvent(hwnd);
		return 0;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
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

	prints(L"Camera list:\n");
	BSTR chosen = SysAllocString(L""); //string of the chosen camera
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
				prints(L"%s\n",varName.bstrVal);
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
							chosen = varName.bstrVal;
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
	prints(L"Chose %s\n", chosen);

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

void Release() {
	pControl->Stop();
	pGraph->Release();
	pEventSink->Release();
	pEvent->Release();
	DeleteObject(hBitmap);
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
		case EC_USER: //invalidate the rectangle where we draw the bitmap, so that it gets redrawn
			const RECT rc{ 640,0,640 + 640,480 };
			InvalidateRect(hwndMain, &rc, FALSE);
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
	WriteFile(dataFile, &ballColors, sizeof(ballColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &goalColors, sizeof(goalColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &lineColors, sizeof(lineColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &currentCalibratorSetting, sizeof(currentCalibratorSetting), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &activeColors, sizeof(activeColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &whitenThresholdPixels, sizeof(whitenThresholdPixels), &numberOfBytesRead, NULL);
	
	CloseHandle(dataFile);
}

void readFromFileColorThresholds() {
	HANDLE dataFile = CreateFile(L"data.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD numberOfBytesRead;
	ReadFile(dataFile, &ballColors, sizeof(ballColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &goalColors, sizeof(goalColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &lineColors, sizeof(lineColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &currentCalibratorSetting, sizeof(currentCalibratorSetting), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &activeColors, sizeof(activeColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &whitenThresholdPixels, sizeof(whitenThresholdPixels), &numberOfBytesRead, NULL);
	CloseHandle(dataFile);
}

void doubleObjectBufferSize(objectCollection* objects) {
	objectInfo* buffer = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 2 * objects->size);
	CopyMemory(buffer, objects->data, (objects->size)*sizeof(objectInfo));
	objects->size *= 2;
	HeapFree(GetProcessHeap(), NULL, (objects->data));
	objects->data = buffer;
}

//void doubleLineBufferSize(lineCollection* objects) {
//	lineInfo* buffer = (lineInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(lineInfo) * 2 * objects->size);
//	CopyMemory(buffer, objects->data, (objects->size)*sizeof(lineInfo));
//	objects->size *= 2;
//	HeapFree(GetProcessHeap(), NULL, (objects->data));
//	objects->data = buffer;
//}