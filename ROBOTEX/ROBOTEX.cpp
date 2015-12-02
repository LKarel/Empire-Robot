#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define PI (3.1415927)
#define SQRT2 (1.4142135)
#define COMPORTRADIO L"\\\\.\\COM4" //4 on NUC
#define COMPORTDONGLE L"COM3" //3 on NUC
#define CLOSEBALL 3.0 //distance to a close enough ball which will be driven to instead of looking for another one

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
bool isBallInDribbler, ignoreBall;
float ignoreX, ignoreY;
OVERLAPPED osReader = { };
OVERLAPPED osWrite = { };
OVERLAPPED osStatus = { };
OVERLAPPED osReaderDongle = { };
OVERLAPPED osWriteDongle = { };
OVERLAPPED osStatusDongle = { };
LARGE_INTEGER timerFrequency;
LARGE_INTEGER startCounter;
LARGE_INTEGER nearestFoundTimeCounter;
LARGE_INTEGER stopCounter;
LARGE_INTEGER chargingStart;
LARGE_INTEGER chargingStop;
LARGE_INTEGER startFPS2Counter;
LARGE_INTEGER stopFPS2Counter;

LARGE_INTEGER lastSpeedSent;
LARGE_INTEGER lastSpeedSentStop;

DWORD dwCommEvent = 0; //variable for the WaitCommEvent, stores the type of the event that occurred
DWORD dwCommEventDongle = 0; //variable for the WaitCommEvent, stores the type of the event that occurred
bool charged;
int listenToRadio; //whether to listen to commands from the radio or not
char* currentID;
drivingState currentDrivingState;
extern objectCollection ballsShare, goalsBlueShare, goalsYellowShare;
extern lineCollection linesShare;
extern HANDLE writeMutex;
extern BOOLEAN calibrating;
extern HWND stateStatusGUI;
extern HWND goalGuessState;
extern HWND statusBall;
extern HWND infoGUI;
extern HWND infoGUI2;
extern HWND statusFPS2;

void sendString(HANDLE hComm, char* outputBuffer);
int receiveString(HANDLE hComm, char* outputBuffer, DWORD bytesRead);
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
void findNearestObjectToOldObject(int& currentx, int& currenty, objectCollection& objects);
void findNearestFloorObjectToOldObject(float& currentx, float& currenty, objectCollection& objects);
int findNearestFloorBall(float& nearestBallX, float& nearestBallY, int& currentx, int& currenty);
void findLargestObject(int& currentx, int& currenty, objectCollection& objects);
void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY);
void driveToFloorXY(float floorX, float floorY);
void rotateAroundFront(float angularVelocity);
void rotateAroundFrontAndMoveForward(float angularVelocity, float speed, float angle);
//bool checkIsBallInDribbler();
bool isLineInFront();
//void dribblerON();
//void dribblerOFF();
void charge();
void discharge();
void kick(int microSeconds);
void kick();
void handleMainBoardCommunication();
//bool isLineBetweenRobotAndBall(int currentx, int currenty);
float time(LARGE_INTEGER start, LARGE_INTEGER stop);

enum State { driveToNearestBall, lookForBall, scanForNearBall, lookForNearBallIgnoreStraight, turnToNearestBall, lookForGoal, driveToBall, kickBall, rotate180,
			 nextTurnLeft, nextTurnRight, goalOnLeft, goalOnRight} state, state2, stateGoal;

bool attackBlue;

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
	//sendString(hCOMDongle, "9:fs0\n");
	QueryPerformanceFrequency(&timerFrequency); //used for calculating the time by the counter values
	QueryPerformanceCounter(&lastSpeedSent);
	HANDLE waitHandles[3] = { osStatus.hEvent, startSignal, osStatusDongle.hEvent };
	while (true) {
		float floorX, floorY;
		int currentx, currenty;
		findNearestFloorBall(floorX, floorY, currentx, currenty);
		float angle = atanf(floorY / floorX) * 180.0 / PI;

		wchar_t buffer[64] = {};
		swprintf_s(buffer, L"NX %.2f \n NY %.2f\n ang %.2f", floorX, floorY, angle);
		SetWindowText(infoGUI, buffer);

		if (hCOMDongle != INVALID_HANDLE_VALUE) {
			QueryPerformanceCounter(&lastSpeedSentStop);
			if (time(lastSpeedSent, lastSpeedSentStop) > 0.5) { //if we send the speeds too often we get communication timeouts
				setSpeedBoth(currentDrivingState);
				QueryPerformanceCounter(&lastSpeedSent);
			}
		}
		switch (WaitForMultipleObjects(3, waitHandles, FALSE, 1000)) {
		case WAIT_OBJECT_0:
			prints(L"radio event %X\n", dwCommEvent);
			receiveCommand();
			WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus);
			break;
		case WAIT_OBJECT_0 + 1:
			prints(L"Start signal arrived.\n");
			SetWindowText(stateStatusGUI, L"started");
			testSingleBallTrack();
			//dribblerOFF();
			//discharge();
			break;
		case WAIT_OBJECT_0 + 2: //info from the main board controller
			//prints(L"main board COM event %X\n", dwCommEventDongle);
			handleMainBoardCommunication();
			WaitCommEvent(hCOMDongle, &dwCommEventDongle, &osStatusDongle); //listen to new events, ie beginning character
			break;
		}
	}
}

