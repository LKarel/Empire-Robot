#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define PI (3.1415927)
#define SQRT2 (1.4142135)

float wheelRadius = 3.5 / 100.0; //in meters
float baseRadius = 14.0 / 100.0; //base radius in meters from the center to the wheel
float height = 17.0 / 100.0; //height in meters of the camera
float cameraAngle = 25.0 * PI / 180.0; //the angle that the camera is down with respect to the horizontal, height of camera is 17cm, midpoint of the image is 37 cm away
float angleOfView = 59.0 * PI / 180.0; //the horizontal angle of view, it saw a 77cm wide rule 67cm away along the floor

HANDLE readySignal = CreateEventW(NULL, FALSE, FALSE, NULL);
HANDLE newImageAnalyzed = CreateEventW(NULL, FALSE,FALSE,NULL);
HANDLE startSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
HANDLE stopSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
HANDLE calibratingSignal = CreateEventW(NULL, TRUE, FALSE, NULL);
HANDLE calibratingEndSignal = CreateEventW(NULL, TRUE, FALSE, NULL);
HANDLE GUIThread;
HANDLE hCOMDongle;
HANDLE hCOMRadio;
int ballsPixelCount = 0, goalsBluePixelCount = 0, goalsYellowPixelCount = 0, linesPixelCount = 0;
OVERLAPPED osReader = { };
OVERLAPPED osWrite = { };
OVERLAPPED osStatus = { };

DWORD dwCommEvent = 0; //variable for the WaitCommEvent, stores the type of the event that occurred
int listenToRadio; //whether to listen to commands from the radio or not
char* currentID;
drivingState currentDrivingState;
extern objectCollection ballsShare, goalsBlueShare, goalsYellowShare;
extern HANDLE writeMutex;
extern BOOLEAN calibrating;

void sendString(HANDLE hComm, char* outputBuffer);
int receiveString(HANDLE hComm, char* outputBuffer);
void initCOMPort();
void initUSBRadio();
void setSpeedAngle(float speed, float angle, float angularVelocity);
void setSpeedAngle(drivingState state);
void setSpeedXY(float vx, float vy, float angularVelocity);
void setSpeedXY(drivingState state);
void setSpeedBoth(drivingState state);
void testSingleBallTrack();
void test();
void receiveCommand();
void read();
void checkCommand(char* readBuffer);
boolean validCommand(char* readBuffer);
void readCOM(HANDLE hCOM, char* readBuffer, DWORD bytesToRead, DWORD &bytesRead);
void rotateAroundCenter(float angularVelocity);
void findNearestObject(int& currentx, int& currenty, objectCollection& objects);
void findLargestObject(int& currentx, int& currenty, objectCollection& objects);
void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY);
void driveToFloorXY(float floorX, float floorY, float angle);
void rotateAroundFront(float angularVelocity);
void rotateAroundFrontAndMoveForward(float angularVelocity, float speedForward);
bool checkIsBallInDribbler();
bool isLineInFront();

enum State { lookForBall, lookForGoal, driveToBall, driveToGoal, rotate180 } state;

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

//initialize the state and start testing the robot
state = lookForBall;
test();

//Don't exit this thread before the GUI
WaitForSingleObject(GUIThread, INFINITE);
}

void test() {
	initCOMPort();
	initUSBRadio();
	HANDLE waitHandles[2] = { osStatus.hEvent, startSignal };
	while (true) {
		switch (WaitForMultipleObjects(2, waitHandles, FALSE, 1000)) {
		case WAIT_OBJECT_0:
			prints(L"radio event %X\n", dwCommEvent);
			receiveCommand();
			WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus);
			break;
		case WAIT_OBJECT_0 + 1:
			prints(L"Start signal arrived.\n");
			testSingleBallTrack();
			break;
		}
	}
}

