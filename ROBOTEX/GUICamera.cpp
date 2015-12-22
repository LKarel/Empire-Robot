#include "stdafx.h"
#include "GUICamera.h"
#include "qedit.h"

#define MAX3(r,g,b) ( r >= g ? (r >= b ? r : b) : (g >= b ? g : b) ) //max of three
#define MIN3(r,g,b) ( r <= g ? (r <= b ? r : b) : (g <= b ? g : b) ) //min of three
#define OBJECTINDEX(x) ((x)&0x3FFFFFFF)	//first 30 bits, gives the index of the current ball or goal
#define BALLORGOAL(x) (((x)>>31)&1)	//32rd bit, the identifier for whether the pixel represents a ball or a goal
#define BIT32 (1<<31)
#define GETOBJID(x) (((x)>>30)&3) //object ids in the temporary buffer, the last bits determine what kind of an object it is
#define MAKEOBJID(x) ((x)<<30)
#define HUE(red,green,blue) ((red >= green && green >= blue && red > blue) ?	((float)(green - blue) / (red - blue)) :\
			(green > red && red >= blue) ?	(2 - (float)(red - blue) / (green - blue)):\
			(green >= blue && blue > red) ?	(2 + (float)(blue - red) / (green - red)):\
			(blue > green && green > red) ?	(4 - (float)(green - red) / (blue - red)):\
			(blue > red && red >= green) ?	(4 + (float)(red - green) / (blue - green)):\
			(red >= blue && blue > green) ?	(6 - (float)(blue - green) / (red - green)):\
			0.0f) //Hue when the image is gray red=green=blue
#define SATURATION(red,green,blue) (MAX3(red,green,blue) == 0 ? 0 : (float)(MAX3(red, green, blue) - MIN3(red, green, blue)) / \
									MAX3(red, green, blue))
#define VALUE(red,green,blue) ((float)MAX3(red, green, blue)/255)
#define COLORSFIT(colors, hue, saturation, value) (hue <= colors.hueMax && hue >= colors.hueMin && \
													saturation <= colors.saturationMax && saturation >= colors.saturationMin && \
													value <= colors.valueMax && value >= colors.valueMin)

//constants for events and ID-s of GUI elements
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
void charge();
void discharge();
void kick(int microSeconds);
void kick();
void dribblerON();
void dribblerOFF();
bool checkRoundness(objectCollection& balls, int i, DWORD* analyzeBuffer, int objectID);
bool isLineBetweenRobotAndBall(int ballX, int ballY, BYTE* pBuffer);
bool isLineBetweenRobotAndBall2(int ballX, int ballY, BYTE* pBuffer);
bool isOnTopOfRobot(int ballX, int ballY, BYTE* pBuffer);

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
BYTE *DShowBuffer; //the buffer that the dshow displays on the screen
BOOL start = TRUE;
DWORD* pixelsTest;
Timer FPSTimer;
DWORD FPSCount;
bool isLineStraightAhead;

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
HWND goalsBlueRadioButton;
HWND goalsYellowRadioButton;
HWND linesBlackRadioButton;
HWND linesWhiteRadioButton;
HWND fieldGreenRadioButton;
HWND hwndBallsPixelCount;
HWND hwndGoalsBluePixelCount;
HWND hwndGoalsYellowPixelCount;
HWND hwndLinesPixelCount;
HWND whitenCheckBox;
HWND excludeCheckBox;
HWND minRadioButton;
HWND maxRadioButton;
HWND radioCheckBox;
HWND hwndEditID = NULL;
HWND stateStatusGUI;
HWND goalGuessState;
HWND statusFPS;
HWND statusFPS2;
HWND statusBall;
HWND infoGUI;
HWND infoGUI2;
HWND infoGUI3;
HWND goalSelectCheckBox;
HWND hwndDrivingSpeed;
HWND hwndAngularVelocity;

HWND SP;
HWND SI;
HWND SD;
HWND AVP;
HWND AVI;
HWND AVD;
extern float speedP;
extern float speedI;
extern float speedD;
extern float angularVelocityP;
extern float angularVelocityI;
extern float angularVelocityD;

BOOLEAN calibrating = FALSE;	//state variable
BOOLEAN whitenThresholdPixels;	//whether to make the selected pixels white during calibration
extern drivingState currentDrivingState;	//current driving speed valuse
float keyboardSpeed;
float keyboardAngularVelocity;

//signals for communicating between threads
extern HANDLE readySignal;
extern HANDLE newImageAnalyzed;
extern HANDLE startSignal;
extern HANDLE stopSignal;
extern HANDLE calibratingSignal;
extern HANDLE calibratingEndSignal;
extern int listenToRadio;
extern char* currentID;
extern int ballsPixelCount, goalsBluePixelCount, goalsYellowPixelCount, linesPixelCount;
HANDLE writeMutex = CreateMutex(NULL, FALSE, NULL);
extern bool attackBlue;

enum {
	ID_EDITCHILD, ID_BUTTON_START, ID_BUTTON_STOP, ID_BUTTON_CALIBRATE, ID_BUTTON_DONE, ID_TRACKBAR_HUE, ID_TRACKBAR_SATURATION,
	ID_TRACKBAR_VALUE, ID_RADIOBOXGROUP_MINMAX, ID_RADIOBOX_MIN, ID_RADIOBOX_MAX, ID_BUTTON_SAVE,
	ID_BUTTON_RESET, ID_RADIOBOXGROUP_OBJECTSELECTOR, ID_RADIOBOX_BALLS, ID_RADIOBOX_GOALSBLUE, ID_RADIOBOX_GOALSYELLOW,
	ID_RADIOBOX_LINES_BLACK, ID_RADIOBOX_LINES_WHITE, ID_RADIOBOX_FIELD_GREEN, ID_CHECKBOX_WHITEN, ID_CHECKBOX_RADIO, ID_EDIT_ID, 
	ID_BALLS_PIXELCOUNT, ID_GOALSBLUE_PIXELCOUNT, ID_CHECKBOX_EXCLUDE,
	ID_GOALSYELLOW_PIXELCOUNT, ID_LINES_PIXELCOUNT, ID_STATUS_TEXT, ID_BUTTON_CHARGE, ID_BUTTON_KICK, ID_BUTTON_DISCHARGE,
	ID_BUTTON_DRIBBLER_ON, ID_BUTTON_DRIBBLER_OFF, ID_STATUS_FPS, ID_STATUS_BALL, ID_GOAL_SELECTION, ID_SET_SPEED,
	ID_SET_ANGULAR_VELOCITY, ID_INFO, ID_INFO2, ID_INFO3, ID_SP, ID_SI, ID_SD, ID_AVP, ID_AVI, ID_AVD
};

objectCollection balls, goalsBlue, goalsYellow, ballsShare, goalsBlueShare, goalsYellowShare, linesBlack, linesWhite; //local data structure for holding objects and shared structures
lineCollection lines, linesShare; //data for the lines
int fieldGreenPixelCount, fieldGreenPixelCountShare;

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
	bool exclude = false;
	bool active = false;
} ballColors, goalBlueColors, goalYellowColors, lineBlackColors, lineWhiteColors, fieldGreenColors;
colorValues *activeColors;	//the current active color values in the calibrator

enum CurrentCalibratorSetting { minimum, maximum };
CurrentCalibratorSetting currentCalibratorSetting = minimum;

