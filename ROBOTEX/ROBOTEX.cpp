#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define PI (3.1415927)
#define SQRT2 (1.4142135)

HANDLE readySignal = CreateEvent(NULL, FALSE, FALSE, NULL);
HANDLE newImageAnalyzed = CreateEvent(NULL, FALSE,FALSE,NULL);
HANDLE startSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
HANDLE stopSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
HANDLE calibratingSignal = CreateEventW(NULL, TRUE, FALSE, NULL);
HANDLE GUIThread;
HANDLE hCOMDongle;
HANDLE hCOMRadio;
extern objectCollection ballsShare, goalsShare;
extern HANDLE writeMutex;
extern BOOLEAN calibrating;
void sendString(HANDLE hComm, char* outputBuffer);
int receiveString(HANDLE hComm, char* outputBuffer);
void initCOMPort();
void initUSBRadio();
void setSpeed(float speed, float angle, float angularVelocity);
void testSingleBallTrack();
void test();

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
	prints(L"Testing printing\r\n\r\n");
	
	//initialize the radio
	//initUSBRadio();

	//initialize the COM port of the dongle
	initCOMPort();

	//testSingleBallTrack();

	test();

	//TODO control the robot...

	//Don't exit this thread before the GUI
	WaitForSingleObject(GUIThread, INFINITE);
}

void test() {
	while (1) {
		WaitForSingleObject(startSignal, INFINITE);
		prints(L"Started\n");
		testSingleBallTrack();
	}
}

void testSingleBallTrack() {
	int currentx, currenty, ballCount;
	while (WaitForSingleObject(newImageAnalyzed, 0) == WAIT_OBJECT_0); //wait for the first image to come

	WaitForSingleObject(writeMutex, INFINITE);
	currentx = ballsShare.data[0].x, currenty = ballsShare.data[0].y, ballCount = ballsShare.count;
	ReleaseMutex(writeMutex);

	while (true) {
		if (WaitForSingleObject(stopSignal, 50) == WAIT_OBJECT_0) {
			setSpeed(0, 0, 0);
			prints(L"Stopped\n\n");
			return;
		}
		if (ballCount == 0) {
			setSpeed(0, 0, -60);
		}
		else if(currentx-320 > 100){
			setSpeed(0, 0, -60);
		}
		else if (currentx - 320 < -100) {
			setSpeed(0, 0, 60);
		}
		else {
			setSpeed(0.3, 0, 0);
		}
		if (WaitForSingleObject(newImageAnalyzed, 0) == WAIT_OBJECT_0) {
			WaitForSingleObject(writeMutex, INFINITE);
			currentx = ballsShare.data[0].x, currenty = ballsShare.data[0].y, ballCount = ballsShare.count;
			ReleaseMutex(writeMutex);
		}
	}
}



//initializes the COM ports to the right handles; not needed, we have a dongle
void initCOMPort() {
	DCB dcb = {};

	//hCOMDongle = CreateFile(L"\\\\.\\COM3", GENERIC_READ | GENERIC_WRITE, 0, NULL,
	//	OPEN_EXISTING, 0, NULL);
	hCOMDongle = CreateFile(L"COM3", GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, 0, NULL);
	if (hCOMDongle == INVALID_HANDLE_VALUE) {
		prints(L"ERROR OPENING DONGLE COM PORT\n");
	}
	else {
		prints(L"USB dongle COM port 3 opened\n");
	}

	ZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	if (!GetCommState(hCOMRadio, &dcb))
	{
		prints(L"GetCommState failed with error %X.\n", GetLastError());
	}
	prints(L"Baudrate %d\n", dcb.BaudRate);

	dcb.BaudRate = 19200;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	if (!SetCommState(hCOMRadio, &dcb))
	{
		prints(L"SetCommState failed with error %X.\n", GetLastError());
	}
	else //turn off the failsafe of the engine controllers
	{
		sendString(hCOMDongle, "1:fs0\n");
		sendString(hCOMDongle, "2:fs0\n");
		sendString(hCOMDongle, "3:fs0\n");
		sendString(hCOMDongle, "4:fs0\n");
	}
	prints(L"COM init end\n\n");

	//this is not needed anymore, we have a dongle
	////read all the COM ports from the registry
	//HKEY hKey;
	//if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", NULL, KEY_READ, &hKey) != ERROR_SUCCESS) {
	//	prints(L"REGISTRY COM PORT LIST READING ERROR\n");
	//	return;
	//}
	//wchar_t valueName[64] = {};
	//wchar_t dataBuffer[256] = {};
	//DWORD valueNameLen;
	//DWORD dataLen;
	//HANDLE COMPorts[4] = { hCOM1,hCOM2,hCOM3,hCOM4 }; //used to assign the com ports to the correct engines
	//for (int i = 0;RegEnumValueW(hKey, i, (LPWSTR)&valueName, &valueNameLen, NULL,
	//	NULL, (LPBYTE)&dataBuffer, &dataLen) != ERROR_NO_MORE_ITEMS;++i) {
	//	wprintf(L"%d %d %s\n", i, dataLen, dataBuffer);
	//	//open the COM port and ask its ID, so that we know which wheel it is
	//	HANDLE hComm = CreateFile(dataBuffer, GENERIC_READ | GENERIC_WRITE, 0, 0,
	//		OPEN_EXISTING, 0, 0);
	//	if (hComm == INVALID_HANDLE_VALUE)
	//		prints(L"INVALID COM HANDLE %s\n", dataBuffer);
	//	else {
	//		sendString(hComm, "?\n");
	//		receiveString(hComm, (char*)dataBuffer);
	//		char id = *((char*)dataBuffer);
	//		COMPorts[id - 1] = hComm; //assigns the port to the correct handle
	//	}

	//}
}

