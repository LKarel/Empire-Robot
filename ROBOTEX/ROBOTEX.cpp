#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

#define max3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define min3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define PI (3.1415927)
#define SQRT2 (1.4142135)
#define COMPORTRADIO L"\\\\.\\COM4" //4 on NUC
#define COMPORTDONGLE L"COM3" //3 on NUC
#define BALLRADIUS (1.5 / 100.0)
#define GOALMIDHEIGHT (5.0 / 100.0)
#define FIELDGREENCOUNTTHRESHOLD 25000

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

Timer FPS2Timer, movingTimer, lastSpeedSentTimer, lastBallFoundTimer;
LARGE_INTEGER Timer::timerFrequency;

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
extern HWND infoGUI3;
extern HWND statusFPS2;
extern int fieldGreenPixelCountShare;
extern bool isLineStraightAhead;
wchar_t buffer[128] = {}; //used for formatting text

void sendString(HANDLE hComm, char* outputBuffer);
int receiveString(HANDLE hComm, char* outputBuffer, DWORD bytesRead);
void initCOMPort();
void initUSBRadio();
void setSpeedAngle(float speed, float angle, float angularVelocity);
void setSpeedAngle(drivingState state);
void setSpeedXY(float vx, float vy, float angularVelocity);
void setSpeedXY(drivingState state);
void setSpeedBoth(drivingState state);
void play();
void listenToCommands();
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
void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY, float heightFromFloor);
void driveToFloorXY(float floorX, float floorY);
void driveToFloorXYPID(float floorX, float floorY);
void rotateAroundFront(float angularVelocity);
void rotateAroundFrontAndMoveForward(float angularVelocity, float speed, float angle);
void dribblerON();
void dribblerOFF();
void charge();
void discharge();
void kick(int microSeconds);
void kick();
void handleMainBoardCommunication();
//bool isLineBetweenRobotAndBall(int currentx, int currenty);
int ballsOnFieldInSight();
void driveForward(float speed);

enum State { driveToNearestBall, lookForBall, lookForNearBallIgnoreStraight, lookForGoal, driveToBall, kickBall, rotate90,
			 nextTurnLeft, nextTurnRight, goalOnLeft, goalOnRight, ballOnRight, ballOnLeft, driveToOtherGoal, wanderForBall} state, state2, stateGoal, stateBallSeen;

bool attackBlue;

struct vector {
	float e1;
	float e2;
	float e3;
};

int main() {
	QueryPerformanceFrequency(&(Timer::timerFrequency)); //set up the timer frequency
	//Create the GUI in a separate thread
	GUIThread = CreateThread(NULL, 0, GUICamera, 0, 0, NULL);
	//Wait for the GUI to initialize
	WaitForSingleObject(readySignal, INFINITE);
	prints(L"Testing printing\r\n\r\n");

	//initialize the state and start testing the robot
	state = lookForBall;
	listenToCommands();

	//Don't exit this thread before the GUI
	WaitForSingleObject(GUIThread, INFINITE);
}