void analyzePixelSurroundings(objectCollection &objects, colorValues &colors, int objectType, DWORD* pixBuffer, DWORD* pBufferCopy, int &x, int &y) {
	//check if there is already a goal in the pixel below it, then group it together with it
	if (y > 0 && GETOBJID(pBufferCopy[x + 640 * (y - 1)]) == objectType &&
		OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)])) {
		int index = OBJECTINDEX(pBufferCopy[x + 640 * (y - 1)]);
		//remove the new group if it was made and decrease the object count
		if (x >= 1 && pBufferCopy[x - 1 + 640 * y] &&
			GETOBJID(pBufferCopy[x - 1 + 640 * y]) == objectType) {
			objects.count -= 1;
			objects.data[OBJECTINDEX(pBufferCopy[x - 1 + 640 * y])] = {};
		}
		//change all the pixels before to the new object index
		pBufferCopy[x + 640 * y] = index + MAKEOBJID(objectType);
				//((DWORD*)g_pBuffer)[x + y * 640] = index * 5; //test
		objects.data[index].pixelcount += 1;
		objects.data[index].x += x;
		objects.data[index].y += y;
		for (int x2 = x - 1; x2 >= 0 && pBufferCopy[x2 + 640 * y] &&
			GETOBJID(pBufferCopy[x2 + 640 * y]) == objectType; --x2) {
			pBufferCopy[x2 + 640 * y] = index + MAKEOBJID(objectType);
					//((DWORD*)g_pBuffer)[x2 + y * 640] = index * 5; //test
			objects.data[index].pixelcount += 1;
			objects.data[index].x += x2;
			objects.data[index].y += y;
		}
		//change all the pixels forward in the line to the object index,
		//merge the objects if it meets another object
		for (++x; x < 640; ++x) {
			int blue = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
				red = (pixBuffer[x + y * 640] >> 16) & 0xFF;
			float hue = HUE(red, green, blue);
			float saturation = SATURATION(red, green, blue);
			float value = VALUE(red, green, blue);

			if (!COLORSFIT(colors, hue, saturation, value)) {
				--x;
				break;
			}
			objects.data[index].pixelcount += 1;
			objects.data[index].x += x;
			objects.data[index].y += y;
			pBufferCopy[x + 640 * y] = index + MAKEOBJID(objectType);
					//((DWORD*)g_pBuffer)[x + y  * 640] = index * 5; //test
			int index2 = OBJECTINDEX(pBufferCopy[x + (y - 1) * 640]);
			//there are objects with different indexes on the current pixel and below it
			if (GETOBJID(pBufferCopy[x + (y - 1) * 640]) == objectType &&
				index2 && index2 != index) {
				//copy the pixels and coordinates from the object under the line to the new object
				objects.data[index].pixelcount += objects.data[index2].pixelcount;
				objects.data[index].x += objects.data[index2].x;
				objects.data[index].y += objects.data[index2].y;
				objects.data[index2] = {};
				//set the last line of the object from the line under to the proper index, so that when
				//it is encoutered again, it is interpreted as the correct object
				for (int x2 = x; x2 <= 640; ++x2) {
					if (GETOBJID(pBufferCopy[x2 + (y - 1) * 640]) == objectType &&
						OBJECTINDEX(pBufferCopy[x2 + (y - 1) * 640]) == index2) {
						pBufferCopy[x2 + (y - 1) * 640] = index + MAKEOBJID(objectType);
								//((DWORD*)g_pBuffer)[x2 + (y - 1) * 640] = index * 5; //test
					}
				}
			}
		}
	}
	else {
		//if there is an object pixel to the left
		if (x > 0 && GETOBJID(pBufferCopy[x - 1 + 640 * y]) == objectType && pBufferCopy[x - 1 + 640 * y]) {
			int index = OBJECTINDEX(pBufferCopy[x - 1 + 640 * y]);
			objects.data[index].x += x;
			objects.data[index].y += y;
			objects.data[index].pixelcount += 1;
			pBufferCopy[x + 640 * y] = index + MAKEOBJID(objectType);
			//((DWORD*)g_pBuffer)[x + y * 640] = OBJECTINDEX(g_pBuffer[x + y * 640]) * 5; //test
		}
		//start a new object
		else {
			++(objects.count);
			pBufferCopy[x + 640 * y] = objects.count + MAKEOBJID(objectType);
			//((DWORD*)g_pBuffer)[x + y * 640] = OBJECTINDEX(g_pBuffer[x + y * 640]) * 5; //test
			if (objects.count == objects.size)
				doubleObjectBufferSize(&objects);
			objects.data[objects.count].pixelcount += 1;
			objects.data[objects.count].x += x;
			objects.data[objects.count].y += y;
		}
	}
}

//this analyzes the image to find all the balls, the goals and the black lines
void analyzeImage(double Time, BYTE *pBuffer, long BufferLen) {
	int ballCount = 0, goalCount = 0;
	DWORD* pixBuffer = (DWORD*)pBuffer;
	ZeroMemory(pBufferCopy, 640 * 480 * 4); //zero the buffer with the indexes and object information
	ZeroMemory(houghTransformBuffer, 150 * 150 * 4);
	ZeroMemory(balls.data, balls.size*sizeof(objectInfo));
	ZeroMemory(goalsBlue.data, goalsBlue.size*sizeof(objectInfo));
	ZeroMemory(goalsYellow.data, goalsYellow.size*sizeof(objectInfo));
	ZeroMemory(lines.data, lines.size*sizeof(lineInfo));
	balls.count = 0;
	goalsBlue.count = 0;
	lines.count = 0;
	fieldGreenPixelCount = 0;
	for (int y = 0; y < 480; ++y) {
		for (int x = 0; x < 640; ++x) {
			int blue = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
				red = (pixBuffer[x + y * 640] >> 16) & 0xFF;

			float hue = HUE(red, green, blue);
			float saturation = SATURATION(red, green, blue);
			float value = VALUE(red, green, blue);

			if (COLORSFIT(ballColors, hue, saturation, value)) {
				analyzePixelSurroundings(balls, ballColors, 0, pixBuffer, pBufferCopy, x, y);
			}
			else if (COLORSFIT(goalBlueColors, hue, saturation, value)) {
				analyzePixelSurroundings(goalsBlue, goalBlueColors, 1, pixBuffer, pBufferCopy, x, y);
			}
			else if (COLORSFIT(goalYellowColors, hue, saturation, value)) {
				analyzePixelSurroundings(goalsYellow, goalYellowColors, 2, pixBuffer, pBufferCopy, x, y);
			}
			if (COLORSFIT(fieldGreenColors, hue, saturation, value)) {
				fieldGreenPixelCount++;
			}
		}
	}
	ballsPixelCount = ballsPixelCount <= 0 ? 1 : ballsPixelCount;
	goalsBluePixelCount = goalsBluePixelCount <= 0 ? 1 : goalsBluePixelCount;
	goalsYellowPixelCount = goalsYellowPixelCount <= 0 ? 1 : goalsYellowPixelCount;
	linesPixelCount = linesPixelCount <= 0 ? 1 : linesPixelCount;
	//check if there are enough pixels in an object, get rid of the other ones and count the balls
	ballCount = 0;
	//printf("balls count before: %d\n", balls.count);
	for (int i = 0; i <= balls.count; ++i) {
		if (balls.data[i].pixelcount >= ballsPixelCount && checkRoundness(balls, i, pBufferCopy, 0)) { // && checkRoundness(balls, i, pBufferCopy, 0)
			balls.data[ballCount] = balls.data[i];
			balls.data[ballCount].x /= balls.data[ballCount].pixelcount;
			balls.data[ballCount].y /= balls.data[ballCount].pixelcount;
			//start at 142 because the robot is quite light itself, after that should be lines only
			balls.data[ballCount].isObjectAcrossLine = isLineBetweenRobotAndBall2(balls.data[ballCount].x, balls.data[ballCount].y, pBuffer)
													|| isOnTopOfRobot(balls.data[ballCount].x, balls.data[ballCount].y, pBuffer);
			//printf("x %d y %d pix %d\n", balls.data[ballCount].x, 
			//	balls.data[ballCount].y, balls.data[ballCount].pixelcount);
			++ballCount;
		}
	}
	balls.count = ballCount;
	//printf("balls count: %d\n", balls.count);

	goalCount = 0;
	for (int i = 0; i <= goalsBlue.count; ++i) {
		if (goalsBlue.data[i].pixelcount > 0) {
			int y = goalsBlue.data[i].y / goalsBlue.data[i].pixelcount;
			if (goalsBlue.data[i].pixelcount >= goalsBluePixelCount + pow((480 - y) / 80.0, 2) * 2000) {
				goalsBlue.data[goalCount] = goalsBlue.data[i];
				goalsBlue.data[goalCount].x /= goalsBlue.data[goalCount].pixelcount;
				goalsBlue.data[goalCount].y /= goalsBlue.data[goalCount].pixelcount;
				//printf("x %d y %d pix %d\n", balls.data[ballCount].x, 
				//	balls.data[ballCount].y, balls.data[ballCount].pixelcount);
				++goalCount;
			}
		}
	}
	goalsBlue.count = goalCount;

	goalCount = 0;
	for (int i = 0; i <= goalsYellow.count; ++i) {
		if (goalsYellow.data[i].pixelcount > 0) {
			int y = goalsYellow.data[i].y / goalsYellow.data[i].pixelcount;
			if (goalsYellow.data[i].pixelcount >= goalsYellowPixelCount + pow((480 - y) / 80.0, 2) * 2000) {
				goalsYellow.data[goalCount] = goalsYellow.data[i];
				goalsYellow.data[goalCount].x /= goalsYellow.data[goalCount].pixelcount;
				goalsYellow.data[goalCount].y /= goalsYellow.data[goalCount].pixelcount;
				//printf("x %d y %d pix %d\n", balls.data[ballCount].x, 
				//	balls.data[ballCount].y, balls.data[ballCount].pixelcount);
				++goalCount;
			}
		}
	}
	goalsYellow.count = goalCount;

	////get the lines from the Hough transform accumulator, no more used, low FPS
	//for (int angle = 0; angle < 150; ++angle) {
	//	for (int r = 0; r < 150; ++r) {
	//		int current = houghTransformBuffer[r + 150 * angle] / 2;
	//		int left = r > 0 ? houghTransformBuffer[r - 1 + 150 * angle] / 2 : 0;
	//		int up = angle < 150-1 ? houghTransformBuffer[r + 150 * (angle + 1)] / 2 : 0;
	//		int right = r < 150-1 ? houghTransformBuffer[r + 1 + 150 * angle] / 2 : 0;
	//		int down = angle > 0 ? houghTransformBuffer[r + 150 * (angle - 1)] / 2 : 0;
	//		if (current > linesPixelCount && current >= left && current >= down && current > right && current > up) {
	//			lines.data[lines.count].radius = 4*r;
	//			lines.data[lines.count].angle = angle*2*PI/150;
	//			lines.data[lines.count].pixelcount = current;
	//			lines.count++;
	//			if (lines.count == lines.size)
	//				doubleObjectBufferSize((objectCollection*)&lines);
	//			//wprintf(L"current: %d, pixelcount: %d\n", current, lines.data[lines.count - 1].pixelcount);
	//		}
	//	}
	//}

	//send the data to the other thread
	WaitForSingleObject(writeMutex, INFINITE);
	while (balls.count >= ballsShare.size)
		doubleObjectBufferSize(&ballsShare);
	ballsShare.count = balls.count;
	CopyMemory(ballsShare.data, balls.data, balls.count*sizeof(objectInfo));

	while (goalsBlue.count >= goalsBlueShare.size)
		doubleObjectBufferSize(&goalsBlueShare);
	goalsBlueShare.count = goalsBlue.count;
	CopyMemory(goalsBlueShare.data, goalsBlue.data, goalsBlue.count*sizeof(objectInfo));

	while (goalsYellow.count >= goalsYellowShare.size)
		doubleObjectBufferSize(&goalsYellowShare);
	goalsYellowShare.count = goalsYellow.count;
	CopyMemory(goalsYellowShare.data, goalsYellow.data, goalsYellow.count*sizeof(objectInfo));

	while (lines.count >= linesShare.size)
		doubleObjectBufferSize((objectCollection*)&linesShare);
	linesShare.count = lines.count;
	CopyMemory(linesShare.data, lines.data, lines.count*sizeof(lineInfo));
	
	fieldGreenPixelCountShare = fieldGreenPixelCount;
	isLineStraightAhead = isLineBetweenRobotAndBall2(320, 300, pBuffer);
	ReleaseMutex(writeMutex);
}

