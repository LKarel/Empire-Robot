#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define PI (3.1415927)

HANDLE readySignal = CreateEvent(NULL, FALSE, FALSE, NULL);
HANDLE newImageAnalyzed = CreateEvent(NULL, FALSE,FALSE,NULL);
HANDLE GUIThread;
HANDLE hCOM1;
HANDLE hCOM2;
HANDLE hCOM3;
HANDLE hCOM4;
extern objectCollection ballsShare, goalsShare;
extern HANDLE writeMutex;
void sendString(HANDLE hComm, char* outputBuffer);
void receiveString(HANDLE hComm, char* outputBuffer);
void initCOMPorts();
void setSpeed(float speed, float angle, float angularVelocity);
void testSingleBallTrack();

struct vector {
	float e1;
	float e2;
	float e3;
};

int main() {
	//Create the GUI in a separate thread
	GUIThread = CreateThread(NULL, 0, GUICamera, 0, 0, NULL);
	//Wait for the GUI to initialize
	WaitForSingleObject(readySignal, INFINITE);

	//initialize the COM ports of the connected engine plates
	initCOMPorts();

	//testSingleBallTrack();

	//TODO control the robot...
	prints(L"Testing printing\n");

	//Don't exit this thread before the GUI
	WaitForSingleObject(GUIThread, INFINITE);
}

//calculates the direction of the robot modulo 90 degrees. For this it determines the orientation with respect to the field lines.
float calculateDirection(lineInfo line) {
	int r = line.radius;
	float angle = line.angle;
	
	float height = 25 / 100; //height in meters
	float cameraAngle = 10 * PI / 180; //the angle that the camera is down with respect to the horizontal
	float angleOfView = 30 * PI / 180; //the horizontal angle of view
	
	//x, y, z are the coordinates on the floor, x axis straight ahead, y axis to the left
	vector eCameraNormal = { cosf(cameraAngle), 0, -sinf(cameraAngle) };	//basis vector in the direction the camera is pointing
	vector ex = { 0, -1, 0 };		//basis vector in the x direction of the camera image where the pixel x coordinate increases, so called image basis
	vector ey = { sinf(cameraAngle), 0, cosf(cameraAngle) };	//basis vector in the y direction of the camera image where the pixel y coordinate increases

	//a vector to a point on the line and a vector along the line in the basis ex, ey, eCameraNormal
	vector vLinePoint = { r*cosf(angle) / 320 * tanf(angleOfView / 2), r*sinf(angle) / 320 * tanf(angleOfView / 2), 1 };
	vector alongTheLine = { sinf(angle), -cosf(angle), 0 };
	//normal vector to the plane that contains the viewer and the line in that same basis, calculated using vector product
	vector planeNormal = { vLinePoint.e2 * alongTheLine.e3 - vLinePoint.e3 * alongTheLine.e2, 
		vLinePoint.e3 * alongTheLine.e1 - vLinePoint.e1 * alongTheLine.e3,
		vLinePoint.e1 * alongTheLine.e2 - vLinePoint.e2 * alongTheLine.e1 };
	//the same normal vector in the floor xyz coordinates:
	vector planeNormalFloor = { planeNormal.e1 * ex.e1 + planeNormal.e2 * ey.e1 + planeNormal.e3 * eCameraNormal.e1,
								planeNormal.e1 * ex.e2 + planeNormal.e2 * ey.e2 + planeNormal.e3 * eCameraNormal.e2, 
								planeNormal.e1 * ex.e3 + planeNormal.e2 * ey.e3 + planeNormal.e3 * eCameraNormal.e3 };
	vector lineTangentLeftToRight = {};
	float x, y;
	if (planeNormalFloor.e2 >= 0) {
		x = planeNormalFloor.e2, y = -planeNormalFloor.e1;
	}
	else {
		x = -planeNormalFloor.e2, y = planeNormalFloor.e1;
	}
	if(x * 10000 < (y < 0 ? -y : y)){
		angle = PI / 2;
	}
	else {
		angle = atanf(y / x);
	}
	return angle;
}

void testSingleBallTrack() {
	int currentx, currenty;
	while (WaitForSingleObject(newImageAnalyzed, 0) == WAIT_OBJECT_0); //wait for the first image to come
	currentx = ballsShare.data[0].x, currenty = ballsShare.data[0].y;
	while (true) {
		if (ballsShare.count == 0) {
			setSpeed(0, 0, -3);
			Sleep(50);
		}
		else if(currentx-320 > 60){
			setSpeed(0, 0, -3);
			Sleep(50);
		}
		else if (currentx - 320 < 60) {
			setSpeed(0, 0, 3);
			Sleep(50);
		}
		else {
			setSpeed(3, 0, 0);
			Sleep(50);
		}
		if (WaitForSingleObject(newImageAnalyzed, 0) == WAIT_OBJECT_0) {
			WaitForSingleObject(writeMutex, INFINITE);
			currentx = ballsShare.data[0].x, currenty = ballsShare.data[0].y;
			ReleaseMutex(writeMutex);
		}
	}
}