void testSingleBallTrack() {
	HANDLE waitHandles[5] = { osStatus.hEvent, stopSignal, osStatusDongle.hEvent, calibratingSignal, newImageAnalyzed };
	int currentx = 320, currenty = 0, FPS2Count = 0;
	int timeOut = 50;
	float movingTime; //timeout after sending each command
	objectCollection goals, goals2;
	float nearestBallFloorX = 0, nearestBallFloorY = 0;
	ignoreX = 0, ignoreY = 0;
	LARGE_INTEGER ignoreStart = {}, ignoreStop = {}, kickingStart = {}, kickingStop = {};

	state = driveToNearestBall;
	stateGoal = goalOnRight;
	SetWindowText(goalGuessState, L"Goal on right");
	isBallInDribbler = false;
	prints(L"Started, going to the nearest ball\n");
	SetWindowText(stateStatusGUI, L"Started to nearest");
	//dribblerON();
	charged = false, ignoreBall = false;
	charge();
	QueryPerformanceCounter(&chargingStart);
	sendString(hCOMDongle, "9:bl\n");

	while (true) {


		//display test stuff

		float floorX, floorY;
		int currentx, currenty;
		findNearestFloorBall(floorX, floorY, currentx, currenty);
		float angle = atanf(floorY / floorX) * 180.0 / PI;

		wchar_t buffer[64] = {};
		swprintf_s(buffer, L"NX %.2f \n NY %.2f\n ang %.2f", floorX, floorY, angle);
		SetWindowText(infoGUI, buffer);

		//end display test stuff

		//QueryPerformanceCounter(&chargingStop);
		//if (state == kickBall && double(chargingStop.QuadPart - chargingStart.QuadPart) / double(timerFrequency.QuadPart) > 5.0) {//too many seconds passed from the last charge, charge again
		//	charge();
		//	QueryPerformanceCounter(&chargingStart);
		//}
		switch (WaitForMultipleObjects(5, waitHandles, FALSE, timeOut)) {
		case WAIT_OBJECT_0: //start of a new command from the radio detected
								//ResetEvent(osStatus.hEvent);
			prints(L"radio event %X\n", dwCommEvent);
			receiveCommand(); //receive and interpret the command
			WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus); //listen to new events, ie beginning character
			continue;
		case WAIT_OBJECT_0 + 1: //stop signal
			prints(L"Stop signal arrived.\n");
			SetWindowText(stateStatusGUI, L"stopped");
			currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.vx = 0,
				currentDrivingState.vy = 0, currentDrivingState.angularVelocity = 0;
			setSpeedAngle(currentDrivingState);
			ResetEvent(startSignal);
			return;
		case WAIT_OBJECT_0 + 2: //info from the main board controller
								//prints(L"main board COM event %X\n", dwCommEvent);
			handleMainBoardCommunication();
			WaitCommEvent(hCOMDongle, &dwCommEventDongle, &osStatusDongle); //listen to new events, ie beginning character
			continue;
		case WAIT_OBJECT_0 + 3: //calibrating signal
			SetWindowText(stateStatusGUI, L"calibrating");
			prints(L"Calibrating signal.\n");
			currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.vx = 0,
				currentDrivingState.vy = 0, currentDrivingState.angularVelocity = 0;
			setSpeedBoth(currentDrivingState);
			//WaitForSingleObject(calibratingEndSignal, INFINITE);
			continue;
		case WAIT_OBJECT_0 + 4: //new image analyzed
			//update FPS rate after every 5 images
			++FPS2Count;
			if (FPS2Count % 5 == 0) {
				wchar_t data[32] = {};
				QueryPerformanceCounter(&stopFPS2Counter);
				float FPS = 5.0 / time(startFPS2Counter, stopFPS2Counter);
				swprintf_s(data, L"FPS2: %.2f", FPS);
				SetWindowText(statusFPS2, data);
				startFPS2Counter.QuadPart = stopFPS2Counter.QuadPart;
			}

			if (calibrating) {
				setSpeedBoth(currentDrivingState);
			}
			else {
				break;
			}
		}

		if (attackBlue) { //set the side
			goals = goalsBlueShare;
			goals2 = goalsYellowShare;
		}
		else {
			goals = goalsYellowShare;
			goals2 = goalsBlueShare;
		}

		if (ignoreBall) { //ignore the ball just kicked for 1s
			QueryPerformanceCounter(&ignoreStop);
			if (time(ignoreStart, ignoreStop) > 1.0) {
				ignoreBall = false;
				ignoreX = 0, ignoreY = 0;
			}
			else {
				float ignoreXNew = ignoreX, ignoreYNew = ignoreY;
				findNearestFloorObjectToOldObject(ignoreXNew, ignoreYNew, ballsShare);
				//if we cant see the ball behind the dribbler, don't start ignoring a ball too far away
				if ((ignoreXNew - ignoreX)*(ignoreXNew - ignoreX) + (ignoreYNew - ignoreY)*(ignoreYNew - ignoreY)) { 
					ignoreX = ignoreXNew;
					ignoreY = ignoreYNew;
				}
				wchar_t buffer[64] = {};
				swprintf_s(buffer, L"IgX %.2f IgY %.2f\n NX %.2f NY %.2f", ignoreX, ignoreY, ignoreXNew, ignoreYNew);
				SetWindowText(infoGUI2, buffer);
			}
		}
		else {
			SetWindowText(infoGUI2, L"Ignore OFF");
		}

		//keep track of the likely direction to turn to to find the goal
		if ((goalsBlueShare.count > 0 || goalsYellowShare.count > 0) && !(goalsBlueShare.count == 1 && goalsYellowShare.count == 1)) {
			int goalX, goalY;
			State stateGoalNew;
			if (goals.count > 0) {
				findLargestObject(goalX, goalY, goals);
				stateGoalNew = goalX > 320 ? goalOnRight : goalOnLeft;
			}
			else if (goals2.count > 0) {
				findLargestObject(goalX, goalY, goals2);
				stateGoalNew = goalX > 320 ? goalOnLeft : goalOnRight;
			}
			if (stateGoalNew != stateGoal) {
				stateGoal = stateGoalNew;
				if (stateGoal == goalOnRight) {
					SetWindowText(goalGuessState, L"Goal on the right");
				}
				else {
					SetWindowText(goalGuessState, L"Goal on the left");
				}
			}
		}
		
		if (state == driveToNearestBall) {
			if (isBallInDribbler) {
				state = lookForGoal;
			}
			else {
				//drive to the nearest ball in the beginning
				SetWindowText(stateStatusGUI, L"Driving to nearest");
				nearestBallFloorX = 0;
				if (ballsShare.count == 0) {
					state = lookForBall;
					QueryPerformanceCounter(&startCounter);
				}
				else {
					state = driveToBall;
					//prints(L"Nearest ball floorX: %.2f, floorY: %.2f, currentx: %d, currenty: %d\n", nearestBallFloorX, nearestBallFloorY, currentx, currenty);
					prints(L"Driving to ball\n");
				}
			}
		}

		if (state == lookForBall) {
			if (isBallInDribbler) {
				state = lookForGoal;
				QueryPerformanceCounter(&startCounter);
			}
			else {
				//prints(L"Looking for ball\n");
				SetWindowText(stateStatusGUI, L"Looking for ball");
				nearestBallFloorX = 0;
				if (ballsShare.count == 0) {
					rotateAroundCenter(-200);
				}
				else {
					findNearestFloorBall(nearestBallFloorX, nearestBallFloorY, currentx, currenty);
					if (nearestBallFloorX != 0 && nearestBallFloorX*nearestBallFloorX + nearestBallFloorY*nearestBallFloorY < CLOSEBALL) { //if close enough
						state = driveToBall;
						//prints(L"Nearest ball floorX: %.2f, floorY: %.2f, currentx: %d, currenty: %d\n", nearestBallFloorX, nearestBallFloorY, currentx, currenty);
						prints(L"Driving to ball\n");
					}
					else {
						rotateAroundCenter(-200);
						movingTime = 4;
						QueryPerformanceCounter(&startCounter);
						nearestBallFloorX = 0, nearestBallFloorY = 0; //initialize the values so that a new nearest can be found
						state = scanForNearBall;
						prints(L"Scanning for a near ball\n");
					}
				}
			}
		}
		if (state == scanForNearBall) { //there wasn't a ball less than 1m away, turn until you find one, finally turn to the nearest
			//prints(L"Looking for a near ball\n");
			if (isBallInDribbler) {
				state = lookForGoal;
				QueryPerformanceCounter(&startCounter);
			}
			else {
				SetWindowText(stateStatusGUI, L"Scanning for a near ball");
				QueryPerformanceCounter(&stopCounter);
				for (int i = 0; i < ballsShare.count; ++i) {
					float floorX, floorY;
					convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].y, floorX, floorY);
					if ((nearestBallFloorX == 0 || floorX*floorX + floorY*floorY < nearestBallFloorX*nearestBallFloorX + nearestBallFloorY*nearestBallFloorY)
						&& !ballsShare.data[i].isObjectAcrossLine && !(ignoreBall && nearestBallFloorX == ignoreX && nearestBallFloorY == ignoreY)) {
						nearestBallFloorX = floorX, nearestBallFloorY = floorY;
						QueryPerformanceCounter(&nearestFoundTimeCounter);
						currentx = ballsShare.data[i].x, currenty = ballsShare.data[i].y;
					}
				}
				if (nearestBallFloorX != 0 && nearestBallFloorX*nearestBallFloorX + nearestBallFloorY*nearestBallFloorY < CLOSEBALL) { //if closer than 1 meter
					//prints(L"Found nearest ball floorX: %.2f, floorY: %.2f, currentx: %d, currenty: %d\n", nearestBallFloorX, nearestBallFloorY, currentx, currenty);
					state = driveToBall;
					QueryPerformanceCounter(&startCounter);
					prints(L"Driving to ball\n");
				}
				else if (time(startCounter, stopCounter) > movingTime) { //if we have rotated 360 degrees
					//rotateAroundCenter(-70);
					movingTime = 4.0;
					QueryPerformanceCounter(&startCounter);
					state = turnToNearestBall;
					if (time(nearestFoundTimeCounter, startCounter) > movingTime / 2) {
						state2 = nextTurnRight;
					}
					else {
						state2 = nextTurnLeft;
					}
					prints(L"Turning back to nearest ball\n");
				}
				else {
					rotateAroundCenter(-120);
				}
			}
		}
		if (state == turnToNearestBall) { //turn to the nearest ball found during scanning
			if (isBallInDribbler) {
				state = lookForGoal;
				QueryPerformanceCounter(&startCounter);
			}
			else {
				//prints(L"Turning back to nearest ball\n");
				SetWindowText(stateStatusGUI, L"Turning back to nearest ball");
				if (nearestBallFloorX == 0) {
					prints(L"Didn't find any balls after turning around\n");
					state = lookForBall;
					QueryPerformanceCounter(&startCounter);
					prints(L"Looking for ball\n");
					continue;
				}
				QueryPerformanceCounter(&stopCounter);
				for (int i = 0; i < ballsShare.count; ++i) {
					float floorX, floorY;
					convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].y, floorX, floorY);
					//not further than 0.2 m from the nearest ball
					if (pow(floorX*floorX + floorY*floorY, 0.5) < pow(nearestBallFloorX*nearestBallFloorX + nearestBallFloorY*nearestBallFloorY, 0.5) + 0.4) {
						currentx = ballsShare.data[i].x, currenty = ballsShare.data[i].y;
						state = driveToBall;
						QueryPerformanceCounter(&startCounter);
						prints(L"Driving to ball\n");
					}
				}
				if (time(startCounter, stopCounter) > movingTime) {//something went wrong, didn't find a nearest ball, look again
					prints(L"Couldn't turn back to the nearest ball.\n");
					state = lookForBall;
					QueryPerformanceCounter(&startCounter);
					prints(L"Looking for ball\n");
				}
				if (state == turnToNearestBall) {
					if (state2 == nextTurnRight) {
						rotateAroundCenter(-120);
					}
					else {
						rotateAroundCenter(120);
					}
				}
			}
		}
		if (state == driveToBall) {
			if (isBallInDribbler) {
				Sleep(150);
				state = lookForGoal;
				QueryPerformanceCounter(&startCounter);
			}
			else {
				//prints(L"Driving to ball\n");
				SetWindowText(stateStatusGUI, L"driving to ball");
				float floorX, floorY;
				//findNearestObject(currentx, currenty, ballsShare);
				//prints(L"Floor coordinates x: %.2f, y: %.2f, currentx: %d, currenty: %d\n", floorX, floorY, currentx, currenty);
				if (findNearestFloorBall(floorX, floorY, currentx, currenty) == 0) {
					//prints(L"Drive state, but ballcount zero, starting to look for ball.\n");
					state = lookForBall;
					QueryPerformanceCounter(&startCounter);
					prints(L"Looking for ball\n");
					continue;
				}
				if (isBallInDribbler) {
					state = lookForGoal;
					QueryPerformanceCounter(&startCounter);
					prints(L"Looking for goal\n");
				}
				//else if (isLineInFront()) {
				//	prints(L"Line in front\n");
				//	QueryPerformanceCounter(&startCounter);
				//	state = rotate180;
				//	currentDrivingState.angularVelocity = -90, currentDrivingState.speed = 0;
				//	setSpeedAngle(currentDrivingState);
				//	movingTime = 1.5;
				//}
				else {
					driveToFloorXY(floorX, floorY);
				}
			}
		}
		if (state == lookForGoal) {
			//prints(L"Looking for goal\n");
			SetWindowText(stateStatusGUI, L"Looking for goal");
			if (isBallInDribbler) {
				int x = 0, y = 0;
				float floorX, floorY;
				findLargestObject(x, y, goals);
				convertToFloorCoordinates(x, y, floorX, floorY);
				//prints(L"Goal x: %d\n", x);
				if (!(goalsBlueShare.count == 1 && goalsYellowShare.count == 1) &&
					(y > 0 && floorX > 0 && fabs(floorY) < 18.0 / 100.0 || abs(x-320) <= 35)) {
					state = kickBall;
					//charge();
					rotateAroundFront(0);
					QueryPerformanceCounter(&kickingStart);
					prints(L"Kicking\n");
				}
				else {
					int sign;
					if (stateGoal == goalOnRight) {
						sign = -1;
					}
					else {
						sign = 1;
					}
					if (goals.count == 0) { //no goals in sight, turn faster
						rotateAroundFront(sign * 200);
					}
					else{
						//around 120 degs/s is max speed so that the goal doesn't go out, less than 30 degs/s is pointless
						float angle = atanf(tanf(angleOfView)*(x - 320.0) / 320.0); //roughly, in degrees
						float speedMultiplier = 1 - expf(-fabs(pow((x - 320.0) / 320.0, 4)));
						speedMultiplier = speedMultiplier > 1 ? 1 : speedMultiplier;
						float turningSpeed = 25 + (160 - 25)*speedMultiplier;

						rotateAroundFront(sign * turningSpeed);
					}
				}
			}
			else {
				state = lookForBall;
				QueryPerformanceCounter(&startCounter);
				prints(L"Looking for ball\n");
			}
		}
		if (state == kickBall) {
			SetWindowText(stateStatusGUI, L"Kicking");
			if (!isBallInDribbler) {
				state = lookForBall;
				QueryPerformanceCounter(&startCounter);
			}
			if (!charged) {//not charged, wait more
				QueryPerformanceCounter(&kickingStop);
				if (time(kickingStart, kickingStop) > 4.0) {
					QueryPerformanceCounter(&kickingStart);
					charge();
				}
				continue;
			}
			kick();
			Sleep(50);
			prints(L"KICKED\n");
			charged = false;

			isBallInDribbler = false;
			sendString(hCOMDongle, "9:bl\n");

			ignoreBall = true;
			ignoreX = 20.0 / 100.0, ignoreY = 0;
			QueryPerformanceCounter(&ignoreStart);

			state = lookForBall;
			QueryPerformanceCounter(&startCounter);
			prints(L"Looking for ball\n");

			Sleep(50);
			charge();
		}

		//if (state == driveToGoal) { //before we had the coilgun
		//	//prints(L"Driving to goal\n");
		//	SetWindowText(stateStatusGUI, L"driving to goal");
		//	if (checkIsBallInDribbler(currentx, currenty)) {
		//		if (goals.count > 0) {
		//			if (isLineInFront()) {
		//				prints(L"Line in front\n");
		//				QueryPerformanceCounter(&startCounter);
		//				state = rotate180;
		//				currentDrivingState.angularVelocity = -90, currentDrivingState.speed = 0;
		//				setSpeedAngle(currentDrivingState);
		//				movingTime = 1.5;
		//			}
		//			else {
		//				int x = 0, y = 0;
		//				findLargestObject(x, y, goals);
		//				rotateAroundFrontAndMoveForward(-60 * tanhf(float(x - 320) / 130) * fabs(tanhf(float(x - 320) / 130))*2, 0.1*2);
		//			}
		//		}
		//		else {
		//			state = lookForGoal;
		//		}
		//	}
		//	else{
		//		state = lookForBall;
		//	}
		//}
		//if (state == rotate180) {
		//	//prints(L"Rotating 180\n");
		//	SetWindowText(stateStatusGUI, L"rotating 180");
		//	timeOut = 0;
		//	QueryPerformanceCounter(&stopCounter);
		//	if ((double)(stopCounter.QuadPart - startCounter.QuadPart)/((double)timerFrequency.QuadPart) > movingTime) {
		//		timeOut = 50;
		//		prints(L"turned around\n");
		//		state = lookForBall;
		//		prints(L"Looking for ball\n");
		//	}
		//}
	}
}

