#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define numa prints( #ZeroMemory(&a,10) )

HANDLE readySignal = CreateEvent(NULL, FALSE, FALSE, NULL);
HANDLE getImageSignal = CreateEvent(NULL, FALSE,FALSE,NULL);
HANDLE setImageSignal = CreateEvent(NULL, FALSE, FALSE, NULL);
HANDLE button2Signal = CreateEvent(NULL, FALSE, FALSE, NULL);
HANDLE GUIThread;
extern BYTE *g_pBuffer;
DWORD *g_pBufferDW = (DWORD*)(&g_pBuffer);
BYTE *editBuffer;

void imageProcessingTest();
void calculateBrightness();
void smoother();
void calculateChroma();
void calculateHue();
void drawCross(int x, int y, int color);
void kMeans(int k, int iterations);

int main() {
	editBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 640 * 480 * 4);
	//Create the GUI in a separate thread
	GUIThread = CreateThread(NULL, 0, GUICamera, 0, 0, NULL);
	//Wait for the GUI to initialize
	WaitForSingleObject(readySignal, INFINITE);

	//TODO control the robot...
	prints(L"Testing printing\n"); //this function can print using the wprintf syntax
	float x = 0;
	prints(L"%d", (int)(1.0 / x));
	imageProcessingTest();

	//Don't exit this thread before the GUI
	WaitForSingleObject(GUIThread, INFINITE);
}

void imageProcessingTest() {
	while (true) {
		//get the image after button 2 was pressed
		WaitForSingleObject(button2Signal, INFINITE);
		SetEvent(getImageSignal);
		WaitForSingleObject(readySignal, INFINITE); //image has been copied to editBuffer

		//Filters:
		calculateBrightness();
		calculateHue();
		//calculateChroma();
		//kMeans(1,5);

		//display the image, copy editBuffer to the screen buffer g_pBuffer
		SetEvent(setImageSignal);
	}
}

//Y' from the YUV HDTV BT.709 standard
void calculateBrightness() {
	for (DWORD *pixBuffer = (DWORD *)editBuffer; pixBuffer < 640 * 480 + (DWORD *)editBuffer;++pixBuffer) {
		DWORD pixel = *pixBuffer;
		pixel = 0.2126*(pixel & 0xFF) + 0.7152*((pixel >> 8) & 0xFF) + 0.0722*((pixel >> 16) & 0xFF);
		*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
	}
}

//calculates the Y' values module some number
void smoother() {
	for (DWORD *pixBuffer = (DWORD *)editBuffer; pixBuffer < 640 * 480 + (DWORD *)editBuffer;++pixBuffer) {
		DWORD pixel = *pixBuffer;
		pixel = 0.2126*(pixel & 0xFF) + 0.7152*((pixel >> 8) & 0xFF) + 0.0722*((pixel >> 16) & 0xFF);
		pixel = pixel - pixel % 32;
		*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
	}
}

void calculateChroma() {
	for (DWORD *pixBuffer = (DWORD *)editBuffer; pixBuffer < 640 * 480 + (DWORD *)editBuffer;++pixBuffer) {
		DWORD pixel = *pixBuffer;
		pixel = max((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF))-
				min((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF));
		pixel = pixel - pixel % 20;
		*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
	}
}

void calculateHue() {
	for (DWORD *pixBuffer = (DWORD *)editBuffer; pixBuffer < 640 * 480 + (DWORD *)editBuffer;++pixBuffer) {
		DWORD pixel = *pixBuffer;
		BYTE red = pixel & 0xFF, green = (pixel >> 8) & 0xFF, blue = (pixel >> 16) & 0xFF;

		//calculates hue in the range 0 to 255
		if(red >= green && green >= blue && red > blue)	pixel =	255/6*((float)(green - blue)  /	(red - blue));
		else if (green > red && red >= blue)	pixel = 255 / 6 * (2 - (float)(red -	blue) /	(green - blue));
		else if (green >= blue && blue > red)	pixel = 255 / 6 * (2 + (float)(blue -	red)  /	(green - red));
		else if (blue > green && green > red)	pixel = 255 / 6 * (4 - (float)(green - red)   /	(blue - red));
		else if (blue > red && red >= green)	pixel = 255 / 6 * (4 + (float)(red - green)   /	(blue - green));
		else if (red >= blue && blue > green)	pixel = 255 / 6 * (6 - (float)(blue - green)  /	(red - green));
		else pixel = 0; //Hue when the image is gray red=green=blue

		pixel = pixel - pixel % 32;
		*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
	}
}