//initializes the COM ports to the right handles
void initCOMPorts() {
	//read all the COM ports from the registry
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", NULL, KEY_READ, &hKey) != ERROR_SUCCESS) {
		prints(L"REGISTRY COM PORT LIST READING ERROR\n");
		return;
	}
	wchar_t valueName[64] = {};
	wchar_t dataBuffer[256] = {};
	DWORD valueNameLen;
	DWORD dataLen;
	HANDLE COMPorts[4] = { hCOM1,hCOM2,hCOM3,hCOM4 }; //used to assign the com ports to the correct engines
	for (int i = 0;RegEnumValueW(hKey, i, (LPWSTR)&valueName, &valueNameLen, NULL,
		NULL, (LPBYTE)&dataBuffer, &dataLen) != ERROR_NO_MORE_ITEMS;++i) {
		wprintf(L"%d %d %s\n", i, dataLen, dataBuffer);
		//open the COM port and ask its ID, so that we know which wheel it is
		HANDLE hComm = CreateFile(dataBuffer, GENERIC_READ | GENERIC_WRITE, 0, 0,
			OPEN_EXISTING, 0, 0);
		if (hComm == INVALID_HANDLE_VALUE)
			prints(L"INVALID COM HANDLE %s\n", dataBuffer);
		else {
			sendString(hComm, "?\n");
			receiveString(hComm, (char*)dataBuffer);
			char id = *((char*)dataBuffer);
			COMPorts[id - 1] = hComm; //assigns the port to the correct handle
		}

	}
}

void sendString(HANDLE hComm, char* outputBuffer) {
	DWORD bytesWritten;
	int len;
	for (len = 0; outputBuffer[len] != '\0'; ++len);
	WriteFile(hComm, outputBuffer, len, &bytesWritten, NULL);
}

//recieves the string upto the new line character and adds a zero character
void receiveString(HANDLE hComm, char* outputBuffer) {
	DWORD bytesRead;
	int i = 0;
	while (outputBuffer[i] != '\n') {
		ReadFile(hComm, outputBuffer, 1, &bytesRead, NULL);
		++i;
	}
	outputBuffer[i + 1] = '\0';
}

//sets the speed of a specific wheel to a value in the plate speed units
void setEngineRotation(HANDLE hComm, int speed) {
	char writeBuffer[32] = {};
	DWORD bytesWritten;
	int len = sprintf_s(writeBuffer, "sd%d\n", speed);

	WriteFile(hComm, writeBuffer, len, &bytesWritten, NULL);
}

//sets the robot speed to specific values, wheels go 1-2-3-4 from front left to back right
//x axis is straight ahead, y axis is to the left
//in units m, s, degrees, positive angle turns left
void setSpeed(float speed, float angle, float angularVelocity) {
	float wheelRadius = 3 / 100; //in meters
	float baseRadius = 15 / 100; //base radius in meters from the center to the wheel

	const float realToPlateUnits = 62.5 / (18.75 * 64);
	float vx = speed*cosf(angle * PI / 180);
	float vy = speed*sinf(angle * PI / 180);
	int wheel1Speed = (int)(((vx - vy) / wheelRadius - angularVelocity*baseRadius/wheelRadius)*realToPlateUnits);
	int wheel2Speed = (int)(((vx + vy) / wheelRadius + angularVelocity*baseRadius / wheelRadius)*realToPlateUnits);
	int wheel3Speed = (int)(((vx + vy) / wheelRadius - angularVelocity*baseRadius / wheelRadius)*realToPlateUnits);
	int wheel4Speed = (int)(((vx - vy) / wheelRadius + angularVelocity*baseRadius / wheelRadius)*realToPlateUnits);
	setEngineRotation(hCOM1, wheel1Speed);
	setEngineRotation(hCOM2, wheel2Speed);
	setEngineRotation(hCOM3, wheel3Speed);
	setEngineRotation(hCOM4, wheel4Speed);
}

