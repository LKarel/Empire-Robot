#pragma once

#define PI (3.1415927f)
DWORD WINAPI GUICamera(LPVOID lpParameter);
void prints(wchar_t* text, ...);
void setSpeedAngle(float speed, float angle, float angularVelocity); //from the main program

//max wheel speed is around 2.176 m/s, max rot speed of the robot is around 890 degrees/s
#define wheelRadius (3.5 / 100.0) //in meters
#define baseRadius (14.0 / 100.0) //base radius in meters from the center to the wheel
#define cameraHeight (17.0 / 100.0) //height in meters of the camera
#define cameraAngle (25.0 * PI / 180.0) //the angle in radians that the camera is down with respect to the horizontal, height of camera is 17cm, midpoint of the image is 37 cm away
#define angleOfView (59.0 * PI / 180.0) //the horizontal angle of view in radians, it saw a 77cm wide rule 67cm away along the floor


//information about a detected object
struct objectInfo {
	int x;
	int y;
	int pixelcount; //how many pixels are in the blob
	bool isObjectAcrossLine;
};

struct drivingState {
	float speed;
	float angle;
	float angularVelocity;
	float vx;
	float vy;
};

struct objectCollection {
	int count;
	objectInfo *data;
	int size; //how many objects the data buffer can hold
	bool isLineBetweenObject;
};

struct lineInfo {
	int radius;
	float angle;
	int pixelcount; //how many pixels are in the blob
};

struct lineCollection {
	int count;
	lineInfo *data;
	int size; //how many objects the data buffer can hold
};

class Timer { //used for timing various events and states
public:
	LARGE_INTEGER startCounter, stopCounter;
	static LARGE_INTEGER timerFrequency;
	bool stopped = true;

	void start() {
		QueryPerformanceCounter(&startCounter);
		stopped = false;
	}
	float time() {
		if (!stopped) {
			QueryPerformanceCounter(&stopCounter);
		}
		return (float)(double(stopCounter.QuadPart - startCounter.QuadPart) / double(timerFrequency.QuadPart));
	}
	void stop() {
		QueryPerformanceCounter(&stopCounter);
		stopped = true;
	}
};

void doubleObjectBufferSize(objectCollection*);
void setSpeedAngle(drivingState currentDrivingState);
//void doubleLineBufferSize(lineCollection*);