void testSingleBallTrack() {
	HANDLE waitHandles[4] = { newImageAnalyzed, stopSignal, osStatus.hEvent, calibratingSignal };
	int currentx = 320, currenty = 0;
	int timeOut = 50; //timeout after sending each command
	objectCollection goals;

	while (true) {
		switch (WaitForMultipleObjects(4, waitHandles, FALSE, timeOut)) {
		case WAIT_OBJECT_0: //new image analyzed
			break;
		case WAIT_OBJECT_0 + 1: //stop signal
			prints(L"Stop signal arrived.\n");
			currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.vx = 0, 
				currentDrivingState.vy = 0, currentDrivingState.angularVelocity = 0;
			setSpeedAngle(currentDrivingState);
			ResetEvent(startSignal);
			return;
		case WAIT_OBJECT_0 + 2: //start of a new command from the radio detected
			//ResetEvent(osStatus.hEvent);
			prints(L"radio event %X\n", dwCommEvent);
			receiveCommand();
			WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus);
			continue;
		case WAIT_OBJECT_0 + 3: //calibrating signal
			prints(L"Calibrating signal.\n");
			setSpeedAngle(0, 0, 0);
			WaitForSingleObject(calibratingEndSignal, INFINITE);
			continue;
		}
		rotateAroundFront(20.0);
		continue;
		goals = currentID[1] == 'A' ? goalsBlueShare : goalsYellowShare;

		if (state == lookForBall) {
			if (ballsShare.count == 0) {
				rotateAroundCenter(-60);
			}
			else {
				state = driveToBall;
			}
		}
		if (state == driveToBall) {
			float floorX, floorY;
			findNearestObject(currentx, currenty, ballsShare);
			convertToFloorCoordinates(currentx, currenty, floorX, floorY);
			prints(L"Floor coordinates x: %.2f, y: %.2f\n", floorX, floorY);
			if (ballsShare.count == 0) {
				prints(L"Drive state, but ballcount zero, starting to look for ball.\n");
				state = lookForBall;
				continue;
			}
			if (floorX < 20.0 / 100.0) {
				state = lookForGoal;
			}
			else {
				driveToFloorXY(floorX, floorY, NULL);
			}
		}
		if (state == lookForGoal) {
			prints(L"Looking for goal\n");
			if (checkIsBallInDribbler()) {
				int x = 0, y = 0;
				findLargestObject(x, y, goals);
				x -= 320, y -= 240;
				if (abs(x) < 30) {
					state = driveToGoal;
				}
				else {
					rotateAroundFront(-60 * tanhf((x - 320) / 130));
				}
			}
			else {
				state = lookForBall;
			}
		}
		if (state == driveToGoal) {
			if (checkIsBallInDribbler()) {
				if (goals.count > 0) {
					if (isLineInFront()) {
						state = rotate180;
					}
					else {
						int x = 0, y = 0;
						findLargestObject(x, y, goals);
						rotateAroundFrontAndMoveForward(-60 * tanhf((x - 320) / 130), 0.1);
					}
				}
				else {
					state = lookForGoal;
				}
			}
			else{
				state = lookForBall;
			}
		}
	}
}

bool isLineInFront() {
	return FALSE;
}

bool checkIsBallInDribbler() {
	float floorX, floorY;
	for (int i = 0; i < ballsShare.count; ++i) {
		convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].x, floorX, floorY);
		if (floorX < 20.0 / 100.0 && fabs(floorY) < 5.0 / 100.0){
			return TRUE;
		}
	}
	return FALSE;
}

void rotateAroundFrontAndMoveForward(float angularVelocity, float speedForward) {
	currentDrivingState.speed = -angularVelocity * PI / 180 * baseRadius;
	currentDrivingState.angle = 90, currentDrivingState.angularVelocity = angularVelocity;
	currentDrivingState.vx = speedForward, currentDrivingState.vy = 0;
	setSpeedBoth(currentDrivingState);
}

void rotateAroundFront(float angularVelocity) {
	currentDrivingState.speed = -angularVelocity * PI / 180 * baseRadius;
	currentDrivingState.angle = 90, currentDrivingState.angularVelocity = angularVelocity;
	setSpeedAngle(currentDrivingState);
}

//start driving to the position with coordinates in the floor system, try to rotate by an angle before arriving
void driveToFloorXY(float floorX, float floorY, float angle) {
	floorX = floorX < 1 / 100 ? 1 / 100 : floorX;
	floorY = floorY < 1 / 100 ? 1 / 100 : floorY;
	float dist = powf(floorX*floorX + floorY*floorY, 0.5); //distance to the ball
	float speedMultiplier = tanhf(dist/0.7); //use square root of the distance for modulating speed
	float speedBase = 0.5; //max speed
	float speed = speedBase*speedMultiplier;
	float time = dist / speed;
	float angleToBall = atanf(floorY / floorX)/PI*180;
	prints(L"Angle to ball: %.2f\n", angleToBall);
	currentDrivingState.angularVelocity = speedBase * 5 * angleToBall;
	currentDrivingState.speed = speed;
	currentDrivingState.angle = angleToBall;
	setSpeedAngle(currentDrivingState);
}