bool isLineInFront() {
	for (int i = 0; i < linesShare.count; ++i) {
		float floorX, floorY;
		float angle = linesShare.data[i].angle, radius = linesShare.data[i].radius;
		float y = fabs(angle) < 1.0/1000.0 ? 1000.0 : radius / sinf(angle);
		y += 240;
		convertToFloorCoordinates(0, y, floorX, floorY);
		if (floorX < 22.0 / 100.0) { //if there is a line closer than 22cm ahead
			return TRUE;
		}
		convertToFloorCoordinates(radius*cosf(angle)+320, radius*sinf(angle)+240, floorX, floorY);
		if (floorX*floorX + floorY*floorY < 5.0 / 100.0*5.0 / 100.0) { //if there is a line closer than 5 cm
			return TRUE;
		}
	}
	return FALSE;
}

//bool isLineBetweenRobotAndBall(int currentx, int currenty) { //check if there is a line between the robot and the ball
//	int ballx = currentx - 320, bally = currenty - 240;
//	for (int i = 0; i < linesShare.count; ++i) {
//		float floorX, floorY;
//		float angle = linesShare.data[i].angle, radius = linesShare.data[i].radius;
//		float lineXOnLowestLine = fabs(angle - PI / 2) < 1.0 / 1000.0 ? 1000.0 : (radius - (-320)*sinf(angle)) / cosf(angle); //x coordinate of the first line on the screen
//		float lineXOnBallLine = fabs(angle - PI / 2) < 1.0 / 1000.0 ? 1000.0 : (radius - (bally)*sinf(angle)) / cosf(angle); //x coordinate of the line with the ball center on it
//		if ((lineXOnBallLine - ballx)*(lineXOnLowestLine) < 0) {
//			prints(L"Line between ball and line currentx: %d, currenty: %d\n", currentx, currenty);
//			return TRUE;
//		}
//	}
//	return FALSE;
//}