//k-Means algorithm, read on Wikipedia
void kMeans(int k, int iterations) { //k centers, done for iterations iterations
	int *xCenter = new int[k], *yCenter = new int[k]; //centers of the k means
	DWORD *pixBuffer = (DWORD *)editBuffer;

	int *xNewCenter = new int[k], *yNewCenter = new int[k], *CenterCount = new int[k]; //new centers and count for averaging later
	
	for (int iterationCount = 0; iterationCount < iterations; ++iterationCount) {
		ZeroMemory(xNewCenter, sizeof(int)*k);
		ZeroMemory(yNewCenter, sizeof(int)*k);
		ZeroMemory(CenterCount, sizeof(int)*k);
		for (int currentY = 0; currentY < 480; ++currentY) {
			for (int currentX = 0; currentX < 640; ++currentX) {
				int pixel = 0xFF & pixBuffer[currentX + 640*currentY];
				int minDistanceSquared = (currentX-xCenter[0])*(currentX - xCenter[0])+ 
										 (currentY - yCenter[0])*(currentY - yCenter[0]);
				int minN = 0;
				for (int currentN = 1; currentN < k; ++currentN) {
					int DistanceSquared = (currentX - xCenter[currentN])*(currentX - xCenter[currentN]) +
										(currentY - yCenter[currentN])*(currentY - yCenter[currentN]);
					if (DistanceSquared < minDistanceSquared) {
						minN = currentN;
						minDistanceSquared = DistanceSquared;
					}
				}
				CenterCount[minN] += pixel;
				xNewCenter[minN] += pixel * currentX;
				yNewCenter[minN] += pixel * currentY;
			}
		}

		//all pixels looped, calculate new centers:
		for (int currentN = 0; currentN < k; ++currentN) {
			xCenter[currentN] = (float)xNewCenter[currentN] / CenterCount[currentN];
			yCenter[currentN] = (float)yNewCenter[currentN] / CenterCount[currentN];
		}
	}

	//draw crosses
	for (int currentN = 0; currentN < k; ++currentN) {
		drawCross(xCenter[currentN], yCenter[currentN], 0x00FFFFFF);
	}
	delete[] xCenter, yCenter, xNewCenter, yNewCenter, CenterCount;
}

//draws a cross of the color #rrggbbaa to coordinates x, y starting from the bottom left corner (Windows bitmap uses that)
void drawCross(int x, int y, int color) {
	DWORD *pixBuffer = (DWORD *)editBuffer;
	for (int j = -1;j <= 1;++j)
		for (int i = -10;i <= 10;++i)
			if(x+i<640 && y+j<480)
				pixBuffer[x + i + 640 * (y+j)] = color;
	for (int j = -1;j <= 1;++j)
		for (int i = -10;i <= 10;++i)
			if (x + j<640 && y + i<480)
				pixBuffer[x+j + 640 * (y+i)] = color;
}

void COMTesting() {
	HANDLE hComm = CreateFile(L"\\\\.\\COM12", GENERIC_READ | GENERIC_WRITE, 0, 0,
		OPEN_EXISTING, 0, 0);
	if (hComm == INVALID_HANDLE_VALUE)
		prints(L"INVALID HANDLE\n");

	char *writeBuffer = "s\n";
	DWORD bytesWritten;
	WriteFile(hComm, writeBuffer, 4, &bytesWritten, NULL);

	char *readBuffer[128]{};
	DWORD bytesRead;
	for (int i = 0;i < 10;++i) {
		ReadFile(hComm, readBuffer, 1, &bytesRead, NULL);
		prints(L"%s\n", readBuffer);
	}
}