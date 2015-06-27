#include "stdafx.h"
#include "GUICamera.h"
#include <Windows.h>

HANDLE signal = CreateEvent(NULL, TRUE, FALSE, NULL);
HANDLE GUIThread;

int main() {
	//Create the GUI in a separate thread
	GUIThread = CreateThread(NULL, 0, GUICamera, 0, 0, NULL);
	//Wait for the GUI to initialize
	WaitForSingleObject(signal,INFINITE);

	//TODO control the robot...
	prints(L"Testing printing\n");

	//Don't exit this thread before the GUI
	WaitForSingleObject(GUIThread, INFINITE);
}