//bool checkIsBallInDribbler() { //not used
//	sendString(hCOMDongle, "9:bl\n");
//	char buffer[32] = {};
//	DWORD bytesRead = 0;
//	receiveString(hCOMDongle, buffer, bytesRead);
//	if (lstrcmpA(buffer, "<9:bl:1>\n") == 0) {
//		return TRUE;
//	}
//	else {
//		return FALSE;
//	}
//	//return FALSE;
//	//sendString(hCOMDongle, "9:bl\n");
//	//char buffer[32] = {};
//	//DWORD bytesRead;
//	//receiveString(hCOMDongle, buffer, bytesRead);
//	//if (lstrcmpA(buffer, "9:bl1")) {
//	//	return TRUE;
//	//}
//	//else {
//	//	return FALSE;
//	//}
//	//float floorX, floorY;
//
//	//if (ballsShare.count > 0) {
//	//	for (int i = 0; i < ballsShare.count; ++i) {
//	//		convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].y, floorX, floorY);
//	//		if (floorX < 21.0 / 100.0 && fabs(floorY) < 8.0 / 100.0) {
//	//			return TRUE;
//	//		}
//	//	}
//	//}
//	//return FALSE;
//}

void kick(int microSeconds) { //kick for a certain amount of microseconds
	char buffer[16] = {};
	sprintf_s(buffer, "9:k\n", microSeconds);
	sendString(hCOMDongle, buffer);
}