void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY) {
	//x, y, z are the coordinates on the floor, x axis straight ahead, y axis to the left
	vector eCameraNormal = { cosf(cameraAngle), 0, -sinf(cameraAngle) };	//basis vector in the direction the camera is pointing
	vector ex = { 0, -1 * tanf(angleOfView / 2) / 320, 0 };		//the vector per pixel along the x direction from the center of the image considering camera normal vector is length 1
	vector ey = { sinf(cameraAngle) * tanf(angleOfView / 2) / 320, 0, cosf(cameraAngle) * tanf(angleOfView / 2) / 320 };	//same in the y direction

	vector ball = { (currenty - 240) * ey.e1 + eCameraNormal.e1, 
					(currentx - 320) * ex.e2 + eCameraNormal.e2, 
					(currenty - 240) * ey.e3 + eCameraNormal.e3 };
	ball.e3 = fabs(ball.e3) < 1 / 10000 ? 1 / 10000 : fabs(ball.e3);

	floorX = ball.e1 * height / ball.e3, floorY = ball.e2 * height / ball.e3;
}

//rotates around the center with the given angular velocity in degs/s
void rotateAroundCenter(float angularVelocity) {
	currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.angularVelocity = angularVelocity;
	setSpeedAngle(currentDrivingState);
}

//finds an object nearest to the old x, y values
void findNearestObject(int& currentx, int& currenty, objectCollection& objects) {
	int minDist = (currentx - objects.data[0].x) * (currentx - objects.data[0].x) + (currenty - objects.data[0].y) * (currenty - objects.data[0].y), minIndex = 0;
	for (int i = 0; i < objects.count; ++i) {
		int dist = (currentx - objects.data[i].x) * (currentx - objects.data[i].x) + (currenty - objects.data[i].y) * (currenty - objects.data[i].y);
		if (dist < minDist) {
			minDist = dist;
			minIndex = i;
		}
	}
	currentx = objects.data[minIndex].x, currenty = objects.data[minIndex].y;
}

//finds an object with the largest pixelcount
void findLargestObject(int& x, int& y, objectCollection& objects) {
	int largestPixelCount = 0;
	for (int i = 0; i < objects.count; ++i) {
		int pixelCount = objects.data[i].pixelcount;
		if (pixelCount > largestPixelCount) {
			x = objects.data[i].x, y = objects.data[i].y;
		}
	}
}

void respondACK() {
	char response[16] = {};
	DWORD bytesWritten = 0;
	sprintf_s(response, "a%sACK------", currentID);
	if (!WriteFile(hCOMRadio, response, 12, &bytesWritten, &osWrite)) {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"USB radio ACK response failed with error %X\n", GetLastError());
		}
		if (!GetOverlappedResult(hCOMRadio, &osWrite, &bytesWritten, TRUE)) {
			prints(L"USB ACK radio write failed with error %X\n", GetLastError());
		}
	}
	if (bytesWritten != 12) {
		prints(L"ACK write timeout\n");
	}

	prints(L"Responded: %S\n", response);
}

void receiveCommand() { //the character 'a' was received from the radio, now read the buffer, find all the a-s and check for commands
	DWORD bytesRead = 0;
	DWORD start = 0, length = 12; //the current position in the buffer and the length of the unread buffer
	char readBuffer[128] = {};
	readCOM(hCOMRadio, readBuffer, 12, bytesRead);
	prints(L"Checking command: %S, valid: %d\n", readBuffer, validCommand(readBuffer));

	while (true) {
		if (readBuffer[start] != 'a') {
			++start;
			--length;
		}
		else {
			if (length < 12) {
				readCOM(hCOMRadio, readBuffer + start, 12, bytesRead);
				if (bytesRead + length < 12) {
					return;
				}
				if (!validCommand(readBuffer + start)) {
					++start;
					--length;
				}
				else {
					checkCommand(readBuffer + start);
				}
			}
			else {
				checkCommand(readBuffer + start);
				++start;
				--length;
			}
		}
		if (length == 0) {
			readCOM(hCOMRadio, readBuffer + start, 12, bytesRead);
			if (bytesRead < 12) {
				return;
			}
			length = bytesRead;
		}
		if (start > 40) {
			CopyMemory(readBuffer, readBuffer + start, length);
			start = 0;
		}
	}
}