void listenToCommands() {
	initCOMPort();
	initUSBRadio();
	//sendString(hCOMDongle, "9:fs0\n");
	lastSpeedSentTimer.start();
	HANDLE waitHandles[3] = { osStatus.hEvent, startSignal, osStatusDongle.hEvent };

	stateGoal = goalOnRight;
	stateBallSeen = ballOnRight;
	while (true) {
		float floorX, floorY;
		int currentx, currenty;
		findNearestFloorBall(floorX, floorY, currentx, currenty);
		float angle = atanf(floorY / floorX) * 180.0 / PI;

		swprintf_s(buffer, L"NX %.2f \n NY %.2f\n ang %.2f", floorX, floorY, angle);
		SetWindowText(infoGUI, buffer);

		//swprintf_s(buffer, L"greencount %d", fieldGreenPixelCountShare);
		//SetWindowText(infoGUI2, buffer);

		if (hCOMDongle != INVALID_HANDLE_VALUE) {
			if (lastSpeedSentTimer.time() > 0.5) { //if we send the speeds too often we get communication timeouts
				setSpeedBoth(currentDrivingState);
				lastSpeedSentTimer.start();
			}
		}
		switch (WaitForMultipleObjects(3, waitHandles, FALSE, 250)) {
		case WAIT_OBJECT_0:
			prints(L"radio event %X\n", dwCommEvent);
			receiveCommand();
			WaitCommEvent(hCOMRadio, &dwCommEvent, &osStatus);
			break;
		case WAIT_OBJECT_0 + 1:
			prints(L"Start signal arrived.\n");
			SetWindowText(stateStatusGUI, L"started");
			dribblerON();
			play();
			dribblerOFF();
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

void play() {
	HANDLE waitHandles[5] = { osStatus.hEvent, stopSignal, osStatusDongle.hEvent, calibratingSignal, newImageAnalyzed };
	int currentx = 320, currenty = 0, FPS2Count = 0;
	int timeOut = 30;
	float movingTime; //timeout after sending each command
	objectCollection goals, goals2;
	float nearestBallFloorX = 0, nearestBallFloorY = 0;
	ignoreX = 0, ignoreY = 0;
	Timer ignoreTimer, ballSearchRotationSpeedTimer, chargingTimer, wanderingTimer;
	bool rotateFast = true;
	FPS2Timer.start();
	FPS2Count = 0;

	bool turnFast = true;
	state = driveToBall;
	SetWindowText(goalGuessState, L"Goal on right");
	isBallInDribbler = false;
	prints(L"Started, driving to ball\n");
	SetWindowText(stateStatusGUI, L"Driving to ball");
	charged = false, ignoreBall = false;
	charge();
	chargingTimer.start();
	sendString(hCOMDongle, "9:bl\n");
	lastBallFoundTimer.start();

	while (true) {

		//display test stuff
		float floorX, floorY;
		int currentx, currenty;
		findNearestFloorBall(floorX, floorY, currentx, currenty);
		float angle = atanf(floorY / floorX) * 180.0 / PI;

		swprintf_s(buffer, L"NX %.2f \n NY %.2f\n ang %.2f", floorX, floorY, angle);
		SetWindowText(infoGUI, buffer);
		//end display test stuff

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
			rotateAroundCenter(0);
			//WaitForSingleObject(calibratingEndSignal, INFINITE);
			continue;
		case WAIT_OBJECT_0 + 4: //new image analyzed
			//update FPS rate after every 5 images
			++FPS2Count;
			if (FPS2Count % 5 == 0) {
				float FPS = 5.0 / FPS2Timer.time();
				swprintf_s(buffer, L"FPS2: %.2f", FPS);
				SetWindowText(statusFPS2, buffer);
				FPS2Timer.start();
			}
			break;
		default:
			continue;
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
			if (ignoreTimer.time() > 1.0) {
				ignoreBall = false;
				ignoreX = 0, ignoreY = 0;
			}
			else {
				float ignoreXNew = ignoreX, ignoreYNew = ignoreY;
				findNearestFloorObjectToOldObject(ignoreXNew, ignoreYNew, ballsShare);
				//if we cant see the ball behind the dribbler, don't start ignoring a ball too far away
				ignoreX = ignoreXNew;
				ignoreY = ignoreYNew;

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
			if (stateGoalNew != stateGoal && state != lookForGoal) {
				stateGoal = stateGoalNew;
				if (stateGoal == goalOnRight) {
					SetWindowText(goalGuessState, L"Goal on the right");
				}
				else {
					SetWindowText(goalGuessState, L"Goal on the left");
				}
			}
		}

		if (ballsOnFieldInSight() > 0) {
			if(ballsShare.data[0].x > 320){
				stateBallSeen = ballOnRight;
			}
			else {
				stateBallSeen = ballOnLeft;
			}
		}
		if (ballsOnFieldInSight() > 0 || isBallInDribbler || state == kickBall || state == wanderForBall) {
			lastBallFoundTimer.start();
		}
		else if (lastBallFoundTimer.time() > 3.1) {
			prints(L"Wandering for ball\n");
			state = wanderForBall;
			wanderingTimer.start();
			movingTimer.start();
		}

		if (fieldGreenPixelCountShare < FIELDGREENCOUNTTHRESHOLD && state != rotate90 && state != wanderForBall) {
			state = rotate90;
			movingTimer.start();
		}

		if (state == lookForBall) {
			if (isBallInDribbler) {
				prints(L"Looking for goal");
				state = lookForGoal;
				movingTimer.start();
			}
			else {
				//prints(L"Looking for ball\n");
				SetWindowText(stateStatusGUI, L"Looking for ball");
				nearestBallFloorX = 0;
				int sign = stateBallSeen == ballOnRight ? -1 : 1;
				if (ballsOnFieldInSight() == 0) {
					if (fmodf(movingTimer.time(), 0.3) < 0.2) {
						rotateAroundCenter(sign * 200);
					}
					else {
						rotateAroundCenter(sign * 40);
					}
					if (movingTimer.time() > 3.0) {
						prints(L"Wandering for ball\n");
						state = wanderForBall;
						wanderingTimer.start();
						movingTimer.start();
					}
				}
				else {
					findNearestFloorBall(nearestBallFloorX, nearestBallFloorY, currentx, currenty);
					prints(L"found nearest ball X: %.2f, Y: %.2f, currentx: %d, currenty: %d\n", nearestBallFloorX, nearestBallFloorY, currentx, currenty);
					prints(L"Driving to ball\n");
					state = driveToBall;
					rotateAroundCenter(0);
					movingTimer.start();
				}
			}
		}
		if (state == driveToBall) {
			if (isBallInDribbler) {
				rotateAroundCenter(0);
				Sleep(150);
				prints(L"Looking for goal");
				state = lookForGoal;
				movingTimer.start();
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
					movingTimer.start();
					prints(L"Looking for ball\n");
					continue;
				}
				driveToFloorXYPID(floorX, floorY);
			}
		}
		if (state == lookForGoal) {
			//prints(L"Looking for goal\n");
			SetWindowText(stateStatusGUI, L"Looking for goal");
			if (isBallInDribbler) {
				int x = 0, y = 0;
				float floorX, floorY;
				findLargestObject(x, y, goals);
				convertToFloorCoordinates(x, y, floorX, floorY, GOALMIDHEIGHT);
				//prints(L"Goal x: %d\n", x);
				if (!(goalsBlueShare.count == 1 && goalsYellowShare.count == 1) &&
					(y > 0 && floorX > 0 && fabs(floorY) < 18.0 / 100.0 || abs(x-320) <= 35)) {
					state = kickBall;
					rotateAroundFront(0);
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
						if (movingTimer.time() > 3.0) { //haven't found the goal in a while, drive towards the other goal
							prints(L"Wandering");
							state = wanderForBall;
							wanderingTimer.start();
							movingTimer.start();
						}
						else {
							rotateAroundFront(sign * 200);
						}
					}
					else{
						//around 120 degs/s is max speed so that the goal doesn't go out, less than 30 degs/s is pointless
						float angle = atanf(tanf(angleOfView)*(x - 320.0) / 320.0); //roughly, in degrees
						float speedMultiplier = 1 - expf(-fabs(pow((x - 320.0) / 320.0, 4)));
						speedMultiplier = speedMultiplier > 1 ? 1 : speedMultiplier;
						float turningSpeed = 50 + (160 - 50)*speedMultiplier;

						rotateAroundFront(sign * turningSpeed);
					}
				}
			}
			else {
				state = lookForBall;
				movingTimer.start();
				prints(L"Looking for ball\n");
			}
		}
		if (state == kickBall) {
			SetWindowText(stateStatusGUI, L"Kicking");
			if (!isBallInDribbler) {
				state = lookForBall;
				movingTimer.start();
			}
			if (!charged) {//not charged, wait more
				if (chargingTimer.time() > 3.5) { //by this time we definitely should have gotten a charge command
					prints(L"No charge info received in time\n");
					chargingTimer.start();
					charge();
				}
				continue;
			}
			kick();
			Sleep(80);
			prints(L"KICKED\n");
			charged = false;

			isBallInDribbler = false;
			sendString(hCOMDongle, "9:bl\n");

			ignoreBall = true;
			ignoreX = 20.0 / 100.0, ignoreY = 0;
			ignoreTimer.start();

			state = lookForBall;
			movingTimer.start();
			prints(L"Looking for ball\n");

			Sleep(80);
			charge();
			chargingTimer.start();
		}

		if (state == rotate90) {
			SetWindowText(stateStatusGUI, L"Rotating 90");
			if (movingTimer.time() < 0.5) {
				rotateAroundCenter(180);
				continue;
			}
			else {
				prints(L"Looking for ball\n");
				state = lookForBall;
				movingTimer.start();
			}
		}

		//wander around, try to drive towards a goal far enough away, if can't find one, drive to a direction where there are no goals, lines and there is enough green
		if (state == wanderForBall) {
			SetWindowText(stateStatusGUI, L"Wandering");
			if (movingTimer.time() > 4.0) { //if we didn't find a far away goal in 4 seconds
				if (goalsBlueShare.count != 0 || goalsYellowShare.count != 0 || isLineStraightAhead || fieldGreenPixelCountShare < FIELDGREENCOUNTTHRESHOLD) {
					rotateAroundCenter(120);
					wanderingTimer.start();
				}
				else {
					if (wanderingTimer.time() > 1.0) {
						movingTimer.start();
						state = lookForBall;
						prints(L"Looking for ball\n");
						continue;
					}
					driveForward(0.5);
				}
			}
			else {
				if (goalsBlueShare.count == 0 && goalsYellowShare.count == 0 || isLineStraightAhead || fieldGreenPixelCountShare < FIELDGREENCOUNTTHRESHOLD) {
					rotateAroundCenter(120);
					wanderingTimer.start();
				}
				else {
					movingTimer.start();
					if (wanderingTimer.time() > 1.0) {
						movingTimer.start();
						state = lookForBall;
						prints(L"Looking for ball\n");
						continue;
					}
					int goalX = 0, goalY = 0;
					findLargestObject(goalX, goalY, goalsBlueShare);
					if (goalY > 450) {
						driveForward(0.5);
						continue;
					}
					else {
						goalX = 0, goalY = 0;
						findLargestObject(goalX, goalY, goalsYellowShare);
						if (goalY > 450) {
							driveForward(0.5);
						}
						else {
							rotateAroundCenter(80);
							wanderingTimer.start();
						}
					}
				}
			}
		}

	}
}

bool isLineInFront() {
	for (int i = 0; i < linesShare.count; ++i) {
		float floorX, floorY;
		float angle = linesShare.data[i].angle, radius = linesShare.data[i].radius;
		float y = fabs(angle) < 1.0/1000.0 ? 1000.0 : radius / sinf(angle);
		y += 240;
		convertToFloorCoordinates(0, y, floorX, floorY, 0);
		if (floorX < 22.0 / 100.0) { //if there is a line closer than 22cm ahead
			return TRUE;
		}
		convertToFloorCoordinates(radius*cosf(angle)+320, radius*sinf(angle)+240, floorX, floorY, 0);
		if (floorX*floorX + floorY*floorY < 5.0 / 100.0*5.0 / 100.0) { //if there is a line closer than 5 cm
			return TRUE;
		}
	}
	return FALSE;
}

int ballsOnFieldInSight() {
	int ballCount = 0;
	for (int i = 0; i < ballsShare.count; ++i) {
		if (!ballsShare.data[i].isObjectAcrossLine) {
			ballCount++;
		}
	}

	return ballCount;
}

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

void dribblerON() {
	sendString(hCOMDongle, "9:dm255\n");
}

void dribblerOFF() {
	sendString(hCOMDongle, "9:dm0\n");
}

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
	float speed = -angularVelocity * PI / 180 * baseRadius;
	if (currentDrivingState.speed != speed || currentDrivingState.angle != 90 || currentDrivingState.angularVelocity != angularVelocity ||
		currentDrivingState.vx != 0 || currentDrivingState.vy != 0 || lastSpeedSentTimer.time() > 0.5) {
		currentDrivingState.speed = -angularVelocity * PI / 180 * baseRadius;
		currentDrivingState.angle = 90, currentDrivingState.angularVelocity = angularVelocity;
		currentDrivingState.vx = 0, currentDrivingState.vy = 0;
		setSpeedAngle(currentDrivingState);
		lastSpeedSentTimer.start();
	}
}