void kick() { //kick for the default duration
	sendString(hCOMDongle, "9:k\n");
}

void charge() {
	charged = false;
	sendString(hCOMDongle, "9:c\n");
	prints(L"Charge sent\n");
}

//void dribblerON() {
//	sendString(hCOMDongle, "9:dm255\n");
//}
//
//void dribblerOFF() {
//	sendString(hCOMDongle, "9:dm0\n");
//}

void discharge() {
	sendString(hCOMDongle, "9:dc\n");
}

void rotateAroundFrontAndMoveForward(float angularVelocity, float speed, float angle) {
	currentDrivingState.speed = -angularVelocity * PI / 180 * baseRadius;
	currentDrivingState.angle = 90, currentDrivingState.angularVelocity = angularVelocity;
	currentDrivingState.vx = speed * cosf(angle * PI / 180), currentDrivingState.vy = speed * sinf(angle * PI / 180);
	setSpeedBoth(currentDrivingState);
}

void rotateAroundFront(float angularVelocity) {
	QueryPerformanceCounter(&lastSpeedSentStop);
	float speed = -angularVelocity * PI / 180 * baseRadius;
	if (currentDrivingState.speed != speed || currentDrivingState.angle != 90 || currentDrivingState.angularVelocity != angularVelocity ||
		currentDrivingState.vx != 0 || currentDrivingState.vy != 0 || time(lastSpeedSent, lastSpeedSentStop) > 0.5) {
		currentDrivingState.speed = -angularVelocity * PI / 180 * baseRadius;
		currentDrivingState.angle = 90, currentDrivingState.angularVelocity = angularVelocity;
		currentDrivingState.vx = 0, currentDrivingState.vy = 0;
		setSpeedAngle(currentDrivingState);
		QueryPerformanceCounter(&lastSpeedSent);
	}
}