//initializes the COM ports to the right handles; not needed, we have a dongle
void initCOMPort() {
	DCB dcb = {};
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = 5;
	timeouts.ReadTotalTimeoutConstant = 5;
	timeouts.ReadTotalTimeoutMultiplier = 5;
	timeouts.WriteTotalTimeoutConstant = 10;
	timeouts.WriteTotalTimeoutMultiplier = 5;

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
	if (!GetCommState(hCOMDongle, &dcb))
	{
		prints(L"Dongle GetCommState failed with error %X.\n", GetLastError());
	}
	prints(L"Baudrate %d\n", dcb.BaudRate);

	dcb.BaudRate = 19200;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	if (!SetCommState(hCOMDongle, &dcb))
	{
		prints(L"Dongle SetCommState failed with error %X.\n", GetLastError());
	}
	if (!SetCommTimeouts(hCOMDongle, &timeouts)) {
		prints(L"Dongle SetCommTimeouts failed with error %d.\n", GetLastError());
	}
	else //turn off the failsafe of the engine controllers
	{
		sendString(hCOMDongle, "1:fs0\n");
		sendString(hCOMDongle, "2:fs0\n");
		sendString(hCOMDongle, "3:fs0\n");
		sendString(hCOMDongle, "4:fs0\n");
	}
	prints(L"COM init end\r\n\r\n");
}

boolean validCommand(char* readBuffer) {
	char tempBuffer[32] = {};
	CopyMemory(tempBuffer, readBuffer, 12);
	if (lstrcmpA(tempBuffer + 3, "STOP-----") != 0 && lstrcmpA(tempBuffer + 3, "START----") != 0) {
		prints(L"here1");
		return FALSE;
	}
	if (tempBuffer[0] != 'a') {
		return FALSE;
	}
	if (tempBuffer[1] != 'A' && tempBuffer[1] != 'B') {
		return FALSE;
	}
	char* allowed = "XABCD";
	for (int i = 0; i < strlen(allowed); ++i) {
		if (allowed[i] == tempBuffer[2]) {
			prints(L"Received from radio: %S\n", tempBuffer);
			return TRUE;
		}
	}
	return FALSE;
}