void checkCommand() {

}



void sendString(HANDLE hComm, char* outputBuffer) {
	DWORD bytesWritten;
	int len;
	for (len = 0; outputBuffer[len] != '\0'; ++len);
	WriteFile(hComm, outputBuffer, len, &bytesWritten, NULL);
}

//recieves the string upto the new line character and adds a zero character
int receiveString(HANDLE hComm, char* outputBuffer) {
	DWORD bytesRead;
	int i = 0;
	while (outputBuffer[i] != '\n' && outputBuffer[i] != '\r') {
		ReadFile(hComm, outputBuffer, 1, &bytesRead, NULL);
		++i;
	}
	outputBuffer[i + 1] = '\0';
	return i;
}

//sets the speed of a specific wheel to a value in the plate speed units
void setEngineRotation(int id, int speed) {
	char writeBuffer[32] = {};
	DWORD bytesWritten;
	int len = sprintf_s(writeBuffer, "%d:sd%d\n", id, speed);

	WriteFile(hCOMDongle, writeBuffer, len, &bytesWritten, NULL);
}

//sets the robot speed to specific values, wheels go 1-2-3-4 from front left to back right
//x axis is straight ahead, y axis is to the left
//in units m, s, degrees, positive angle turns left
void setSpeed(float speed, float angle, float angularVelocity) {
	float wheelRadius = 3.5 / 100; //in meters
	float baseRadius = 14.0 / 100; //base radius in meters from the center to the wheel

	const float realToPlateUnits = (18.75 * 64) / 62.5;
	float vx = speed*cosf(angle * PI / 180);
	float vy = speed*sinf(angle * PI / 180);
	prints(L"speed %.2f angle %.2f angularvelocity %.2f vx %.2f vy %.2f\n", speed, angle, angularVelocity, vx, vy);
	int wheel1Speed = (int)(((vx - vy) / (wheelRadius * SQRT2 * 2 * PI) - angularVelocity/360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel2Speed = (int)(((vx + vy) / (wheelRadius * SQRT2 * 2 * PI) + angularVelocity/360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel3Speed = (int)(((vx + vy) / (wheelRadius * SQRT2 * 2 * PI) - angularVelocity/360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel4Speed = (int)(((vx - vy) / (wheelRadius * SQRT2 * 2 * PI) + angularVelocity/360 * baseRadius / wheelRadius)*realToPlateUnits);
	if (wheel1Speed > 190 || wheel2Speed > 190 || wheel3Speed > 190 || wheel4Speed > 190)
		prints(L"Wheel speed overflow.");
	prints(L"%d %d %d %d\n", wheel1Speed, wheel2Speed, wheel3Speed, wheel4Speed);
	setEngineRotation(1, wheel1Speed);
	setEngineRotation(2, wheel2Speed);
	setEngineRotation(3, wheel3Speed);
	setEngineRotation(4, wheel4Speed);
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
	if (x * 10000 < (y < 0 ? -y : y)) {
		angle = PI / 2;
	}
	else {
		angle = atanf(y / x);
	}
	return angle;
}

void initUSBRadio() {
	DCB dcb;
	HANDLE hCOMRadio;
	hCOMRadio = CreateFile(L"\\\\.\\COM5", GENERIC_READ | GENERIC_WRITE, 0, 0,
		OPEN_EXISTING, 0, 0);

	if (hCOMRadio == INVALID_HANDLE_VALUE) {
		prints(L"ERROR OPENING USB COM PORT\N");
	}
	ZeroMemory(&dcb, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);

	if (!GetCommState(hCOMRadio, &dcb))
	{
		prints(L"GetCommState failed with error %d.\n", GetLastError());
	}
	prints(L"Baudrate %d\n", dcb.BaudRate);

	dcb.BaudRate = 9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	if (!SetCommState(hCOMRadio, &dcb))
	{
		prints(L"SetCommState failed with error %d.\n", GetLastError());
	}


	char* buffer = "+++\0";
	DWORD bytesRead = 0;
	WriteFile(hCOMRadio, buffer, 3, &bytesRead, NULL);
	prints(L"Wrote %d bytes.\n", bytesRead);

	char readBuffer[128] = {};
	bytesRead = 0;
	Sleep(1000);
	if (!ReadFile(hCOMRadio, readBuffer, 2, &bytesRead, NULL))
		prints(L"Read failed with error %X.\n", GetLastError());

	prints(L"Read %d bytes.\n", bytesRead);

	prints(L"%S\n", readBuffer);
	if (lstrcmpA(readBuffer, "OK") == 0) {
		prints(L"USB Radio initialization successful.\n");
	}
	else
	{
		prints(L"Did not receive OK from the radio.\n");
	}
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