void driveForward(float speed) {
	currentDrivingState.angle = 0, currentDrivingState.angularVelocity = 0, currentDrivingState.speed = speed;
	currentDrivingState.vx = 0, currentDrivingState.vy = 0;
	setSpeedAngle(currentDrivingState);
	lastSpeedSentTimer.start();
}

Timer turningPIDTimer;

void turnToGoalPID(objectCollection goals) {
	if (goals.count == 0) {
		int sign = -1;
		turningPIDTimer.start();
		rotateAroundFront(sign * 200);
	}
	else {
		int x = 0, y = 0;
		float goalX = 0, goalY = 0;
		findLargestObject(x, y, goals);
		convertToFloorCoordinates(x, y, goalX, goalY, GOALMIDHEIGHT);
		goalX = goalX < 0.01 ? 0.01 : goalX; //for safety if the goal is somehow too close
		float angle = atanf(goalY / goalX);

		float angularVelocity = 3.5 * angle;

		float timeDuration = turningPIDTimer.time();
		turningPIDTimer.start();

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
	float finalSpeed = 0.4;
	float speed = finalSpeed + (speedBase - finalSpeed) * speedMultiplier;
	float angleToBall = atanf(floorY / floorX) / PI * 180.0;
	float angularVelocity = angleToBall * 3; //turn fast enough so that we have turned by the angle to the ball by half the distance
	float time = angleToBall / angularVelocity; //time to reach the ball
	float angle = angleToBall;

	if (angleToBall > 20 && dist < 0.4) {
		speed = 0.2;
	}

	//if (dist < 0.8 && angleToBall > 30) {
	//	angularVelocity = 3 * angleToBall;
	//	angularVelocity = angularVelocity > 90 ? 90 : angularVelocity;
	//	time = angleToBall / angularVelocity;
	//	speed = 0.5 * dist;
	//}

	//old driving, where the distances from the center of the robot were used, now using distances to the dribbler
	//prints(L"Angle to ball: %.2f, angular velocity: %.2f, speed: %.2f, dist: %.2f, floorX: %.2f, floorY: %.2f\n", angleToBall, angularVelocity, speed, dist, floorX, floorY);
	currentDrivingState.angularVelocity = angularVelocity;
	currentDrivingState.speed = speed;
	currentDrivingState.angle = angle;
	setSpeedAngle(currentDrivingState);

	//rotateAroundFrontAndMoveForward( angularVelocity, speed, angleToBall / 4);
	//prints(L"angv: %.2f, speed: %.2f, angle: %.2f, time: %.2f, dist: %.2f, x: %.2f, y: %.2f\n", angularVelocity, speed, angleToBall, time, dist, floorX, floorY);

	lastSpeedSentTimer.start();
}

//constants and data for the PID;
float integralSpeed;
float integralAngularVelocity;
float lastErrorSpeed;
float lastErrorAngularVelocity;
Timer drivingTimer;

float speedP = 0.5;
float speedI = 20.0;
float speedD = 0.001;
float angularVelocityP = 0.5;
float angularVelocityI = 20.0;
float angularVelocityD = 0.001;

void driveToFloorXYPID(float floorX, float floorY) {

	floorX = floorX < 1 / 100 ? 1 / 100 : floorX; //use distance from the dribbler
	floorY = fabs(floorY) < 1 / 300 ? 0.0 : floorY;
	float dist = powf(floorX*floorX + floorY*floorY, 0.5); //distance to the ball

	float speedBase = 1.6; //max speed
	float finalSpeed = 0.35;
	float speed = finalSpeed + (speedBase - finalSpeed) * tanhf((dist-0.15)/(speedBase - finalSpeed));
	float angleToBall = atanf(floorY / floorX) / PI * 180.0;
	float angularVelocity = angleToBall * 3.5; //turn fast enough so that we have turned by the angle to the ball by half the distance
	float angle = angleToBall;

	float timeDuration = drivingTimer.time();
	drivingTimer.start(); //restart the timer
	if (timeDuration < 1.0 / 150.0) {
		//prints(L"too fast driving state change\n");
	}

	if (currentDrivingState.speed == 0 && currentDrivingState.angularVelocity == 0) { //started driving to a new ball
		lastErrorSpeed = 0;
		lastErrorAngularVelocity = 0;
		integralSpeed = 0;
		integralAngularVelocity = 0;
		timeDuration = 0.01;
	}

	float errorSpeed = speed - currentDrivingState.speed;
	float errorAngularVelocity = angularVelocity - currentDrivingState.angularVelocity;
	float derivativeSpeed = (errorSpeed - lastErrorSpeed) / timeDuration;
	float derivativeAngularVelocity = (errorAngularVelocity - lastErrorAngularVelocity) / timeDuration;
	lastErrorSpeed = errorSpeed;
	lastErrorAngularVelocity = errorAngularVelocity;
	
	integralSpeed += errorSpeed*timeDuration;
	integralAngularVelocity += errorAngularVelocity*timeDuration;

	if (fabs(integralSpeed) > 0.1) {
		integralSpeed = integralSpeed > 0 ? 0.1 : -0.1;
	}
	if (fabs(integralAngularVelocity) > 20) {
		integralAngularVelocity = integralAngularVelocity > 0 ? 20 : -20;
	}

	currentDrivingState.speed = speedP*errorSpeed + speedI*integralSpeed + speedD*derivativeSpeed;
	currentDrivingState.angularVelocity = angularVelocityP*errorAngularVelocity + angularVelocityI*integralAngularVelocity + angularVelocityD*derivativeAngularVelocity;
	currentDrivingState.angle = angleToBall;
	setSpeedAngle(currentDrivingState);

	//prints(L"s: %.2f, av: %.2f, es: %.2f, is: %.2f, ds: %.2f, eav: %.2f, iav: %.2f, dav: %.2f, time: %.2f\n", speed, angularVelocity, errorSpeed, integralSpeed, derivativeSpeed, 
	//	errorAngularVelocity, integralAngularVelocity, derivativeAngularVelocity, timeDuration);
	//prints(L"cs: %.2f, cav: %.2f, ang: %.2f, sP: %.2f, sI: %.2f, sD: %.2f, avP: %.2f, avI: %.2f, avD: %.2f\n",
	//	currentDrivingState.speed, currentDrivingState.angularVelocity, currentDrivingState.angle, speedP, speedI, speedD,
	//	angularVelocityP, angularVelocityI, angularVelocityD);
}


//converts pixel coordidnates to floor coordinates with respect to camera, uses the camera height, angle of view and camera angle
void convertToFloorCoordinates(int currentx, int currenty, float& floorX, float& floorY, float heightFromFloor) {
	//x, y, z are the coordinates on the floor, x axis straight ahead, y axis to the left
	vector eCameraNormal = { cosf(cameraAngle), 0, -sinf(cameraAngle) };	//basis vector in the direction the camera is pointing
	vector ex = { 0, -1 * tanf(angleOfView / 2) / 320, 0 };		//the vector in floor coordinates per pixel along the x direction of the image from the center of the image considering camera normal vector is length 1
	vector ey = { sinf(cameraAngle) * tanf(angleOfView / 2) / 320, 0, cosf(cameraAngle) * tanf(angleOfView / 2) / 320 };	//same in the y direction

	vector ball = { (currenty - 240) * ey.e1 + eCameraNormal.e1, 
					(currentx - 320) * ex.e2 + eCameraNormal.e2, 
					(currenty - 240) * ey.e3 + eCameraNormal.e3 };
	ball.e3 = fabs(ball.e3) < 1 / 10000 ? 1 / 10000 : fabs(ball.e3);

	floorX = ball.e1 * (cameraHeight - heightFromFloor) / ball.e3, floorY = ball.e2 * (cameraHeight - heightFromFloor) / ball.e3;
}

//rotates around the center with the given angular velocity in degs/s
void rotateAroundCenter(float angularVelocity) {
	if (currentDrivingState.angle != 0 || currentDrivingState.speed != 0 || currentDrivingState.angularVelocity != angularVelocity ||
		currentDrivingState.vx != 0 || currentDrivingState.vy != 0 || lastSpeedSentTimer.time() > 0.5) {
		currentDrivingState.angle = 0, currentDrivingState.speed = 0, currentDrivingState.angularVelocity = angularVelocity;
		currentDrivingState.vx = 0, currentDrivingState.vy = 0;
		setSpeedAngle(currentDrivingState);
		lastSpeedSentTimer.start();
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
		convertToFloorCoordinates(objects.data[i].x, objects.data[i].y, floorX2, floorY2, BALLRADIUS);
		float dist = (floorX - floorX2) * (floorX - floorX2) + (floorY - floorY2) * (floorY - floorY2), minIndex = 0;
		if (minDist == 0 || dist < minDist) {
			minDist = dist;
			minIndex = i;
		}
	}
	convertToFloorCoordinates(objects.data[minIndex].x, objects.data[minIndex].y, floorX, floorY, BALLRADIUS);
}


//finds the nearest ball to the robot
int findNearestFloorBall(float& nearestFloorX, float& nearestFloorY, int& currentx, int& currenty) {
	int ballCount = 0;
	float nearestX = 0, nearestY = 0;
	for (int i = 0; i < ballsShare.count; ++i) {
		float floorX, floorY;
		convertToFloorCoordinates(ballsShare.data[i].x, ballsShare.data[i].y, floorX, floorY, BALLRADIUS);
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
	DWORD start = 0, length = 0; //the current position in the buffer and the length of the unread buffer
	char readBuffer[128] = {};
	readCOM(hCOMRadio, readBuffer, 12, bytesRead);
	length += bytesRead;
	prints(L"Checking command v1: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));

	while (true) {
		if (readBuffer[start] != 'a' && length > 0) {
			++start;
			--length;
		}
		else {
			if (length < 12) {
				readCOM(hCOMRadio, readBuffer + start + length, 12, bytesRead);
				prints(L"Checking command v2: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));
				length += bytesRead;
				if (length < 12) {
					prints(L"returning\n");
					return;
				}
				if (!validCommand(readBuffer + start)) {
					++start;
					--length;
				}
				else {
					checkCommand(readBuffer + start);
					++start;
					--length;
				}
			}
			else {
				prints(L"Checking command v3: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));
				checkCommand(readBuffer + start);
				++start;
				--length;
			}
		}
		if (length == 0) {
			readCOM(hCOMRadio, readBuffer + start, 12, bytesRead);
			length += bytesRead;
			prints(L"Checking command v4: %S, valid: %d\n", readBuffer + start, validCommand(readBuffer));
			if (length < 12) {
				prints(L"returning\n");
				return;
			}
		}
		if (start > 64) {
			prints(L"zeroing\n");
			CopyMemory(readBuffer, readBuffer + start, length);
			ZeroMemory(readBuffer + length, 128 - length);
			start = 0;
		}
	}
	prints(L"returning\n");
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
			//prints(L"Received from radio: %S\n", tempBuffer);
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
			if ((readBuffer[2] == 'X' || readBuffer[2] == currentID[1]) && listenToRadio) {
				if (strcmp(tempBuffer+3, "STOP-----") == 0) {
					ResetEvent(startSignal);
					SetEvent(stopSignal);
					if (readBuffer[2] == currentID[1]) {
						respondACK();
					}
				}
				else if (strcmp(tempBuffer+3, "START----") == 0) {
					ResetEvent(stopSignal);
					SetEvent(startSignal);
					if (readBuffer[2] == currentID[1]) {
						respondACK();
					}
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

	dcb.BaudRate = 115200;
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

	if (!SetCommMask(hCOMRadio, EV_RXCHAR)) {
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

	//testUSBOK();

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
