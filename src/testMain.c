
#pragma config(Sensor, in1,    leftLight,      sensorReflection)
#pragma config(Sensor, in2,    midLight,       sensorReflection)
#pragma config(Sensor, in3,    rightLight,     sensorReflection)
#pragma config(Sensor, in4,    wristPoti,      sensorPotentiometer)
#pragma config(Sensor, in6,    gyro,           sensorGyro)
#pragma config(Sensor, dgtl1,  rightEnc,       sensorQuadEncoder)
#pragma config(Sensor, dgtl3,  leftEnc,        sensorQuadEncoder)
#pragma config(Sensor, dgtl5,  scissorEnc,     sensorQuadEncoder)
#pragma config(Sensor, dgtl8,  Sonar,          sensorSONAR_cm)
#pragma config(Motor,  port1,           frontRight,    tmotorVex393TurboSpeed_HBridge, openLoop, reversed)
#pragma config(Motor,  port2,           backRight,     tmotorVex393TurboSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port3,           frontLeft,     tmotorVex393TurboSpeed_MC29, openLoop)
#pragma config(Motor,  port4,           backLeft,      tmotorVex393TurboSpeed_MC29, openLoop)
#pragma config(Motor,  port6,           claw,          tmotorVex393TurboSpeed_MC29, openLoop)
#pragma config(Motor,  port8,           scissorLeft,   tmotorVex393HighSpeed_MC29, openLoop)
#pragma config(Motor,  port9,           scissorRight,  tmotorVex393HighSpeed_MC29, openLoop)
#pragma config(Motor,  port10,          wrist,         tmotorVex393TurboSpeed_HBridge, openLoop)
//*!!Code automatically generated by 'ROBOTC' configuration wizard               !!*//

#define IGNORE_DISABLE

#include "Vex_Competition_Includes_Custom.c"

// Library headers
#include "task.h"
#include "async.h"
#include "motors.h"
#include "sensors.h"
#include "joysticks.h"
#include "cycle.h"
#include "utilities.h"
#include "pid.h"
#include "notify.h"

// Library source
#include "task.c"
#include "async.c"
#include "motors.c"
#include "sensors.c"
#include "joysticks.c"
#include "cycle.c"
#include "utilities.c"
#include "pid.c"
#include "notify.c"

// Controls
#include "controls.h"

unsigned long gOverAllTime;
sCycleData gMainCycle;

bool gKillDriveOnTimeout = false;

int lightVal;
int lightThresh;
int lightMin = 2000;
int lightMax = 0;
int scissorBottom = -180;
int scissorTop = 1400;

///////////////////

void setDrive (word left, word right)
{
	gMotor[frontRight].power = gMotor[backRight].power = right;
	gMotor[frontLeft].power = gMotor[backLeft].power = left;
}

typedef enum _sLiftStates
{
	liftIdle,
	liftRaise,
	liftLower
}sLiftStates;

#define LIFT_TOP 1475
#define LIFT_BOTTOM 0
int gLiftPositions[] = {0, 265, 1475};

#define LIFT_RAISING_POWER 127
#define LIFT_LOWERING_POWER -127

sLiftStates gLiftState = liftIdle;

void setLift(word speed)
{
	gMotor[scissorLeft].power = gMotor[scissorRight].power = speed;
}

//void handleLift ()
//{
//	switch (gLiftState)
//	{
//	case liftIdle:
//		{
//			setLift(0);
//			break;
//		}
//	case liftRaise:
//		{
//			setLift(LIFT_RAISING_POWER);
//			break;
//		}
//	case liftLower:
//		{
//			setLift(LIFT_LOWERING_POWER);
//			break;
//		}
//	}
//}

int gLiftPos = 0;

void liftToPos (int target, int offset = 0)
{
	if (gLiftPos < target)
	{
		writeDebugStreamLine("Lift Raising");
		setLift(LIFT_RAISING_POWER);
		while (gSensor[scissorEnc].value < gLiftPositions[target] + offset) sleep(10);
	}
	else if (gLiftPos > target)
	{
		writeDebugStreamLine("Lift Lowering");
		setLift(LIFT_LOWERING_POWER);
		while (gSensor[scissorEnc].value > gLiftPositions[target] + offset) sleep(10);
	}
	writeDebugStreamLine ("Lift pos: %d", gSensor[scissorEnc].value);
	setLift(0);
	gLiftPos = target;
}