//no longer needed, the image processing is in the GUICamera file, done as soon as the frame arrives in BufferCB
//void imageProcessingTest() {
//	while (true) {
//		//get the image after button 2 was pressed
//		//WaitForSingleObject(button2Signal, INFINITE);
//		//CopyMemory(doubleBuffer, editBuffer, 640 * 480 * 4);
//		//prints(L"here\n");
//		//Filters:
//		//threshold(doubleBuffer);
//		//calculateBrightness(editBuffer);
//		//calculateHue(doubleBuffer);
//		//calculateChroma(doubleBuffer);
//		//calculateSaturation(editBuffer);
//		//kMeans(2,5, doubleBuffer);
//
//		//display the image, copy editBuffer to the screen buffer g_pBuffer
//		//SetEvent(setImageSignal);
//	}
//}
//
////Y' from the YUV HDTV BT.709 standard
//void calculateBrightness(BYTE* buffer) {
//	for (DWORD *pixBuffer = (DWORD *)buffer; pixBuffer < 640 * 480 + (DWORD *)buffer;++pixBuffer) {
//		DWORD pixel = *pixBuffer;
//		float brightness = 0.2126f*(pixel & 0xFF) + 0.7152f*((pixel >> 8) & 0xFF) + 0.0722f*((pixel >> 16) & 0xFF);
//		*pixBuffer = (int)brightness*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
//	}
//}
//
////calculates the Y' values modulo some number
//void smoother(BYTE* buffer) {
//	for (DWORD *pixBuffer = (DWORD *)buffer; pixBuffer < 640 * 480 + (DWORD *)buffer;++pixBuffer) {
//		DWORD pixel = *pixBuffer;
//		DWORD brightness = (int)(0.2126f*(pixel & 0xFF) + 0.7152f*((pixel >> 8) & 0xFF) + 0.0722f*((pixel >> 16) & 0xFF));
//		brightness = brightness - brightness % 32;
//		*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
//	}
//}
//
//void calculateChroma(BYTE* buffer) {
//	for (DWORD *pixBuffer = (DWORD *)buffer; pixBuffer < 640 * 480 + (DWORD *)buffer;++pixBuffer) {
//		DWORD pixel = *pixBuffer;
//		DWORD chroma = max3((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF))-
//				min3((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF));
//
//		//chroma = chroma - chroma % 20;
//		*pixBuffer = chroma*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
//
//		//*pixBuffer = (chroma > 10) ? *pixBuffer : 0;
//	}
//}
//
//void calculateSaturation(BYTE* buffer) {
//	for (DWORD *pixBuffer = (DWORD *)buffer; pixBuffer < 640 * 480 + (DWORD *)buffer;++pixBuffer) {
//		DWORD pixel = *pixBuffer;
//		float saturation = (float)(max3((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF)) - //chroma/value
//			min3((pixel & 0xFF), ((pixel >> 8) & 0xFF), ((pixel >> 16) & 0xFF))) /
//			(0.2126f*(pixel & 0xFF) + 0.7152f*((pixel >> 8) & 0xFF) + 0.0722f*((pixel >> 16) & 0xFF));
//
//		//chroma = chroma - chroma % 20;
//		*pixBuffer = (int)(saturation*255)*(1 + (1 << 8) + (1 << 16)); //grayscale image of pixel values
//
//		//*pixBuffer = (saturation > 0.5) ? *pixBuffer : 0;
//	}
//}
//
//void calculateHue(BYTE* buffer) {
//	for (DWORD *pixBuffer = (DWORD *)buffer; pixBuffer < 640 * 480 + (DWORD *)buffer;++pixBuffer) {
//		DWORD pixel = *pixBuffer;
//		BYTE red = pixel & 0xFF, green = (pixel >> 8) & 0xFF, blue = (pixel >> 16) & 0xFF;
//		float hue;
//
//		//calculates hue in the range 0 to 6
//		if(red >= green && green >= blue && red > blue)	hue =	((float)(green - blue)  /	(red - blue));
//		else if (green > red && red >= blue)	hue =  (2 - (float)(red -	blue) /	(green - blue));
//		else if (green >= blue && blue > red)	hue =  (2 + (float)(blue -	red)  /	(green - red));
//		else if (blue > green && green > red)	hue =  (4 - (float)(green - red)   /	(blue - red));
//		else if (blue > red && red >= green)	hue =  (4 + (float)(red - green)   /	(blue - green));
//		else if (red >= blue && blue > green)	hue =  (6 - (float)(blue - green)  /	(red - green));
//		else hue = 0; //Hue when the image is gray red=green=blue
//
//		//pixel = 256 * hue / 6;
//		//prints(L"%X %.2f \n", pixel, hue);
//		//*pixBuffer = pixel*(1 + (1 << 8) + (1 << 16));
//		*pixBuffer = (hue > 4 || hue < 2) ? *pixBuffer : 0;
//	}
//}
//
////k-Means algorithm, read on Wikipedia
//void kMeans(int k, int iterations, BYTE* buffer) { //k centers, done for iterations iterations
//	int *xCenter = new int[k], *yCenter = new int[k]; //centers of the k means
//	DWORD *pixBuffer = (DWORD *)buffer;
//
//	int *xNewCenter = new int[k], *yNewCenter = new int[k], *CenterCount = new int[k]; //new centers and count for averaging later
//	
//	//for (int i = 0; i < k; ++i) {	//distribute centers around the corners
//	//	if ((i + 3) % 4 < 2)
//	//		xCenter[i] = 640;
//	//	else
//	//		xCenter[i] = 0;
//	//	if (i % 4 < 2)
//	//		yCenter[i] = 0;
//	//	else
//	//		yCenter[i] = 480;
//	//}
//
//	for (int iterationCount = 0; iterationCount < iterations; ++iterationCount) {
//		ZeroMemory(xNewCenter, sizeof(int)*k);
//		ZeroMemory(yNewCenter, sizeof(int)*k);
//		ZeroMemory(CenterCount, sizeof(int)*k);
//		for (int currentY = 0; currentY < 480; ++currentY) {
//			for (int currentX = 0; currentX < 640; ++currentX) {
//				int pixel = 0xFF & pixBuffer[currentX + 640*currentY];
//				int minDistanceSquared = (currentX-xCenter[0])*(currentX - xCenter[0])+ 
//										 (currentY - yCenter[0])*(currentY - yCenter[0]);
//				int minN = 0;
//				for (int currentN = 1; currentN < k; ++currentN) {
//					int DistanceSquared = (currentX - xCenter[currentN])*(currentX - xCenter[currentN]) +
//										(currentY - yCenter[currentN])*(currentY - yCenter[currentN]);
//					if (DistanceSquared < minDistanceSquared) {
//						minN = currentN;
//						minDistanceSquared = DistanceSquared;
//					}
//				}
//				CenterCount[minN] += pixel;
//				xNewCenter[minN] += pixel * currentX;
//				yNewCenter[minN] += pixel * currentY;
//			}
//		}
//
//		//all pixels looped, calculate new centers:
//		for (int currentN = 0; currentN < k; ++currentN) {
//			xCenter[currentN] = (int)((float)xNewCenter[currentN] / CenterCount[currentN]);
//			yCenter[currentN] = (int)((float)yNewCenter[currentN] / CenterCount[currentN]);
//		}
//	}
//
//	////draw the crosses
//	//for (int currentN = 0; currentN < k; ++currentN) {
//	//	drawCross(xCenter[currentN], yCenter[currentN], 0x00FFFFFF, editBuffer);
//	//}
//	delete[] xCenter, yCenter, xNewCenter, yNewCenter, CenterCount;
//}
//
////void threshold(BYTE* buffer) {
////	for (DWORD *pixBuffer = (DWORD*)buffer; pixBuffer < (DWORD*)buffer + 640*480; ++pixBuffer) {
////		DWORD pixel = *pixBuffer;
////		BYTE red = pixel & 0xFF, green = (pixel >> 8) & 0xFF, blue = (pixel >> 16) & 0xFF;
////
////		float hue;
////		//calculates hue in the range 0 to 6
////		if (red >= green && green >= blue && red > blue)	hue = ((float)(green - blue) / (red - blue));
////		else if (green > red && red >= blue)	hue = (2 - (float)(red - blue) / (green - blue));
////		else if (green >= blue && blue > red)	hue = (2 + (float)(blue - red) / (green - red));
////		else if (blue > green && green > red)	hue = (4 - (float)(green - red) / (blue - red));
////		else if (blue > red && red >= green)	hue = (4 + (float)(red - green) / (blue - green));
////		else if (red >= blue && blue > green)	hue = (6 - (float)(blue - green) / (red - green));
////		else hue = 0; //Hue when the image is gray red=green=blue
////
////		float saturation = (float)(max3(red, green, blue) - min3(red, green, blue))
////			/ max3(red, green, blue);
////
////		float value = (float)max3(red, green, blue) / 255;
////
////		if (hue < hueMin || hue > hueMax || saturation < saturationMin || saturation > saturationMax ||
////			value < valueMin || value > valueMax)
////			*pixBuffer = 0;
////	}
////}
//
//void COMTesting() {
//
//
//	HANDLE hComm = CreateFile(L"\\\\.\\COM12", GENERIC_READ | GENERIC_WRITE, 0, 0,
//		OPEN_EXISTING, 0, 0);
//	if (hComm == INVALID_HANDLE_VALUE)
//		prints(L"INVALID HANDLE\n");
//
//	char *writeBuffer = "s\n";
//	DWORD bytesWritten;
//	WriteFile(hComm, writeBuffer, 4, &bytesWritten, NULL);
//
//	char *readBuffer[128]{};
//	DWORD bytesRead;
//	for (int i = 0;i < 10;++i) {
//		ReadFile(hComm, readBuffer, 1, &bytesRead, NULL);
//		prints(L"%s\n", readBuffer);
//	}
//}
//