//start driving to the position with coordinates in the floor system, try to rotate by an angle before arriving
void driveToFloorXY(float floorX, float floorY) { //0.6 m/s is around the speed at which we can still catch the ball, speed should go low starting at around 0.8m
	//around 0.2m is the distance when the ball is very near the dribbler
	floorX = floorX < 1 / 100 ? 1 / 100 : floorX; //use distance from the dribbler
	floorY = fabs(floorY) < 1 / 300 ? 0.0 : floorY;
	float dist = powf(floorX*floorX + floorY*floorY, 0.5); //distance to the ball
	float speedMultiplier = 1 - expf(-fabs(pow((dist - 0.2) / 0.8, 6))); //pow(fabs(tanhf((dist - 0.2) / 1.0)) / tanhf(1), 2); //use square root of the distance for modulating speed
	speedMultiplier = speedMultiplier > 1 ? 1 : speedMultiplier;
	float speedBase = 1.5; //max speed
	float finalSpeed = 0.5;
	float speed = finalSpeed + (speedBase - finalSpeed) * speedMultiplier;
	float time = dist > 0.2 ? (dist-0.15) / speed : dist / speed; //time to reach the ball
	float angleToBall = atanf(floorY / floorX) / PI * 180.0;
	float angularVelocity = angleToBall / time * 2; //turn fast enough so that we have turned by the angle to the ball by half the distance
	float angle = angleToBall / 2;

	//if (dist < 0.4) {
	//	angularVelocity = fabs(angularVelocity) > 100 ? 100 * (angularVelocity > 0 ? 1 : -1) : angularVelocity;
	//}
	if (dist < 0.25 && angleToBall > 25) {
		speed = 0.25;
		angularVelocity = angleToBall * 3;
	}

	QueryPerformanceCounter(&lastSpeedSentStop);

	//old driving, where the distances from the center of the robot were used, now using distances to the dribbler
	prints(L"Angle to ball: %.2f, angular velocity: %.2f, speed: %.2f, dist: %.2f, floorX: %.2f, floorY: %.2f\n", angleToBall, angularVelocity, speed, dist, floorX, floorY);
	currentDrivingState.angularVelocity = angularVelocity;
	currentDrivingState.speed = speed;
	currentDrivingState.angle = angle;
	setSpeedAngle(currentDrivingState);

	//rotateAroundFrontAndMoveForward( angularVelocity, speed, angleToBall / 4);
	//prints(L"angv: %.2f, speed: %.2f, angle: %.2f, x: %.2f, y: %.2f\n", angularVelocity, speed, angleToBall, floorX, floorY);

	QueryPerformanceCounter(&lastSpeedSent);
}


//converts pixel coordidnates to floor coordinates with respect to camera, uses the camera height, angle of view and camera angle
void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY) {
	//x, y, z are the coordinates on the floor, x axis straight ahead, y axis to the left
	vector eCameraNormal = { cosf(cameraAngle), 0, -sinf(cameraAngle) };	//basis vector in the direction the camera is pointing
	vector ex = { 0, -1 * tanf(angleOfView / 2) / 320, 0 };		//the vector in floor coordinates per pixel along the x direction of the image from the center of the image considering camera normal vector is length 1
	vector ey = { sinf(cameraAngle) * tanf(angleOfView / 2) / 320, 0, cosf(cameraAngle) * tanf(angleOfView / 2) / 320 };	//same in the y direction

	vector ball = { (currenty - 240) * ey.e1 + eCameraNormal.e1, 
					(currentx - 320) * ex.e2 + eCameraNormal.e2, 
					(currenty - 240) * ey.e3 + eCameraNormal.e3 };
	ball.e3 = fabs(ball.e3) < 1 / 10000 ? 1 / 10000 : fabs(ball.e3);

	floorX = ball.e1 * cameraHeight / ball.e3, floorY = ball.e2 * cameraHeight / ball.e3;
}