void checkCommand(char* readBuffer) {
	char tempBuffer[32] = {};
	CopyMemory(tempBuffer, readBuffer, 12);
	if (validCommand(readBuffer)) {
		if (readBuffer[1] == currentID[0]) {
			if (readBuffer[2] == 'X' || readBuffer[2] == currentID[1]) {
				if (strcmp(tempBuffer+3, "STOP-----") == 0) {
					ResetEvent(startSignal);
					SetEvent(stopSignal);
					respondACK();
				}
				else if (strcmp(tempBuffer+3, "START----") == 0) {
					ResetEvent(stopSignal);
					SetEvent(startSignal);
					respondACK();
				}
			}
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
//x axis is straight ahead, y axis is to the left, angle is with respect to x axis
//in units m, s, degrees, positive angle turns left, uses only the angle
void setSpeedAngle(float speed, float angle, float angularVelocity) {
	float vx = speed*cosf(angle * PI / 180);
	float vy = speed*sinf(angle * PI / 180);
	setSpeedXY(vx, vy, angularVelocity);
}

void setSpeedAngle(drivingState state) {
	setSpeedAngle(state.speed, state.angle, state.angularVelocity);
}

//sets the speed by adding up the vx, vy and the values got from the angle and speed
void setSpeedBoth(drivingState state) {
	setSpeedXY(state.vx + state.speed*cosf(state.angle * PI / 180), state.vy + state.speed*sinf(state.angle * PI / 180), state.angularVelocity);
}

//sets the robot speed to specific values, wheels go 1-2-3-4 from front left to back right
//x axis is straight ahead, y axis is to the left
//in units m, s, degrees, positive angle turns left
void setSpeedXY(float vx, float vy, float angularVelocity) {

	const float realToPlateUnits = (18.75 * 64) / 62.5;

	int wheel1Speed = (int)(((vx - vy) / (wheelRadius * SQRT2 * 2 * PI) - angularVelocity / 360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel2Speed = (int)(((vx + vy) / (wheelRadius * SQRT2 * 2 * PI) + angularVelocity / 360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel3Speed = (int)(((vx + vy) / (wheelRadius * SQRT2 * 2 * PI) - angularVelocity / 360 * baseRadius / wheelRadius)*realToPlateUnits);
	int wheel4Speed = (int)(((vx - vy) / (wheelRadius * SQRT2 * 2 * PI) + angularVelocity / 360 * baseRadius / wheelRadius)*realToPlateUnits);
	if (wheel1Speed > 190 || wheel2Speed > 190 || wheel3Speed > 190 || wheel4Speed > 190)
		prints(L"Wheel speed overflow.");

	setEngineRotation(1, wheel1Speed);
	setEngineRotation(2, wheel2Speed);
	setEngineRotation(3, wheel3Speed);
	setEngineRotation(4, wheel4Speed);
}

void setSpeedXY(drivingState state) {
	setSpeedXY(state.vx, state.vy, state.angularVelocity);
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
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = 5;
	timeouts.ReadTotalTimeoutConstant = 5;
	timeouts.ReadTotalTimeoutMultiplier = 5;
	timeouts.WriteTotalTimeoutConstant = 10;
	timeouts.WriteTotalTimeoutMultiplier = 5;

	osReader.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osStatus.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	DCB dcb;
	hCOMRadio = CreateFile(L"\\\\.\\COM4", GENERIC_READ | GENERIC_WRITE, 0, 0,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	if (hCOMRadio == INVALID_HANDLE_VALUE) {
		prints(L"ERROR OPENING USB COM PORT\N");
	}
	ZeroMemory(&dcb, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);

	if (!GetCommState(hCOMRadio, &dcb))
	{
		prints(L"GetCommState failed with error %d.\n", GetLastError());
	}
	prints(L"Baudrate %d abort on error %d\n", dcb.BaudRate, dcb.fAbortOnError);

	dcb.BaudRate = 9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.EvtChar = 'a';

	if (!SetCommState(hCOMRadio, &dcb))
	{
		prints(L"SetCommState failed with error %d.\n", GetLastError());
	}

	if (!SetCommTimeouts(hCOMRadio, &timeouts)) {
		prints(L"SetCommTimeouts failed with error %d.\n", GetLastError());
	}

	if (!SetCommMask(hCOMRadio, EV_RXFLAG)) {
		prints(L"SetCommMask failed with error %d.\n", GetLastError());
	}

	if (WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus)) {
		prints(L"Wait Com event %X\n", dwCommEvent);
	}
	else {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"WaitCommEvent failed with error %d\n", GetLastError());
		}
	}
}

void testUSBOK() {
	char* buffer = "+++";
	DWORD bytesRead = 0;
	DWORD bytesWritten = 0;
	WriteFile(hCOMRadio, buffer, 3, &bytesRead, &osWrite);
	if (!GetOverlappedResult(hCOMRadio, &osWrite, &bytesWritten, TRUE)) {
		prints(L"Init USB radio write failed\n");
	}
	if (bytesWritten != 3) {
		prints(L"Init write timeout\n");
	}

	prints(L"Init wrote %d bytes.\n", bytesWritten);

	char readBuffer[128] = {};
	bytesRead = 0;

	ReadFile(hCOMRadio, readBuffer, 2, &bytesRead, &osReader);
	int i;
	for (i = 0; i < 20;++i) {
		if (!GetOverlappedResult(hCOMRadio, &osReader, &bytesRead, TRUE)) {
			prints(L"Init read failed, error %d\n", GetLastError());
			break;
		}
		if (bytesRead != 2) {
			ReadFile(hCOMRadio, readBuffer, 2, &bytesRead, &osReader);
		}
		else {
			if (lstrcmpA(readBuffer, "OK") == 0) {
				prints(L"USB Radio initialization successful.\r\n\r\n");
			}
			else
			{
				prints(L"Received something else from the radio.\r\n\r\n");
			}
			break;
		}
		Sleep(100);
	}
	if (i == 20)
		prints(L"Did not receive OK in 2 seconds from the radio\n");
}

//read from the OVERLAPPED COM-port until timeouts set when initializing
void readCOM(HANDLE hCOM, char* readBuffer, DWORD bytesToRead, DWORD &bytesRead) {
	if (!ReadFile(hCOM, readBuffer, bytesToRead, &bytesRead, &osReader)) {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"USB radio read failed with error %X\n", GetLastError());
		}
		else {
			if (!GetOverlappedResult(hCOMRadio, &osReader, &bytesRead, TRUE)) {
				prints(L"USB radio read GetOverlappedResult failed with error %X\n", GetLastError());
			}
		}
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