typedef enum _sClawStates
{
	clawIdle,
	clawOpen,
	clawClose
} sClawStates;

#define CLAW_CLOSE_POWER 127
#define CLAW_OPEN_POWER -127
#define CLAW_WAIT 200

sClawStates gClawState = clawIdle;

void setClaw (word speed)
{
	gMotor[claw].power = speed;
}

void openClaw (bool open)
{
	//switch(gClawState)
	//{
	//case clawIdle:
	//	{
	//		setClaw(0);
	//		break;
	//	}
	//case clawOpen:
	//	{
	//		setClaw(CLAW_OPEN_POWER);
	//		wait1Msec(CLAW_WAIT);
	//		gClawState = clawIdle;
	//		break;
	//	}
	//case clawClose:
	//	{
	//		setClaw(CLAW_CLOSE_POWER);
	//		wait1Msec(CLAW_WAIT);
	//		gClawState = clawIdle;
	//		break;
	//	}
	//}

	setClaw( open ? CLAW_OPEN_POWER : CLAW_CLOSE_POWER );
	wait1Msec(CLAW_WAIT);
	setClaw(0);
}

bool TimedOut(unsigned long timeOut, const string description)
{
	if (nPgmTime > timeOut)
	{
		hogCPU();
		// TODO: kill everything except drive
		if (gKillDriveOnTimeout) setDrive(0, 0);
		updateMotors();
		writeDebugStreamLine("%06d EXCEEDED TIME %d - %s", nPgmTime - gOverAllTime, timeOut - gOverAllTime, description);
		int current = nCurrentTask;
		while (true)
		{
			int next = tEls[current].parent;
			if (next == -1 || next == usercontrol) break;
			current = next;
		}
		tStopAll(current);
		return true;
	}
	else
		return false;
}

//update states
//task updateStates()
//{
//	sCycleData cycle;
//	initCycle(cycle, 10, "update states");

//	while(true)
//	{
//		//handleLift();
//		//handleClaw();
//		endCycle(cycle);
//	}
//}

// Auto
#include "auto.h"
#include "auto_simple.h"
#include "auto_runs.h"

#include "auto.c"
#include "auto_simple.c"
#include "auto_runs.c"

// This function gets called 2 seconds after power on of the cortex and is the first bit of code that is run
void startup()
{
	clearDebugStream();
	writeDebugStreamLine("Code start");

	// Setup and initilize the necessary libraries
	setupMotors();
	setupSensors();
	setupJoysticks();
	tInit();

	gJoy[JOY_TURN].deadzone = DZ_TURN;
	gJoy[JOY_THROTTLE].deadzone = DZ_THROTTLE;
	gJoy[JOY_LIFT].deadzone = DZ_LIFT;
	gJoy[JOY_ARM].deadzone = DZ_ARM;

	gSensor[rightEnc].scale = -1; // set encoder to reversed
}

// This function gets called every 25ms during disabled (DO NOT PUT BLOCKING CODE IN HERE)
void disabled()
{
	displayLCDCenteredString(0, "DISABLED");
}

// This task gets started at the begining of the autonomous period
task autonomous()
{
	//tStart(updateStates);

	gAutoTime = nPgmTime;
	writeDebugStreamLine("Auto start %d", gAutoTime);
	displayLCDCenteredString(0, "AUTO");

	startSensors(); // Initilize the sensors

	gKillDriveOnTimeout = true;

	resetPosition(gPosition);
	tStart(autoMotorSensorUpdateTask);
	tStart(trackPositionTask);
	////////////////////////////
	runAuto();

	writeDebugStreamLine("Auto: %d ms", nPgmTime - gAutoTime);

	return_t;
}

task usercontrol()
{
	startSensors(); // Initilize the sensors
	initCycle(gMainCycle, 20, "main");

	gKillDriveOnTimeout = false;

	while (true)
	{
		updateSensorInputs();
		updateJoysticks();

		// TODO: put your main drive code here

		updateSensorOutputs();
		updateMotors();
		endCycle(gMainCycle);
	}

	return_t;
}