//rotates around the center with the given angular velocity in degs/s
void rotateAroundCenter(float angularVelocity) {
	QueryPerformanceCounter(&lastSpeedSentStop);
	if (currentDrivingState.angle != 0 || currentDrivingState.speed != 0 || currentDrivingState.angularVelocity != angularVelocity ||
		currentDrivingState.vx != 0 || currentDrivingState.vy != 0 || time(lastSpeedSent, lastSpeedSentStop) > 0.5) {
		currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.angularVelocity = angularVelocity;
		currentDrivingState.vx = 0, currentDrivingState.vy = 0;
		setSpeedAngle(currentDrivingState);
		QueryPerformanceCounter(&lastSpeedSent);
	}
}

//finds an object nearest to the old image x, y values
void findNearestObjectToOldObject(int& currentx, int& currenty, objectCollection& objects) {
	int minDist = (currentx - objects.data[0].x) * (currentx - objects.data[0].x) + (currenty - objects.data[0].y) * (currenty - objects.data[0].y), minIndex = 0;
	for (int i = 0; i < objects.count; ++i) {
		int dist = (currentx - objects.data[i].x) * (currentx - objects.data[i].x) + (currenty - objects.data[i].y) * (currenty - objects.data[i].y);
		if (dist < minDist && currentx > 2 && currentx <638) {
			minDist = dist;
			minIndex = i;
		}
	}
	currentx = objects.data[minIndex].x, currenty = objects.data[minIndex].y;
}

//finds an object nearest to the old floor x, y values
void findNearestFloorObjectToOldObject(float& floorX, float& floorY, objectCollection& objects) {
	if (objects.count == 0)
		return;
	float floorX2 = 0, floorY2 = 0;
	float minDist = 0;
	int minIndex = 0;
	for (int i = 0; i < objects.count; ++i) {
		convertToFloorCoordinates(objects.data[i].x, objects.data[i].y, floorX2, floorY2);
		float dist = (floorX - floorX2) * (floorX - floorX2) + (floorY - floorY2) * (floorY - floorY2), minIndex = 0;
		if (minDist == 0 || dist < minDist) {
			minDist = dist;
			minIndex = i;
		}
	}
	convertToFloorCoordinates(objects.data[minIndex].x, objects.data[minIndex].y, floorX, floorY);
}


//finds the nearest ball to the robot
int findNearestFloorBall(float& nearestFloorX, float& nearestFloorY, int& currentx, int& currenty) {
	int ballCount = 0;
	float nearestX = 0, nearestY = 0;
	for (int i = 0; i < ballsShare.count; ++i) {
		float floorX, floorY;
		convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].y, floorX, floorY);
		if (!ballsShare.data[i].isObjectAcrossLine && !(ignoreBall && nearestX == ignoreX && nearestY == ignoreY)) {
			ballCount++;
			if ((nearestX == 0 || floorX*floorX + floorY*floorY < nearestX*nearestX + nearestY*nearestY)) {
				nearestX = floorX, nearestY = floorY;
				currentx = ballsShare.data[i].x, currenty = ballsShare.data[i].y;
			}
		}
	}
	nearestFloorX = nearestX, nearestFloorY = nearestY;
	return ballCount;
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

float time(LARGE_INTEGER start, LARGE_INTEGER stop) {
	return (float)(double(stop.QuadPart - start.QuadPart) / double(timerFrequency.QuadPart));
}

void handleMainBoardCommunication() {
	char buffer[32] = {};
	DWORD bytesRead = 0;
	DWORD bytesReadTotal = 0;
	while (true) {
		readCOM(hCOMDongle, buffer+bytesReadTotal, 1, bytesRead);
		if (bytesRead == 0) {
			if (bytesReadTotal != 0) {
				prints(L"read %d bytes: %S\n", bytesReadTotal, buffer);
			}
			break;
		}
		else {
			++bytesReadTotal;
			if (buffer[bytesReadTotal - 1] == '\n') {
				buffer[bytesReadTotal] = 0;
				prints(L"read command %d bytes: %S\n", bytesReadTotal, buffer);
				if (lstrcmpA(buffer, "<9:bl:1>\n") == 0) {
					isBallInDribbler = true;
					SetWindowText(statusBall, L"Ball 1");
				}
				else if (lstrcmpA(buffer, "<9:bl:0>\n") == 0) {
					isBallInDribbler = false;
					SetWindowText(statusBall, L"Ball 0");
				}
				else if (lstrcmpA(buffer, "<9:c:0>\n") == 0) {
					charged = true;
				}
				else if (lstrcmpA(buffer, "<9:cf>\n") == 0) {
					charged = true;
					prints(L"CHARGE TIMEOUT\n");
				}
				bytesReadTotal = 0;
			}
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
				prints(L"Checking command: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));
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
			prints(L"Checking command: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));
			if (bytesRead < 12) {
				return;
			}
			length = bytesRead;
		}
		if (start > 40) {
			CopyMemory(readBuffer, readBuffer + start, length);
			ZeroMemory(readBuffer + start, 128 - start);
			start = 0;
		}
	}
}


//initializes the dongle COM port
void initCOMPort() {
	DCB dcb = {};
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = 15;
	timeouts.ReadTotalTimeoutConstant = 15;
	timeouts.ReadTotalTimeoutMultiplier = 15;
	timeouts.WriteTotalTimeoutConstant = 60;
	timeouts.WriteTotalTimeoutMultiplier = 25;


	osReaderDongle.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osWriteDongle.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osStatusDongle.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	hCOMDongle = CreateFile(COMPORTDONGLE, GENERIC_READ | GENERIC_WRITE, 0, NULL, //COM3 on the NUC
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
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

	dcb.BaudRate = 19200;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.EvtChar = '\n';

	if (!SetCommState(hCOMDongle, &dcb))
	{
		prints(L"Dongle SetCommState failed with error %X.\n", GetLastError());
	}
	if (!SetCommTimeouts(hCOMDongle, &timeouts)) {
		prints(L"Dongle SetCommTimeouts failed with error %d.\n", GetLastError());
	}
	if (!SetCommMask(hCOMDongle, EV_RXCHAR)) {
		prints(L"SetCommMask failed with error %d.\n", GetLastError());
	}

	if (WaitCommEvent(hCOMDongle, &dwCommEventDongle, &osStatusDongle)) {
		prints(L"Main board wait Com event %X\n", dwCommEvent);
	}
	else {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"WaitCommEvent failed with error %d\n", GetLastError());
		}
	}
	prints(L"Dongle COM init end\r\n\r\n");
}

boolean validCommand(char* readBuffer) {
	char tempBuffer[32] = {};
	CopyMemory(tempBuffer, readBuffer, 12);
	if (lstrcmpA(tempBuffer + 3, "STOP-----") != 0 && lstrcmpA(tempBuffer + 3, "START----") != 0) {
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
	//WriteFile(hComm, outputBuffer, len, &bytesWritten, NULL);

	if (!WriteFile(hCOMDongle, outputBuffer, len, &bytesWritten, &osWrite)) {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"Main board sendString failed with error %X\n", GetLastError());
		}
		else if (!GetOverlappedResult(hCOMDongle, &osWrite, &bytesWritten, TRUE)) {
			prints(L"Main board sendString failed with error %X\n", GetLastError());
		}
	}
	if (bytesWritten != len) {
		prints(L"Main board sendString timeout: %S\n", outputBuffer);
	}
}