//draws a cross of the color #0xAABBGGRR to coordinates x, y starting from the bottom left corner (Windows bitmap uses that)
void drawCross(int x, int y, int color, BYTE* buffer) {
	DWORD *pixBuffer = (DWORD *)buffer;
	for (int j = -0;j <= 1;++j)
		for (int i = -10;i <= 10;++i)
			if (0 <= x + i && x + i < 640 && 0 <= y + j && y + j < 480)
				pixBuffer[x + i + 640 * (y + j)] = color;
	for (int j = -0;j <= 1;++j)
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

//because the cameras on the robot are upside down
void reverse(BYTE* buffer) {
	DWORD pixel;
	DWORD* pixBuffer = (DWORD*)buffer;
	for (int y = 0; y < 480/2; ++y) {
		for (int x = 0; x < 640; ++x) {
			pixel = pixBuffer[x + 640 * y];
			pixBuffer[x + 640 * y] = pixBuffer[640 - 1 - x + 640 * (480 - 1 - y)];
			pixBuffer[640 - 1 - x + 640 * (480 - 1 - y)] = pixel;
		}
	}
}

//check if there are enough pixels in a square with side twice the radius of the ball based on the pixelcount
bool checkRoundness(objectCollection& balls, int i, DWORD* analyzeBuffer, int objectID) {
	int size = int(pow(balls.data[i].pixelcount/PI, 0.5))*1.5;
	size = size > 5 ? size : 5;
	int counter = 0;
	int posx = balls.data[i].x / balls.data[i].pixelcount;
	int posy = balls.data[i].y / balls.data[i].pixelcount;
	for (int y = posy - size; y < posy + size; ++y) {
		for (int x = posx - size; x < posx + size; ++x) {
			if (x >= 0 && x < 640 && y >= 0 && y < 480) {
				DWORD temp = analyzeBuffer[x + y * 640];
				if (GETOBJID(temp) == objectID && OBJECTINDEX(temp)) {
					++counter;
				}
			}
		}
	}
	if (counter > balls.data[i].pixelcount*0.5) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}

bool isOnTopOfRobot(int ballX, int ballY, BYTE* pBuffer) {
	DWORD* pixBuffer = (DWORD*)pBuffer;
	int blueCount = 0, yellowCount = 0;
	for (int y = ballY; y >= 137; --y) {
		for (int x = ballX - 1; x <= ballX + 1; ++x) {
			if (x >= 0 && x < 640) {
				int blue = (pixBuffer[x + y * 640]) & 0xFF, green = (pixBuffer[x + y * 640] >> 8) & 0xFF,
					red = (pixBuffer[x + y * 640] >> 16) & 0xFF;
				float hue = HUE(red, green, blue);
				float saturation = SATURATION(red, green, blue);
				float value = VALUE(red, green, blue);

				if (COLORSFIT(goalBlueColors, hue, saturation, value)) {
					++blueCount;
				}
				if (COLORSFIT(goalYellowColors, hue, saturation, value)) {
					++yellowCount;
				}
			}
		}
	}
	if (blueCount > 15 || yellowCount > 15) {
		return true;
	}
	return false;
}

//considers as elements of one line if there isn't a larger than 10px gap of incorrect colors for the line
//then uses these line elements to determine if there is a black line after a white one and they are close enough
bool isLineBetweenRobotAndBall2(int ballX, int ballY, BYTE* pBuffer) {
	ZeroMemory(linesBlack.data, linesBlack.size * sizeof(objectInfo));
	ZeroMemory(linesWhite.data, linesWhite.size * sizeof(objectInfo));
	int x = 320; //in camera x,y coordinates 
	int y = 240 - 1 / tanf(cameraAngle) / (tanf(angleOfView / 2) / 320);
	int avgXWhite = 0, avgYWhite = 0, avgXBlack = 0, avgYBlack = 0, blackPixelCount = 0, whitePixelCount = 0;
	DWORD* pixBuffer = (DWORD*)pBuffer;

	if (ballX == x && ballY == y) {
		return FALSE;
	}

	float lineY = y; //start coordinates
	float lineX = x;
	float slope;
	int step;
	int whiteLineCount = 0, blackLineCount = 0;
	int whiteMissed = 0, blackMissed = 0;

	if (abs(ballX - x) >= abs(ballY - y)) { //move along the x or y direction based depending on the angle of the line
		step = ballX > x ? 1 : -1;
		slope = (float)(ballY - y) / (ballX - x);
	}
	else {
		step = ballY > y ? 1 : -1;
		slope = (float)(ballX - x) / (ballY - y);
	}

	while (TRUE) { //make a list of all the lines
		int lineXint = lineX;
		int lineYint = lineY;

		if (lineYint >= 107 && lineYint < 480) {
			bool atLeastOneBlack = false, atLeastOneWhite = false;
			for (int X2 = lineXint - 1; X2 <= lineXint + 1; ++X2) { //check 3 pixels on the line
				if (X2 >= 0 && X2 < 640 && (X2 >= 140 && X2 <= 525 || lineYint >= 140)) {
					//wprintf(L"lineXint: %d, lineYint: %d\n", X2, lineYint);
					int blue = (pixBuffer[X2 + lineYint * 640]) & 0xFF, green = (pixBuffer[X2 + lineYint * 640] >> 8) & 0xFF,
						red = (pixBuffer[X2 + lineYint * 640] >> 16) & 0xFF;
					float hue = HUE(red, green, blue);
					float saturation = SATURATION(red, green, blue);
					float value = VALUE(red, green, blue);

					if (COLORSFIT(lineBlackColors, hue, saturation, value)) {
						linesBlack.data[blackLineCount].x += X2;
						linesBlack.data[blackLineCount].y += lineYint;
						linesBlack.data[blackLineCount].pixelcount += 1;
						atLeastOneBlack = true;
						blackMissed = 0;
					}
					if (COLORSFIT(lineWhiteColors, hue, saturation, value)) {
						linesWhite.data[whiteLineCount].x += X2;
						linesWhite.data[whiteLineCount].y += lineYint;
						linesWhite.data[whiteLineCount].pixelcount += 1;
						atLeastOneWhite = true;
						whiteMissed = 0;
					}
				}
			}
			if (!atLeastOneBlack) {
				blackMissed++;
			}
			if (!atLeastOneWhite) {
				whiteMissed++;
			}
			if (blackMissed == 10 && linesBlack.data[blackLineCount].pixelcount) {
				blackLineCount++;
				if (blackLineCount == linesBlack.size) {
					doubleObjectBufferSize(&linesBlack);
				}
			}
			if (whiteMissed == 10 && linesWhite.data[whiteLineCount].pixelcount) {
				whiteLineCount++;
				if (whiteLineCount == linesWhite.size) {
					doubleObjectBufferSize(&linesWhite);
				}
			}
		}

		if (abs(ballX - x) >= abs(ballY - y)) {
			lineX += step;
			lineY += slope;
		}
		else {
			lineX += slope;
			lineY += step;
		}
		if (lineYint == ballY) {
			break;
		}
	}

	for (int b = 0; b <= blackLineCount; ++b) {
		if (linesBlack.data[b].pixelcount > 0) {
			linesBlack.data[b].x /= linesBlack.data[b].pixelcount;
			linesBlack.data[b].y /= linesBlack.data[b].pixelcount;
		}
	}

	for (int w = 0; w <= whiteLineCount; ++w) {
		if (linesWhite.data[w].pixelcount > 0) {
			linesWhite.data[w].x /= linesWhite.data[w].pixelcount;
			linesWhite.data[w].y /= linesWhite.data[w].pixelcount;
		}
	}

	for (int b = 0; b <= blackLineCount; ++b) {
		if (linesBlack.data[b].pixelcount > 25) {
			for (int w = 0; w <= whiteLineCount; ++w) {
				if (linesBlack.data[b].y > linesWhite.data[w].y && linesBlack.data[b].y - linesWhite.data[w].y < 70
					&& linesWhite.data[w].pixelcount > 20 && linesBlack.data[b].pixelcount > 20) {
					return true;
				}
			}
		}
	}

	return false;
}

bool isLineBetweenRobotAndBall(int ballX, int ballY, BYTE* pBuffer) {
	int x = 320; //in camera x,y coordinates 
	int y = 240 - 1 / tanf(cameraAngle) / (tanf(angleOfView / 2) / 320);
	int avgXWhite = 0, avgYWhite = 0, avgXBlack = 0, avgYBlack = 0, blackPixelCount = 0, whitePixelCount = 0;
	DWORD* pixBuffer = (DWORD*)pBuffer;

	if (ballX == x && ballY == y) {
		return FALSE;
	}

	float lineY = y; //start coordinates
	float lineX = x;
	float slope;
	int step;

	if (abs(ballX - x) >= abs(ballY - y)) { //move along the x or y direction based depending on the angle of the line
		step = ballX > x ? 1 : -1;
		slope = (float)(ballY - y) / (ballX - x);
	}
	else {
		step = ballY > y ? 1 : -1;
		slope = (float)(ballX - x) / (ballY - y);
	}
	
	
	while (TRUE) {
		int lineXint = lineX;
		int lineYint = lineY;
		if (lineYint >= 0 && lineYint < 480) {
			for (int X2 = lineXint - 1; X2 <= lineXint + 1; ++X2) { //check 3 pixels on the line
				if (X2 >= 0 && X2 < 640) {
					//wprintf(L"lineXint: %d, lineYint: %d\n", X2, lineYint);
					int blue = (pixBuffer[X2 + lineYint * 640]) & 0xFF, green = (pixBuffer[X2 + lineYint * 640] >> 8) & 0xFF,
						red = (pixBuffer[X2 + lineYint * 640] >> 16) & 0xFF;
					float hue = HUE(red, green, blue);
					float saturation = SATURATION(red, green, blue);
					float value = VALUE(red, green, blue);
					if (COLORSFIT(lineBlackColors, hue, saturation, value)) {
						avgXBlack += X2;
						avgYBlack += lineYint;
						blackPixelCount += 1;
					}
					else if (COLORSFIT(lineWhiteColors, hue, saturation, value)) {
						avgXWhite += X2;
						avgYWhite += lineYint;
						whitePixelCount += 1;
					}
				}
			}
		}
		if (abs(ballX - x) >= abs(ballY - y)) {
			lineX += step;
			lineY += slope;
		}
		else {
			lineX += slope;
			lineY += step;
		}
		if (lineYint == ballY) {
			break;
		}
	}

	//prints(L"ball %d %d line %.2f %.2f\n", ballX, ballY, lineX, lineY);
	//prints(L"%d %d %d %d\n", avgXBlack, avgYBlack, avgXWhite, avgYWhite);
	if (whitePixelCount >= 20 && blackPixelCount >= 20) {
		avgXBlack /= blackPixelCount;
		avgYBlack /= blackPixelCount;
		avgXWhite /= whitePixelCount;
		avgYWhite /= whitePixelCount;
		if (avgYBlack > avgYWhite) {
			return TRUE;
		}
	}
	return FALSE;
}

void smoothen(int range, BYTE* source, BYTE* destination) {
	DWORD* pixSource = (DWORD*)source;
	DWORD* pixDestination = (DWORD*)destination;
	for (int y = 0; y < 480; ++y) {
		int redavg = 0, greenavg = 0, blueavg = 0;
		for (int x = 0; x < 640; ++x) {
			int red = 0, green = 0, blue = 0, counter = 0;
			for (int y2 = 0; y2 < range; y2++) {
				for (int x2 = 0; x2 < range; x2++) {
					int x3 = x - range / 2 + x2, y3 = y - range / 2 + y2;
					if (x3 >= 0 && x3 < 640 && y3 >= 0 && y3 < 480) {
						DWORD pixel = pixSource[x3 + y3*(640)];
						blue += pixel & 0xFF;
						green += (pixel >> 8) & 0xFF;
						red += (pixel >> 16) & 0xFF;
						counter++;
					}
				}
			}
			red = red / counter;
			green = green / counter;
			blue = blue / counter;

			pixDestination[x + y * 640] = blue | (green << 8) | (red << 16);
		}
	}
}
bool temp = true;

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
		reverse(pBuffer); //because the cameras on the robot are upside down
		DShowBuffer = pBuffer;

		//update FPS rate after every 5 images
		++FPSCount;
		if (FPSCount % 5 == 0) {
			wchar_t data[32] = {};
			float FPS = 5.0 / FPSTimer.time();
			swprintf_s(data, L"FPS: %.2f", FPS);
			SetWindowText(statusFPS, data);
			FPSTimer.start();
		}

		//smoothen the image by averaging over pixel values
		if (g_pBuffer != NULL) {
			//smoothen(2, pBuffer, g_pBuffer);
			CopyMemory(g_pBuffer, pBuffer, BufferLen);
		}

		//if (g_pBuffer != NULL) { //for displaying the image from the analyzeTest function, for testing purposes
		//	memcpy(g_pBuffer, pixelsTest, 640*480*4);
		//}
		//return S_OK;

		//if the calibrator is open, black out the pixels that aren't in the range of the current selection
		//in the calibrator window
		if (calibrating && g_pBuffer != NULL) {
			for (DWORD *pixBuffer = (DWORD*)g_pBuffer; pixBuffer < (DWORD*)g_pBuffer + BufferLen/4; ++pixBuffer) {
				DWORD pixel = *pixBuffer;
				int blue = pixel & 0xFF, green = (pixel >> 8) & 0xFF, red = (pixel >> 16) & 0xFF;
				
				float hue = HUE(red, green, blue);
				float saturation = SATURATION(red,green,blue);
				float value = VALUE(red, green, blue);

				if (!COLORSFIT((*activeColors), hue, saturation, value)) {
					*pixBuffer = 0;
				}
				else if (whitenThresholdPixels) {
					*pixBuffer = 0xFFFFFF;
				}
			}
			analyzeImage(Time, pBuffer, BufferLen);
			for (int i = 0; i < balls.count; ++i) {
				if (balls.data[i].isObjectAcrossLine) {
					drawCross(balls.data[i].x, balls.data[i].y, 0x00, g_pBuffer); //black
				}
				else {
					drawCross(balls.data[i].x, balls.data[i].y, 0xFF0000, g_pBuffer); //red
				}
			}
			for (int i = 0; i < goalsBlue.count; ++i) {
				drawCross(goalsBlue.data[i].x, goalsBlue.data[i].y, 0x0000FF, g_pBuffer); //blue
			}
			for (int i = 0; i < goalsYellow.count; ++i) {
				drawCross(goalsYellow.data[i].x, goalsYellow.data[i].y, 0xFFFF00, g_pBuffer); //yellow
			}
			for (int i = 0; i < lines.count; ++i) {
				drawLine(lines.data[i].angle, lines.data[i].radius, 0xCC00CC, g_pBuffer); //purple
			}
			GdiFlush();
		}
		//if not calibrating, analyze each new image and draw crosses where the balls are at
		else if(g_pBuffer != NULL)
		{
			analyzeImage(Time, pBuffer, BufferLen);

			//prints(L"analyzed %d\n", balls.count);
			for (int i = 0; i < balls.count; ++i) {
				if (balls.data[i].isObjectAcrossLine) {
					drawCross(balls.data[i].x, balls.data[i].y, 0x00, g_pBuffer); //black
				}
				else {
					drawCross(balls.data[i].x, balls.data[i].y, 0xFF0000, g_pBuffer); //red
				}
			}
			for (int i = 0; i < goalsBlue.count; ++i) {
				drawCross(goalsBlue.data[i].x, goalsBlue.data[i].y, 0x0000FF, g_pBuffer); //blue
			}
			for (int i = 0; i < goalsYellow.count; ++i) {
				drawCross(goalsYellow.data[i].x, goalsYellow.data[i].y, 0xFFFF00, g_pBuffer); //yellow
			}
			for (int i = 0; i < lines.count; ++i) {
				drawLine(lines.data[i].angle, lines.data[i].radius, 0xCC00CC, g_pBuffer); //purple
			}
			//drawCross(320, 240, 0, g_pBuffer);
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

void drawRectangle(DWORD* pixBuffer, int x, int y, int width, int height, float angle, DWORD color) {
	for (int y2 = 0; y2 < height; ++y2) {
		for (int x2 = 0; x2 < width; ++x2) {
			int x3 = x + x2*cosf(angle) + y2*sinf(angle);
			int y3 = y + x2*sinf(angle) + y2*cosf(angle);
			if (x3 >= 0 && x3 < 640 && y3 >= 0 && y3 < 480) {
				pixBuffer[x3 + (640)*(y3)] = color;
			}
		}
	}
}


void analyzeTest() {
	ballsPixelCount = 1;
	ballColors.hueMin = 0.3, ballColors.hueMax = 0.6;
	ballColors.saturationMin = 0.3, ballColors.saturationMax = 0.6;
	ballColors.valueMin = 0.3, ballColors.valueMax = 0.6;

	pixelsTest = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 640 * 480 * 4);

	DWORD color = HSVtoRGB((ballColors.hueMin + ballColors.hueMax) / 2,
		(ballColors.saturationMin + ballColors.saturationMax) / 2,
		(ballColors.valueMin + ballColors.valueMax) / 2);

	wprintf(L"zero %X\n", pixelsTest[5+(640)*5]);
	drawRectangle(pixelsTest, 0, 0, 200, 200, 0 * PI/180.0, 0x0000FF);
	wprintf(L"zero %X\n", pixelsTest[0]);
	analyzeImage(0, (BYTE*)pixelsTest, 640 * 480 * 4);
	for (int i = 0; i < balls.count; ++i) {
		wprintf(L"ball: %d, x = %d, y = %d, pixelcount = %d\n", i + 1, balls.data[i].x, 
			balls.data[i].y, balls.data[i].pixelcount);
	}
}

DWORD WINAPI GUICamera(LPVOID lpParameter)
{
	//initialize structs and buffers:
	currentID = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 16);
	readFromFileColorThresholds();

	balls.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	balls.size = 128, balls.count = 0;
	goalsBlue.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goalsBlue.size = 128, goalsBlue.count = 0;
	goalsYellow.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goalsYellow.size = 128, goalsYellow.count = 0;
	lines.data = (lineInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(lineInfo) * 128);
	lines.size = 128, lines.count = 0;
	ballsShare.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	ballsShare.size = 128, ballsShare.count = 0;
	goalsBlueShare.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goalsBlueShare.size = 128, goalsBlueShare.count = 0;
	goalsYellowShare.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	goalsYellowShare.size = 128, goalsYellowShare.count = 0;
	linesShare.data = (lineInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(lineInfo) * 128);
	linesShare.size = 128, linesShare.count = 0;
	linesBlack.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	linesBlack.size = 128;
	linesWhite.data = (objectInfo*)HeapAlloc(GetProcessHeap(), NULL, sizeof(objectInfo) * 128);
	linesWhite.size = 128;

	pBufferCopy = (DWORD*)HeapAlloc(GetProcessHeap(), NULL, 640*480*4);
	houghTransformBuffer = (DWORD*)HeapAlloc(GetProcessHeap(), NULL, 150 * 150 * 4);
	activeColors = &ballColors;

	//isLineBetweenRobotAndBall(400, 50, (BYTE*)pBufferCopy);
	//Sleep(-1);

	//set up timers
	FPSTimer.start();

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

void pressedUP() {
	if (currentDrivingState.speed != keyboardSpeed) {
		currentDrivingState.speed = keyboardSpeed;
		setSpeedAngle(currentDrivingState);
	}
}
void releasedUP() {
	if (currentDrivingState.speed != 0) {
		currentDrivingState.speed = 0;
		setSpeedAngle(currentDrivingState);
	}
}
void pressedLeft() {
	if (currentDrivingState.angularVelocity != keyboardAngularVelocity) {
		currentDrivingState.angularVelocity = keyboardAngularVelocity;
		setSpeedAngle(currentDrivingState);
	}
}
void releasedLeft() {
	if (currentDrivingState.angularVelocity != 0) {
		currentDrivingState.angularVelocity = 0;
		setSpeedAngle(currentDrivingState);
	}
}
void pressedDown() {
	if (currentDrivingState.speed != -keyboardSpeed) {
		currentDrivingState.speed = -keyboardSpeed;
		setSpeedAngle(currentDrivingState);
	}
}
void releasedDown() {
	if (currentDrivingState.speed != 0) {
		currentDrivingState.speed = 0;
		setSpeedAngle(currentDrivingState);
	}
}
void pressedRight() {
	if (currentDrivingState.angularVelocity != -keyboardAngularVelocity) {
		currentDrivingState.angularVelocity = -keyboardAngularVelocity;
		setSpeedAngle(currentDrivingState);
	}
}
void releasedRight() {
	if (currentDrivingState.angularVelocity != 0) {
		currentDrivingState.angularVelocity = 0;
		setSpeedAngle(currentDrivingState);
	}
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
		SendMessage(hwndEdit, EM_SETLIMITTEXT, 10000000, 0);

		//buttons in the upper right
		HWND button1 = CreateWindowEx(0, L"BUTTON", L"START",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 0, 100, 20, hwnd, (HMENU)ID_BUTTON_START, hInstance, NULL);
		HWND button2 = CreateWindowEx(0, L"BUTTON", L"STOP",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 20, 100, 20, hwnd, (HMENU)ID_BUTTON_STOP, hInstance, NULL);
		HWND button3 = CreateWindowEx(0, L"BUTTON", L"CALIB",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 40, 100, 20, hwnd, (HMENU)ID_BUTTON_CALIBRATE, hInstance, NULL);
		HWND button4 = CreateWindowEx(0, L"BUTTON", L"CHARGE",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 60, 100, 20, hwnd, (HMENU)ID_BUTTON_CHARGE, hInstance, NULL);
		HWND button5 = CreateWindowEx(0, L"BUTTON", L"DISCHARGE",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 80, 100, 20, hwnd, (HMENU)ID_BUTTON_DISCHARGE, hInstance, NULL);
		HWND button6 = CreateWindowEx(0, L"BUTTON", L"KICK",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 100, 100, 20, hwnd, (HMENU)ID_BUTTON_KICK, hInstance, NULL);
		HWND button7 = CreateWindowEx(0, L"BUTTON", L"DRIB ON",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 120, 100, 20, hwnd, (HMENU)ID_BUTTON_DRIBBLER_ON, hInstance, NULL);
		HWND button8 = CreateWindowEx(0, L"BUTTON", L"DRIB OFF",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 640 + 640, 140, 100, 20, hwnd, (HMENU)ID_BUTTON_DRIBBLER_OFF, hInstance, NULL);

		stateStatusGUI = CreateWindowExW(0, L"STATIC", L"stopped",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 180, 110, 60, hwnd, (HMENU)ID_STATUS_TEXT, hInstance, NULL);
		statusFPS = CreateWindowExW(0, L"STATIC", L"FPS: 0.00",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 240, 110, 20, hwnd, (HMENU)ID_STATUS_FPS, hInstance, NULL);
		statusFPS2 = CreateWindowExW(0, L"STATIC", L"FPS2: 0.00",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 260, 110, 20, hwnd, (HMENU)ID_STATUS_FPS, hInstance, NULL);
		statusBall = CreateWindowExW(0, L"STATIC", L"Ball status",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 280, 110, 20, hwnd, (HMENU)ID_STATUS_BALL, hInstance, NULL);
		goalGuessState = CreateWindowExW(0, L"STATIC", L"Goal direction",
			WS_VISIBLE | WS_CHILD | SS_CENTER , 640 + 640, 300, 110, 20, hwnd, (HMENU)ID_STATUS_BALL, hInstance, NULL);

		CreateWindowExW(0, L"STATIC", L"Driving speed",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 320, 110, 20, hwnd, (HMENU)ID_STATUS_TEXT, hInstance, NULL);
		wchar_t tempBuffer[32] = {};
		hwndDrivingSpeed = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640+10, 340, 90, 20, hwnd, (HMENU)ID_SET_SPEED, hInstance, NULL);
		swprintf_s(tempBuffer, L"%.2f", keyboardSpeed);
		SetWindowTextW(hwndDrivingSpeed, tempBuffer);

		CreateWindowExW(0, L"STATIC", L"Angular velocity",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 365, 110, 20, hwnd, (HMENU)ID_STATUS_TEXT, hInstance, NULL);
		hwndAngularVelocity = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640+10, 385, 90, 20, hwnd, (HMENU)ID_SET_ANGULAR_VELOCITY, hInstance, NULL);
		swprintf_s(tempBuffer, L"%.0f", keyboardAngularVelocity);
		SetWindowTextW(hwndAngularVelocity, tempBuffer);

		infoGUI = CreateWindowExW(0, L"STATIC", L"DEBUG\n INFO",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 420, 110, 80, hwnd, (HMENU)ID_INFO, hInstance, NULL);

		infoGUI2 = CreateWindowExW(0, L"STATIC", L"DEBUG\n INFO 2",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 500, 110, 80, hwnd, (HMENU)ID_INFO2, hInstance, NULL);

		infoGUI3 = CreateWindowExW(0, L"STATIC", L"DEBUG\n INFO 3",
			WS_VISIBLE | WS_CHILD | SS_CENTER, 640 + 640, 580, 110, 80, hwnd, (HMENU)ID_INFO3, hInstance, NULL);

		SP = CreateWindowExW( //for testing pid constants
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640 , 620, 55, 20, hwnd, (HMENU)ID_SP, hInstance, NULL);
		SI = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640, 640, 55, 20, hwnd, (HMENU)ID_SI, hInstance, NULL);
		SD = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640, 660, 55, 20, hwnd, (HMENU)ID_SD, hInstance, NULL);
		AVP = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640 + 55, 620, 55, 20, hwnd, (HMENU)ID_AVP, hInstance, NULL);
		AVI = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640 + 55, 640, 55, 20, hwnd, (HMENU)ID_AVI, hInstance, NULL);
		AVD = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_CENTER,
			640 + 640 + 55, 660, 55, 20, hwnd, (HMENU)ID_AVD, hInstance, NULL);

		//set the proper window position
		RECT rc;
		rc.left = 0, rc.top = 0, rc.right = 640 + 640 + 65 + 45, rc.bottom = 680;
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
		setSpeedAngle(0, 0, 0);
		discharge();
		ShowWindow(hwndCalibrate, SW_HIDE);
		Release();
		PostQuitMessage(0);
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_BUTTON_START:
			ResetEvent(stopSignal);
			SetEvent(startSignal);
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_STOP:
			ResetEvent(startSignal);
			SetEvent(stopSignal);
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_CALIBRATE:
			ShowWindow(hwndCalibrate, SW_SHOW);
			calibrating = TRUE;
			ResetEvent(calibratingEndSignal);
			SetEvent(calibratingSignal);
			SetFocus(hwndCalibrate);
			return 0;
		case ID_BUTTON_CHARGE:
			charge();
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_KICK:
			kick();
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_DISCHARGE:
			discharge();
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_DRIBBLER_ON:
			dribblerON();
			SetFocus(hwndMain);
			return 0;
		case ID_BUTTON_DRIBBLER_OFF:
			dribblerOFF();
			SetFocus(hwndMain);
			return 0;
		case ID_SET_SPEED:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndDrivingSpeed, buffer, 15);
				keyboardSpeed = _wtof(buffer);
				prints(L"keyboardSpeed: %.2f\n", keyboardSpeed);
				return 0;
			default:
				break;
			}
		case ID_SET_ANGULAR_VELOCITY:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndAngularVelocity, buffer, 15);
				keyboardAngularVelocity = _wtof(buffer);
				prints(L"keyboardAngularVelocity: %.2f\n", keyboardAngularVelocity);
				return 0;
			default:
				break;
			}
		case ID_SP:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(SP, buffer, 15);
				speedP = _wtof(buffer);
				return 0;
			default:
				break;
			}
		case ID_SI:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(SI, buffer, 15);
				speedI = _wtof(buffer);
				return 0;
			default:
				break;
			}
		case ID_SD:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(SD, buffer, 15);
				speedD = _wtof(buffer);
				return 0;
			default:
				break;
			}
		case ID_AVP:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(AVP, buffer, 15);
				angularVelocityP = _wtof(buffer);
				return 0;
			default:
				break;
			}
		case ID_AVI:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(AVI, buffer, 15);
				angularVelocityI = _wtof(buffer);
				return 0;
			default:
				break;
			}
		case ID_AVD:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(AVD, buffer, 15);
				angularVelocityD = _wtof(buffer);
				return 0;
			default:
				break;
			}
		}
		break;
	case WM_KEYDOWN: //drive the robot when the calibrating window is open and arrow keys are pressed
		switch (wParam) {
		case VK_UP:
			pressedUP();
			return 0;
		case VK_DOWN:
			pressedDown();
			return 0;
		case VK_LEFT:
			pressedLeft();
			return 0;
		case VK_RIGHT:
			pressedRight();
			return 0;
		}
		break;
	case WM_KEYUP:
		switch (wParam) {
		case VK_UP:
			releasedUP();
			break;
		case VK_DOWN:
			releasedDown();
			break;
		case VK_LEFT:
			releasedLeft();
			break;
		case VK_RIGHT:
			releasedRight();
			break;
		}
		break;
	case WM_LBUTTONDOWN:
		SetFocus(hwndMain);
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

	//pDisplay->RepaintVideo();

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

	if (activeColors->exclude) {
		SendMessage(excludeCheckBox, BM_SETCHECK, BST_CHECKED, 0);
	}
	else {
		SendMessage(excludeCheckBox, BM_SETCHECK, BST_UNCHECKED, 0);
	}

	RECT rc{ 120,340,280,420 };
	InvalidateRect(hwndCalibrate, &rc, FALSE); //redraw the rectangle with the current color
	rc = { 0,0,150,250 };
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
		rc.left = 0, rc.top = 0, rc.right = 400, rc.bottom = 520;
		AdjustWindowRectEx(&rc, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
		SetWindowPos(hwnd, NULL, 300, 100, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
		HINSTANCE hInstance = GetModuleHandle(0);

		//the selections to calibrate colors for the balls, the goals or the lines
		CreateWindowEx(0, L"BUTTON", L"Select object to calibrate:",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
			10, 10, 300, 80, hwnd, (HMENU)ID_RADIOBOXGROUP_OBJECTSELECTOR, hInstance, NULL);
		ballsRadioButton = CreateWindowEx(0, L"BUTTON", L"Balls",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
			10 + 10, 25, 70, 30, hwnd, (HMENU)ID_RADIOBOX_BALLS, hInstance, NULL);
		if (ballColors.active) {
			activeColors = &ballColors;
			SendMessage(ballsRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		goalsBlueRadioButton = CreateWindowEx(0, L"BUTTON", L"GoalsB",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 70, 25, 80, 30, hwnd, (HMENU)ID_RADIOBOX_GOALSBLUE, hInstance, NULL);
		if (goalBlueColors.active) {
			activeColors = &goalBlueColors;
			SendMessage(goalsBlueRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		goalsYellowRadioButton = CreateWindowEx(0, L"BUTTON", L"GoalsY",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 150, 25, 80, 30, hwnd, (HMENU)ID_RADIOBOX_GOALSYELLOW, hInstance, NULL);
		if (goalYellowColors.active) {
			activeColors = &goalYellowColors;
			SendMessage(goalsYellowRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		linesBlackRadioButton = CreateWindowEx(0, L"BUTTON", L"LinesB",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10, 55, 70, 30, hwnd, (HMENU)ID_RADIOBOX_LINES_BLACK, hInstance, NULL);
		if (lineBlackColors.active) {
			activeColors = &lineBlackColors;
			SendMessage(linesBlackRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		linesWhiteRadioButton = CreateWindowEx(0, L"BUTTON", L"LinesW",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 70, 55, 70, 30, hwnd, (HMENU)ID_RADIOBOX_LINES_WHITE, hInstance, NULL);
		if (lineWhiteColors.active) {
			activeColors = &lineWhiteColors;
			SendMessage(linesWhiteRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		fieldGreenRadioButton = CreateWindowEx(0, L"BUTTON", L"FieldG",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10 + 150, 55, 70, 30, hwnd, (HMENU)ID_RADIOBOX_FIELD_GREEN, hInstance, NULL);
		if (fieldGreenColors.active) {
			activeColors = &fieldGreenColors;
			SendMessage(fieldGreenRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		}
		whitenCheckBox = CreateWindowEx(0, L"BUTTON", L"Whiten?",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10 + 10 + 300, 25, 80, 30, hwnd, (HMENU)ID_CHECKBOX_WHITEN, hInstance, NULL);
		if (whitenThresholdPixels) {
			SendMessage(whitenCheckBox, BM_SETCHECK, BST_CHECKED, 0);
		}
		prints(L"whiten %d\n", whitenThresholdPixels);
		excludeCheckBox = CreateWindowEx(0, L"BUTTON", L"Exclude?",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10 + 10 + 300, 55, 80, 30, hwnd, (HMENU)ID_CHECKBOX_EXCLUDE, hInstance, NULL);
		if (activeColors->exclude) {
			SendMessage(excludeCheckBox, BM_SETCHECK, BST_CHECKED, 0);
		}
		else {
			SendMessage(excludeCheckBox, BM_SETCHECK, BST_UNCHECKED, 0);
		}

		//boxes for the pixelcount thresholds of various objects
		wchar_t tempBuffer[32] = {};
		hwndBallsPixelCount = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT,
			10 + 10, 95, 60, 25, hwnd, (HMENU)ID_BALLS_PIXELCOUNT, hInstance, NULL);
		SendMessage(hwndBallsPixelCount, EM_SETLIMITTEXT, 6, 0);
		swprintf_s(tempBuffer, L"%d", ballsPixelCount);
		SetWindowTextW(hwndBallsPixelCount, tempBuffer);
		hwndGoalsBluePixelCount = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT,
			10 + 10 + 75, 95, 60, 25, hwnd, (HMENU)ID_GOALSBLUE_PIXELCOUNT, hInstance, NULL);
		SendMessage(hwndGoalsBluePixelCount, EM_SETLIMITTEXT, 6, 0);
		swprintf_s(tempBuffer, L"%d", goalsBluePixelCount);
		SetWindowTextW(hwndGoalsBluePixelCount, tempBuffer);
		hwndGoalsYellowPixelCount = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT,
			10 + 10 + 150, 95, 60, 25, hwnd, (HMENU)ID_GOALSYELLOW_PIXELCOUNT, hInstance, NULL);
		SendMessage(hwndGoalsYellowPixelCount, EM_SETLIMITTEXT, 6, 0);
		swprintf_s(tempBuffer, L"%d", goalsYellowPixelCount);
		SetWindowTextW(hwndGoalsYellowPixelCount, tempBuffer);
		hwndLinesPixelCount = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT,
			10 + 10 + 225, 95, 60, 25, hwnd, (HMENU)ID_LINES_PIXELCOUNT, hInstance, NULL);
		SendMessage(hwndLinesPixelCount, EM_SETLIMITTEXT, 6, 0);
		swprintf_s(tempBuffer, L"%d", linesPixelCount);
		SetWindowTextW(hwndLinesPixelCount, tempBuffer);

		//the sliders
		hwndHue = CreateWindowEx(0, TRACKBAR_CLASS, L"Hue", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 140, 250, 20, hwnd, (HMENU)ID_TRACKBAR_HUE, hInstance, 0);
		activeColors->hue = activeColors->hueMin;
		SendMessage(hwndHue, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndHue, TBM_SETPOS, TRUE, (int)(activeColors->hueMin * 1000 / 6));
		SendMessage(hwndHue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndHue, TBM_SETSELSTART, TRUE, (int)(activeColors->hueMin * 1000 / 6));
		SendMessage(hwndHue, TBM_SETSELEND, TRUE, (int)(activeColors->hueMax * 1000 / 6));
		hwndSaturation = CreateWindowEx(0, TRACKBAR_CLASS, L"Saturation", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 180, 250, 20, hwnd, (HMENU)ID_TRACKBAR_SATURATION, hInstance, 0);
		activeColors->saturation = activeColors->saturationMin;
		SendMessage(hwndSaturation, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndSaturation, TBM_SETPOS, TRUE, (int)(activeColors->saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndSaturation, TBM_SETSELSTART, TRUE, (int)(activeColors->saturationMin * 1000));
		SendMessage(hwndSaturation, TBM_SETSELEND, TRUE, (int)(activeColors->saturationMax * 1000));
		hwndValue = CreateWindowEx(0, TRACKBAR_CLASS, L"Value", WS_VISIBLE | WS_CHILD | TBS_ENABLESELRANGE,
			150, 220, 250, 20, hwnd, (HMENU)ID_TRACKBAR_VALUE, hInstance, 0);
		activeColors->value = activeColors->valueMin;
		SendMessage(hwndValue, TBM_SETRANGE, TRUE, (1000 << 16));
		SendMessage(hwndValue, TBM_SETPOS, TRUE, (int)(activeColors->valueMin * 1000));
		SendMessage(hwndValue, TBM_SETPAGESIZE, 0, 1);
		SendMessage(hwndValue, TBM_SETSELSTART, TRUE, (int)(activeColors->valueMin * 1000));
		SendMessage(hwndValue, TBM_SETSELEND, TRUE, (int)(activeColors->valueMax * 1000));

		//the selections for minimum or maximum
		CreateWindowEx(0, L"BUTTON", L"Set thresholds for the desired color:",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX | WS_GROUP,
			10, 245, 385, 90, hwnd, (HMENU)ID_RADIOBOXGROUP_MINMAX, hInstance, NULL);
		minRadioButton = CreateWindowEx(0, L"BUTTON", L"Minimum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10, 245 + 20, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MIN, hInstance, NULL);
		if(currentCalibratorSetting == minimum)
			SendMessage(minRadioButton, BM_SETCHECK, BST_CHECKED, 0);
		maxRadioButton = CreateWindowEx(0, L"BUTTON", L"Maximum values",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
			10 + 10, 245 + 50, 150, 30, hwnd, (HMENU)ID_RADIOBOX_MAX, hInstance, NULL);
		if (currentCalibratorSetting == maximum)
			SendMessage(maxRadioButton, BM_SETCHECK, BST_CHECKED, 0);

		//the radio data
		radioCheckBox = CreateWindowEx(0, L"BUTTON", L"Radio?  ID:",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10 + 10 , 430, 90, 30, hwnd, (HMENU)ID_CHECKBOX_RADIO, hInstance, NULL);
		if (listenToRadio)
			SendMessage(radioCheckBox, BM_SETCHECK, BST_CHECKED, 0);
		hwndEditID = CreateWindowExW(
			0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT,
			10 + 10 + 95, 435, 80, 25, hwnd, (HMENU)ID_EDIT_ID, hInstance, NULL);
		SendMessage(hwndEditID, EM_SETLIMITTEXT, 2, 0);
		SetWindowTextA(hwndEditID, currentID);

		goalSelectCheckBox = CreateWindowEx(0, L"BUTTON", L"Attack blue?",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			10 + 10 + 200, 430, 90, 30, hwnd, (HMENU)ID_GOAL_SELECTION, hInstance, NULL);
		if (attackBlue) {
			SendMessage(goalSelectCheckBox, BM_SETCHECK, BST_CHECKED, 0);
		}


		//the buttons
		CreateWindowEx(0, L"BUTTON", L"DONE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			50, 480, 100, 30, hwnd, (HMENU)ID_BUTTON_DONE, hInstance, NULL);
		CreateWindowEx(0, L"BUTTON", L"SAVE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			150, 480, 100, 30, hwnd, (HMENU)ID_BUTTON_SAVE, hInstance, NULL);
		CreateWindowEx(0, L"BUTTON", L"RESET", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			250, 480, 100, 30, hwnd, (HMENU)ID_BUTTON_RESET, hInstance, NULL);
		
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
		//ResetEvent(calibratingSignal);
		//SetEvent(startSignal);
		return 0;
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		calibrating = FALSE;
		ResetEvent(calibratingSignal);
		SetEvent(calibratingEndSignal);
		SetFocus(hwndMain);
		return 0;
	case WM_COMMAND: //if a button is pressed
		switch (LOWORD(wParam)) {
		case ID_RADIOBOX_BALLS:
			activeColors->active = false;
			activeColors = &ballColors;
			activeColors->active = true;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_GOALSBLUE:
			activeColors->active = false;
			activeColors = &goalBlueColors;
			activeColors->active = true;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_GOALSYELLOW:
			activeColors->active = false;
			activeColors = &goalYellowColors;
			activeColors->active = true;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_LINES_BLACK:
			activeColors->active = false;
			activeColors = &lineBlackColors;
			activeColors->active = true;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_LINES_WHITE:
			activeColors->active = false;
			activeColors = &lineWhiteColors;
			activeColors->active = true;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);
			return 0;
		case ID_RADIOBOX_FIELD_GREEN:
			activeColors->active = false;
			activeColors = &fieldGreenColors;
			activeColors->active =true;
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
		case ID_CHECKBOX_EXCLUDE:
			if (SendMessage(excludeCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
				activeColors->exclude = true;
			}
			else {
				activeColors->exclude = false;
			}
			return 0;
		case ID_BUTTON_DONE:
			//ShowWindow(hwnd, SW_HIDE);
			//calibrating = FALSE;
			SendMessage(hwndCalibrate, WM_CLOSE, 0, 0);
			return 0;
		case ID_BUTTON_SAVE:
			saveToFileColorThresholds();
			return 0;
		case ID_BUTTON_RESET:
			activeColors->hueMin = 0, activeColors->hueMax = 6;
			
			activeColors->saturationMin = 0, activeColors->saturationMax = 1;
			activeColors->valueMin = 0, activeColors->valueMax = 1;
			if (currentCalibratorSetting == minimum) {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
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
		case ID_CHECKBOX_RADIO:
			if (SendMessage(radioCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
				listenToRadio = 1;
			}
			else {
				listenToRadio = 0;
			}
			return 0;
		case ID_GOAL_SELECTION:
			if (SendMessage(goalSelectCheckBox, BM_GETCHECK, 0, 0) == BST_CHECKED) {
				attackBlue = 1;
			}
			else {
				attackBlue = 0;
			}
			return 0;
		case ID_EDIT_ID:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[4];
				int charsConverted;
				GetWindowTextW(hwndEditID, buffer, 4);
				wcstombs_s((size_t*)&charsConverted, currentID, 4, buffer, 3); //convert to char from wchar_t
				for (int i = GetWindowTextLengthW(hwndEditID); i < 4; ++i)
					currentID[i] = 0;
				return 0;
			default:
				break;
			}
		case ID_BALLS_PIXELCOUNT:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndBallsPixelCount, buffer, 15);
				ballsPixelCount = _wtoi(buffer);
				return 0;
			default:
				break;
			}
		case ID_GOALSBLUE_PIXELCOUNT:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndGoalsBluePixelCount, buffer, 15);
				goalsBluePixelCount = _wtoi(buffer);
				return 0;
			default:
				break;
			}
		case ID_GOALSYELLOW_PIXELCOUNT:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndGoalsYellowPixelCount, buffer, 15);
				goalsYellowPixelCount = _wtoi(buffer);
				return 0;
			default:
				break;
			}
		case ID_LINES_PIXELCOUNT:
			switch (HIWORD(wParam)) {
			case EN_CHANGE:
				wchar_t buffer[16];
				int charsConverted;
				GetWindowTextW(hwndLinesPixelCount, buffer, 15);
				linesPixelCount = _wtoi(buffer);
				return 0;
			default:
				break;
			}
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
	case WM_KEYDOWN: //drive the robot when the calibrating window is open and arrow keys are pressed
		switch (wParam) {
		case VK_UP:
			pressedUP();
			return 0;
		case VK_DOWN:
			pressedDown();
			return 0;
		case VK_LEFT:
			pressedLeft();
			return 0;
		case VK_RIGHT:
			pressedRight();
			return 0;
		}
		break;
	case WM_KEYUP:
		switch (wParam) {
		case VK_UP:
			releasedUP();
			break;
		case VK_DOWN:
			releasedDown();
			break;
		case VK_LEFT:
			releasedLeft();
			break;
		case VK_RIGHT:
			releasedRight();
			break;
		}
		break;
	case WM_LBUTTONDOWN: //switch the min-max mode of the calibration when the mouse is clicked on the window
		SetFocus(hwndCalibrate);
		if (currentCalibratorSetting == minimum) {
			SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			SendMessage(maxRadioButton, BM_SETCHECK, BST_CHECKED, 0);
			SendMessage(minRadioButton, BM_SETCHECK, BST_UNCHECKED, 0);
		}
		else {
			SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			SendMessage(minRadioButton, BM_SETCHECK, BST_CHECKED, 0);
			SendMessage(maxRadioButton, BM_SETCHECK, BST_UNCHECKED, 0);
		}
		break;
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void OnPaintCalibrator(HWND hwnd) {
	wchar_t buf[24];
	int len = 0;
	RECT rc;
	GetClientRect(hwnd, &rc);
	RECT rc2 = { 120,340,280,420 };		//rectangle where the current color is drawn
	PAINTSTRUCT ps;
	HDC hdc;
	hdc = BeginPaint(hwnd, &ps);

	//ExcludeClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);
	FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW));
	//IntersectClipRect(hdc, rc2.left, rc2.top, rc2.right, rc2.bottom);

	SetBkMode(hdc, TRANSPARENT); //for text printing, doesn't draw a box around the text

	TextOutW(hdc, 10, 140, buf, swprintf(buf, 20, L"Hue:        %.4f", activeColors->hue));
	TextOutW(hdc, 10, 180, buf, swprintf(buf, 20, L"Saturation: %.4f", activeColors->saturation));
	TextOutW(hdc, 10, 220, buf, swprintf(buf, 20, L"Value:     %.4f", activeColors->value));

	//HRGN hrgn = CreateRectRgn(120, 250, 280, 330);
	//SelectClipRgn(hdc, hrgn);
	DWORD color = HSVtoRGB(activeColors->hue, activeColors->saturation, activeColors->value);
	HBRUSH rectBrush = CreateSolidBrush( (color>>16)&0xFF | (color)&0xFF00 | (color<<16)&0xFF0000 );
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
		prints(L"Camera initialized\r\n\r\n");
		return 0;
	case WM_PAINT:
		pDisplay->RepaintVideo();
		break;
	case WM_GRAPH_EVENT:
		OnGraphEvent(hwnd);
		return 0;
	case WM_LBUTTONDOWN:
		POINTS click = MAKEPOINTS(lParam);
		int x = click.x;

		int y = 480 - click.y;
		prints(L"x: %d, y: %d\n", x, y);

		if (calibrating) { //if calibrating, choose the HSV thresholds by a mouse click on the left video
			int colors = ((DWORD*)DShowBuffer)[x + y * 640];
			int blue = (colors & 0xFF),  green = (colors & 0xFF00)>>8, red = (colors & 0xFF0000) >> 16;
			float hue = HUE(red, green, blue), saturation = SATURATION(red, green, blue), value = VALUE(red, green, blue);
			float halfWidth = 0.1; //the factor by which to either side the sliders are moved
			activeColors->hueMax = hue + 6 * halfWidth > 6 ? 6 : hue + 6 * halfWidth;
			activeColors->hueMin = hue - 6 * halfWidth < 0 ? 0 : hue - 6 * halfWidth;
			activeColors->saturationMax = saturation + 1 * halfWidth > 1 ? 1 : saturation + 1 * halfWidth;
			activeColors->saturationMin = saturation - 1 * halfWidth < 0 ? 0 : saturation - 1 * halfWidth;
			activeColors->valueMax = value + 1 * halfWidth > 1 ? 1 : value + 1 * halfWidth;
			activeColors->valueMin = value - 1 * halfWidth < 0 ? 0 : value - 1 * halfWidth;
			
			if (currentCalibratorSetting == minimum) { //set the sliders to the new values
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MIN, 0);
			}
			else {
				SendMessage(hwndCalibrate, WM_COMMAND, ID_RADIOBOX_MAX, 0);
			}
			setSlidersToValues(activeColors);

			prints(L"clicked on red %d green %d blue %d, x: %d, y: %d\n", red, green, blue, x, y);
		}
		SetFocus(hwndMain);
		break;
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
	int chose = 0;
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
						if (!chose) {
							VariantClear(&varName);
							hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pCap);
							if (SUCCEEDED(hr))
							{
								hr = pGraph->AddFilter(pCap, L"Capture Filter");
								chosen = varName.bstrVal;
								pMoniker->Release();
								chose = 1;
							}
							else {
								prints(L"camera error");
							}
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
		        //prints(L"Width: %d, Height: %d, BitCount: %d, Compression: %X\n",
		        //       videoInfoHeader->bmiHeader.biWidth,videoInfoHeader->bmiHeader.biHeight,
		        //       videoInfoHeader->bmiHeader.biBitCount,videoInfoHeader->bmiHeader.biCompression);
		        //prints(L"Image size: %d, Bitrate KB: %.2f, FPS: %.2f\n",
		        //       videoInfoHeader->bmiHeader.biSizeImage,videoInfoHeader->dwBitRate/(8.0*1000),
		        //       10000000.0/(videoInfoHeader->AvgTimePerFrame));
		if (videoInfoHeader->bmiHeader.biWidth == 640 &&
			videoInfoHeader->bmiHeader.biBitCount >= 16) {
			prints(L"Chosen image format:\n");
			prints(L"Width: %d, Height: %d, BitCount: %d, Compression: %X\n",
				videoInfoHeader->bmiHeader.biWidth,videoInfoHeader->bmiHeader.biHeight,
			    videoInfoHeader->bmiHeader.biBitCount,videoInfoHeader->bmiHeader.biCompression);
			prints(L"Image size: %d, Bitrate KB: %.2f, FPS: %.2f\n",
				videoInfoHeader->bmiHeader.biSizeImage,videoInfoHeader->dwBitRate/(8.0*1000),
				10000000.0/(videoInfoHeader->AvgTimePerFrame));
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
			prints(L"GRAPH EVENT ERROR\n");
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
	return (int)blue + ((int)green << 8) + ((int)red << 16);
}

void saveToFileColorThresholds() {
	HANDLE dataFile = CreateFile(L"data.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD numberOfBytesRead;
	WriteFile(dataFile, &ballColors, sizeof(ballColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &goalBlueColors, sizeof(goalBlueColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &goalYellowColors, sizeof(goalYellowColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &lineBlackColors, sizeof(lineBlackColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &lineWhiteColors, sizeof(lineWhiteColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &fieldGreenColors, sizeof(fieldGreenColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &currentCalibratorSetting, sizeof(currentCalibratorSetting), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &activeColors, sizeof(activeColors), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &whitenThresholdPixels, sizeof(whitenThresholdPixels), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &listenToRadio, sizeof(listenToRadio), &numberOfBytesRead, NULL);
	WriteFile(dataFile, currentID, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &ballsPixelCount, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &goalsBluePixelCount, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &goalsYellowPixelCount, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &linesPixelCount, 4, &numberOfBytesRead, NULL);
	WriteFile(dataFile, &attackBlue, sizeof(attackBlue), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &keyboardSpeed, sizeof(keyboardSpeed), &numberOfBytesRead, NULL);
	WriteFile(dataFile, &keyboardAngularVelocity, sizeof(keyboardAngularVelocity), &numberOfBytesRead, NULL);
	
	CloseHandle(dataFile);
}

void readFromFileColorThresholds() {
	HANDLE dataFile = CreateFile(L"data.txt", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD numberOfBytesRead;
	ReadFile(dataFile, &ballColors, sizeof(ballColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &goalBlueColors, sizeof(goalBlueColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &goalYellowColors, sizeof(goalYellowColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &lineBlackColors, sizeof(lineBlackColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &lineWhiteColors, sizeof(lineWhiteColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &fieldGreenColors, sizeof(fieldGreenColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &currentCalibratorSetting, sizeof(currentCalibratorSetting), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &activeColors, sizeof(activeColors), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &whitenThresholdPixels, sizeof(whitenThresholdPixels), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &listenToRadio, sizeof(listenToRadio), &numberOfBytesRead, NULL);
	ReadFile(dataFile, currentID, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &ballsPixelCount, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &goalsBluePixelCount, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &goalsYellowPixelCount, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &linesPixelCount, 4, &numberOfBytesRead, NULL);
	ReadFile(dataFile, &attackBlue, sizeof(attackBlue), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &keyboardSpeed, sizeof(keyboardSpeed), &numberOfBytesRead, NULL);
	ReadFile(dataFile, &keyboardAngularVelocity, sizeof(keyboardAngularVelocity), &numberOfBytesRead, NULL);

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