//recieves the string upto the new line character and adds a zero character
int receiveString(HANDLE hComm, char* outputBuffer, DWORD bytesRead) {
	int i = 0;
	while (outputBuffer[i] != '\n' && outputBuffer[i] != '\r') {
		ReadFile(hComm, outputBuffer, 1, &bytesRead, NULL);
		++i;
	}
	bytesRead = i;
	outputBuffer[i + 1] = '\0';
	return i;
}

//sets the speed of a specific wheel to a value in the plate speed units
void setEngineRotation(int id, int speed) {
	if (speed > 190) {
		prints(L"Wheel speed overflow.");
		speed = 190;
	}
	char writeBuffer[32] = {};
	DWORD bytesWritten;
	int len = sprintf_s(writeBuffer, "%d:sd%d\n", id, speed);
	sendString(hCOMDongle, writeBuffer);
	//WriteFile(hCOMDongle, writeBuffer, len, &bytesWritten, NULL);
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
	state.vx = 0, state.vy = 0;
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

	setEngineRotation(1, wheel1Speed);
	setEngineRotation(2, wheel2Speed);
	setEngineRotation(3, wheel3Speed);
	setEngineRotation(4, wheel4Speed);
}

void setSpeedXY(drivingState state) {
	state.angle = 0, state.speed = 0;
	setSpeedXY(state.vx, state.vy, state.angularVelocity);
}

//calculates the direction of the robot modulo 90 degrees. For this it determines the orientation with respect to the field lines.
float calculateDirection(lineInfo line) {
	int r = line.radius;
	float angle = line.angle;
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
	timeouts.ReadIntervalTimeout = 40; //ms, max time before arrival of next byte
	timeouts.ReadTotalTimeoutConstant = 15; //added to multiplier*bytes
	timeouts.ReadTotalTimeoutMultiplier = 10; //ms times bytes
	timeouts.WriteTotalTimeoutConstant = 15;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	osReader.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	osStatus.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	DCB dcb;
	hCOMRadio = CreateFile(COMPORTRADIO, GENERIC_READ | GENERIC_WRITE, 0, 0, //COM4 on the NUC
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	if (hCOMRadio == INVALID_HANDLE_VALUE) {
		prints(L"ERROR OPENING USB COM PORT\N");
	}
	else {
		prints(L"USB radio COM port opened\n");
	}
	ZeroMemory(&dcb, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);

	if (!GetCommState(hCOMRadio, &dcb))
	{
		prints(L"Radio GetCommState failed with error %d.\n", GetLastError());
	}
	//prints(L"Baudrate %d abort on error %d\n", dcb.BaudRate, dcb.fAbortOnError);

	dcb.BaudRate = 9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.EvtChar = 'a';

	if (!SetCommState(hCOMRadio, &dcb))
	{
		prints(L"Radio SetCommState failed with error %d.\n", GetLastError());
	}

	if (!SetCommTimeouts(hCOMRadio, &timeouts)) {
		prints(L"Radio SetCommTimeouts failed with error %d.\n", GetLastError());
	}

	if (!SetCommMask(hCOMRadio, EV_RXFLAG)) {
		prints(L"Radio SetCommMask failed with error %d.\n", GetLastError());
	}

	if (WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus)) {
		prints(L"Radio wait Com event %X\n", dwCommEvent);
	}
	else {
		if (GetLastError() != ERROR_IO_PENDING) {
			prints(L"WaitCommEvent failed with error %d\n", GetLastError());
		}
	}
	prints(L"USB radio init end\n");
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
