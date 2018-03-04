#pragma config(Sensor, in1,    autoPoti,       sensorPotentiometer)
#pragma config(Sensor, in2,    mobilePoti,     sensorPotentiometer)
#pragma config(Sensor, in3,    liftPoti,       sensorPotentiometer)
#pragma config(Sensor, in4,    armPoti,        sensorPotentiometer)
#pragma config(Sensor, in5,    expander,       sensorAnalog)
#pragma config(Sensor, in6,    lsBarL,         sensorReflection)
#pragma config(Sensor, in7,    lsBarR,         sensorReflection)
#pragma config(Sensor, dgtl1,  trackL,         sensorQuadEncoder)
#pragma config(Sensor, dgtl3,  trackR,         sensorQuadEncoder)
#pragma config(Sensor, dgtl5,  trackB,         sensorQuadEncoder)
#pragma config(Sensor, dgtl7,  sonarL,         sensorSONAR_mm)
#pragma config(Sensor, dgtl9,  limMobile,      sensorTouch)
#pragma config(Sensor, dgtl10, jmpSkills,      sensorDigitalIn)
#pragma config(Sensor, dgtl11, sonarR,         sensorSONAR_mm)
#pragma config(Motor,  port2,           liftL,         tmotorVex393HighSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port3,           driveL1,       tmotorVex393TurboSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port4,           driveL2,       tmotorVex393TurboSpeed_MC29, openLoop)
#pragma config(Motor,  port5,           arm,           tmotorVex393HighSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port6,           mobile,        tmotorVex393HighSpeed_MC29, openLoop)
#pragma config(Motor,  port7,           driveR2,       tmotorVex393TurboSpeed_MC29, openLoop, reversed)
#pragma config(Motor,  port8,           driveR1,       tmotorVex393TurboSpeed_MC29, openLoop)
#pragma config(Motor,  port9,           liftR,         tmotorVex393HighSpeed_MC29, openLoop)
//*!!Code automatically generated by 'ROBOTC' configuration wizard               !!*//

#define CHECK_POTI_JUMPS
//#define FORCE_AUTO

// Necessary definitions

#define TID0(routine) #routine, 0
#define TID1(routine, id) #routine, id
#define TID2(routine, major, minor) #routine, ((major << 8) | minor)

//Sensors
#include "sensors.h"

//Timeout function
bool TimedOut(unsigned long timeOut, const unsigned char *routine, unsigned short id, bool kill = true, tSensors senT = -1, short dir = 1, unsigned long elpsdTime = 0);

// Year-independent libraries

#include "notify.h"
#include "task.h"
#include "async.h"
#include "timeout.h"
#include "motors.h"
#include "joysticks.h"
#include "cycle.h"
#include "utilities.h"
#include "pid.h"
#include "state.h"

#include "notify.c"
#include "task.c"
#include "async.c"
#include "timeout.c"
#include "motors.c"
#include "sensors.c"
#include "joysticks.c"
#include "cycle.c"
#include "utilities.c"
#include "pid.c"
#include "state.c"

#include "Vex_Competition_Includes_Custom.c"

#include "controls.h"

#include "auto.h"
#include "auto_simple.h"
#include "auto_runs.h"
#include "diagnostics.h"

#define TRACK_IN_DRIVER
#define SKILLS_RESET_AT_START

//#define ULTRASONIC_RESET

//#define LIFT_SLOW_DRIVE_THRESHOLD 1200

bool stackRunning();

typedef enum _stackFlags {
	sfNone = 0,
	sfStack = 1,
	sfDetach = 2,
	sfClear = 4,
	sfReturn = 8,
	sfMobile = 16,
	sfLoader = 32
} tStackFlags;

typedef enum _stackStates {
	stackNotRunning,
	stackPickupGround,
	stackPickupLoader,
	stackStationaryPrep,
	stackStationary,
	stackStack,
	stackDetach,
	stackClear,
	stackReturn
} tStackStates;

DECLARE_MACHINE(stack, tStackStates)

#define STACK_CLEAR_CONFIG(flags, mobileState) ((flags) | sfClear | sfMobile | ((mobileState) << 16))
#define MAX_STACK 11

sCycleData gMainCycle;
int gNumCones = 0;
bool gSetTimedOut = false;

#define DRIVE_TURN_BRAKE 6

bool gDriveManual;

/* Drive */
void setDrive(word left, word right, bool debug = false)
{
	if (debug)
		writeDebugStreamLine("DRIVE %d %d", left, right);
	gMotor[driveL1].power = gMotor[driveL2].power = left;
	gMotor[driveR1].power = gMotor[driveR2].power = right;
}

void handleDrive()
{
	if (gDriveManual)
	{
		//gJoy[JOY_TURN].deadzone = MAX(abs(gJoy[JOY_THROTTLE].cur) / 2, DZ_ARM);
		short y = gJoy[JOY_THROTTLE].cur;
		short a = gJoy[JOY_TURN].cur;

#if defined(DRIVE_TURN_BRAKE) && DRIVE_TURN_BRAKE > 0
#ifndef TRACK_IN_DRIVER
#error "Turn braking requires track in driver!"
#endif
		if (!a && abs(gVelocity.a) > 0.5)
			a = -DRIVE_TURN_BRAKE * sgn(gVelocity.a);
#endif

		word l = y + a;
		word r = y - a;

		//if (gSensor[liftPoti].value > LIFT_SLOW_DRIVE_THRESHOLD)
		//{
		//	LIM_TO_VAL_SET(l, 40);
		//	LIM_TO_VAL_SET(r, 40);
		//}
		setDrive(l, r);
	}
}


/* Lift */
typedef enum _tLiftStates {
	liftManaged,
	liftIdle,
	liftManual,
	liftRaiseSimpleState,
	liftLowerSimpleState,
	liftHold,
	liftHoldDown,
	liftHoldUp,
} tLiftStates;

void setLift(word power,bool debug=false)
{
	if( debug )	writeDebugStreamLine("%06d Lift %4d", nPgmTime,power );
	gMotor[liftL].power = gMotor[liftR].power = power;
}

#define LIFT_BOTTOM 1050
#define LIFT_TOP (LIFT_BOTTOM + 2150)
#define LIFT_MID (LIFT_BOTTOM + 900)
#define LIFT_HOLD_DOWN_THRESHOLD (LIFT_BOTTOM + 50)
#define LIFT_HOLD_UP_THRESHOLD (LIFT_TOP - 100)
#define LIFT_LOADER (LIFT_BOTTOM + 650)
#define LIFT_PERIMETER (LIFT_BOTTOM + 500)

DECLARE_MACHINE(lift, tLiftStates)

void liftRaiseSimple(int target, word mainPower, word brakePower)
{
	writeDebugStreamLine("Raising lift to %d", target);
	setLift(mainPower);
	int pos;
	while ((pos = gSensor[liftPoti].value) < target) sleep(10);
	if (brakePower)
	{
		setLift(brakePower);
		sleep(200);
	}
	writeDebugStreamLine("Lift moved up to %d | %d", target, pos);
}

NEW_ASYNC_VOID_STATE_3(lift, liftRaiseSimpleState, liftRaiseSimple, int, word, word);

void liftLowerSimple(int target, word mainPower, word brakePower)
{
	writeDebugStreamLine("Lowering lift to %d", target);
	setLift(mainPower);
	int pos;
	while ((pos = gSensor[liftPoti].value) > target) sleep(10);
	if (brakePower)
	{
		setLift(brakePower);
		sleep(200);
	}
	writeDebugStreamLine("Lift moved down to %d | %d", target, pos);
}

NEW_ASYNC_VOID_STATE_3(lift, liftLowerSimpleState, liftLowerSimple, int, word, word);

MAKE_MACHINE(lift, tLiftStates, liftIdle,
{
case liftIdle:
	setLift(0);
	break;
case liftRaiseSimpleState:
	{
		STATE_INVOKE_ASYNC(liftRaiseSimple);
		NEXT_STATE(liftHold);
	}
case liftLowerSimpleState:
	{
		STATE_INVOKE_ASYNC(liftLowerSimple);
		NEXT_STATE(liftHold);
	}
case liftHold:
	{
		int target = gSensor[liftPoti].value;
		if (target < LIFT_HOLD_DOWN_THRESHOLD)
			NEXT_STATE(liftHoldDown);
		if (target > LIFT_HOLD_UP_THRESHOLD)
			NEXT_STATE(liftHoldUp);
		setLift(6 + (word)(6 * cos((MIN(target - LIFT_MID, 0)) * PI / 2700)));
		break;
	}
case liftHoldDown:
	setLift(-15);
	break;
case liftHoldUp:
	setLift(15);
	break;
})

void handleLift()
{
	if (liftState == liftManaged || stackRunning()) return;

	if (RISING(JOY_LIFT_DRIVER) || RISING(JOY_LIFT_PARTNER))
	{
		liftSet(liftManual);
	}
	if (liftState == liftManual && !gJoy[JOY_LIFT_DRIVER].cur && !gJoy[JOY_LIFT_PARTNER].cur)
	{
		liftSet(liftHold, -1);
	}

	if (liftState == liftManual)
	{
		word value = gJoy[JOY_LIFT_DRIVER].cur ? gJoy[JOY_LIFT_DRIVER].cur : gJoy[JOY_LIFT_PARTNER].cur;
		value = value * 2 - 128 * sgn(value);
		if (gSensor[liftPoti].value <= LIFT_BOTTOM && value < -15) value = -15;
		if (gSensor[liftPoti].value >= LIFT_TOP && value > 15) value = 15;
		setLift(value);
	}
}


/* Arm */
typedef enum _tArmStates {
	armManaged,
	armIdle,
	armManual,
	armToTarget,
	armRaiseSimpleState,
	armLowerSimpleState,
	armStopping,
	armHold
} tArmStates;

#define ARM_TOP 2400
#define ARM_BOTTOM 950
#define ARM_PRESTACK 2000
#define ARM_RELEASE 1900
#define ARM_CARRY 1500
#define ARM_STACK 2350
#define ARM_HORIZONTAL 1150

void setArm(word power, bool debug = false)
{
	if( debug ) writeDebugStreamLine("%06d Arm  %4d", nPgmTime, power);
	gMotor[arm].power = power;
	//	motor[arm]=power;
}

DECLARE_MACHINE(arm, tArmStates)

void armRaiseSimple(int target, word mainPower, word brakePower, float earlyDrop, unsigned long brakeDelay)
{
	setArm(mainPower);
	int pos;
	velocityClear(armPoti);
	do
	{
		sleep(10);
		pos = gSensor[armPoti].value;
		velocityCheck(armPoti);
		if (gSensor[armPoti].velGood && pos + gSensor[armPoti].velocity * earlyDrop > target)
			break;
	}	while (pos < target);
	if (brakePower)
	{
		setArm(brakePower);
		sleep(brakeDelay);
		setArm(0);
	}
	writeDebugStreamLine("Arm moved up to %d | %d", target, pos);
}

NEW_ASYNC_VOID_STATE_5(arm, armRaiseSimpleState, armRaiseSimple, int, word, word, float, unsigned long);

unsigned long armRaiseSimpleAsync(int target, word mainPower, word brakePower)
{
	return armRaiseSimpleAsync(target, mainPower, brakePower, 0, 200);
}

void armLowerSimple(int target, word mainPower, word brakePower, float earlyDrop, unsigned long brakeDelay)
{
	setArm(mainPower);
	int pos;
	velocityClear(armPoti);
	do
	{
		sleep(10);
		pos = gSensor[armPoti].value;
		velocityCheck(armPoti);
		if (gSensor[armPoti].velGood && pos + gSensor[armPoti].velocity * earlyDrop < target)
			break;
	} while (pos > target);
	if (brakePower)
	{
		setArm(brakePower);
		sleep(brakeDelay);
	}
	writeDebugStreamLine("Arm moved down to %d | %d", target, pos);
}

NEW_ASYNC_VOID_STATE_5(arm, armLowerSimpleState, armLowerSimple, int, word, word, float, unsigned long);

unsigned long armLowerSimpleAsync(int target, word mainPower, word brakePower)
{
	return armLowerSimpleAsync(target, mainPower, brakePower, 0, 200);
}

MAKE_MACHINE(arm, tArmStates, armIdle,
{
case armIdle:
	setArm(0);
	break;
case armToTarget:
	{
		if (arg._long != -1)
		{
			const float kP = 0.1;
			int err;
			sCycleData cycle;
			initCycle(cycle, 10, "armToTarget");
			do
			{
				err = arg._long - gSensor[armPoti].value;
				float power = kP * err;
				LIM_TO_VAL_SET(power, 127);
				setArm((word)power);
				endCycle(cycle);
			} while (abs(err) > 100);
		}
		writeDebugStreamLine("Arm at target: %d %d", arg, gSensor[armPoti].value);
		NEXT_STATE(armStopping);
	}
case armRaiseSimpleState:
	{
		STATE_INVOKE_ASYNC(armRaiseSimple);
		NEXT_STATE(armHold);
	}
case armLowerSimpleState:
	{
		STATE_INVOKE_ASYNC(armLowerSimple);
		NEXT_STATE(armHold);
	}
case armStopping:
	velocityClear(armPoti);
	velocityCheck(armPoti);
	do
	{
		sleep(5);
		velocityCheck(armPoti);
	} while (!gSensor[armPoti].velGood);
	setArm(sgn(gSensor[armPoti].velocity) * -25);
	sleep(150);
	NEXT_STATE(armHold);
case armHold:
	{
		//const float kP = -5;
		//velocityClear(armPoti);
		//sCycleData cycle;
		//initCycle(cycle, 10, "armHold");
		//while (true)
		//{
		//	velocityCheck(armPoti);
		//	if (gSensor[armPoti].velGood) {
		//		float power = 5 + gSensor[armPoti].velocity * kP;
		//		LIM_TO_VAL_SET(power, 12);
		//		setArm((word)power);
		//	}
		//	endCycle(cycle);
		//}
		setArm(7);
	}
})

void handleArm()
{
	if (armState == armManaged || stackRunning()) return;

	if (RISING(JOY_ARM_DRIVER) || RISING(JOY_ARM_PARTNER))
	{
		armSet(armManual);
	}
	if (armState == armManual && !gJoy[JOY_ARM_DRIVER].cur && !gJoy[JOY_ARM_PARTNER].cur)
	{
		armSet(armHold, -1);
	}

	if (armState == armManual)
	{
		word value = gJoy[JOY_ARM_DRIVER].cur ? gJoy[JOY_ARM_DRIVER].cur : gJoy[JOY_ARM_PARTNER].cur;
		value = value * 2 - 128 * sgn(value);
		if (gSensor[armPoti].value >= ARM_TOP && value > 10) value = 10;
		if (gSensor[armPoti].value <= ARM_BOTTOM && value < -10) value = -10;
		setArm(value);
	}
}


/* Mobile */

typedef enum _tMobileStates {
	mobileManaged,
	mobileIdle,
	mobileTop,
	mobileBottom,
	mobileBottomSlow,
	mobileUpToMiddle,
	mobileDownToMiddle,
	mobileMiddle
} tMobileStates;

#define MOBILE_TOP 2250
#define MOBILE_BOTTOM 500
#define MOBILE_MIDDLE_UP 600
#define MOBILE_MIDDLE_DOWN 1200
#define MOBILE_MIDDLE_THRESHOLD 1900
#define MOBILE_HALFWAY 1200

#define MOBILE_UP_POWER 127
#define MOBILE_DOWN_POWER -127
#define MOBILE_UP_HOLD_POWER 10
#define MOBILE_DOWN_HOLD_POWER -10
#define MOBILE_DOWN_SLOW_POWER_1 -60
#define MOBILE_DOWN_SLOW_POWER_2 6

#define MOBILE_LIFT_CHECK_THRESHOLD 1700
#define LIFT_MOBILE_THRESHOLD (LIFT_BOTTOM + 400)

#define MOBILE_SLOW_HOLD_TIMEOUT 500
#define MOBILE_AUTO_TIMEOUT 500

bool gMobileCheckLift;
bool gMobileSlow = false;
bool gMobileAutoEnabled = true;
unsigned long gMobileAutoTimeout = 0;
unsigned long gMobileAutoIgnore = 0;

void setMobile(word power, bool debug = false)
{
	//writeDebugStreamLine("MOBILE %d", power);
	gMotor[mobile].power = power;
}

void mobileClearLift()
{
	writeDebugStreamLine("mobileClearLift");
	if (gMobileCheckLift && gSensor[liftPoti].value < LIFT_MOBILE_THRESHOLD)
	{
		liftRaiseSimpleAsync(LIFT_MOBILE_THRESHOLD, 80, -15);
		unsigned long timeout = nPgmTime + 1000;
		liftTimeoutWhile(liftRaiseSimpleState, timeout, TID0(mobileClearLift));
	}
}

MAKE_MACHINE(mobile, tMobileStates, mobileIdle,
{
case mobileIdle:
	setMobile(0);
	break;
case mobileTop:
	{
		gMobileAutoEnabled = false;
		if (arg._long)
			mobileClearLift();
		setMobile(MOBILE_UP_POWER);
		unsigned long timeout = nPgmTime + 2000;
		while (gSensor[mobilePoti].value < MOBILE_TOP - 600 && !TimedOut(timeout, TID1(mobileTop, 1))) sleep(10);
		setMobile(15);
		while (gSensor[mobilePoti].value < MOBILE_TOP - 600 && !TimedOut(timeout, TID1(mobileTop, 2))) sleep(10);
		setMobile(MOBILE_UP_HOLD_POWER);
		//if (gSensor[jmpSkills].value)
		//	liftLowerSimpleAsync(LIFT_BOTTOM, -127, 0);
		break;
	}
case mobileBottom:
	{
		gNumCones = 0;
		gMobileAutoIgnore = nPgmTime + 1500;
		if (gMobileSlow)
			NEXT_STATE(mobileBottomSlow)
		if (arg._long && gSensor[mobilePoti].value > MOBILE_LIFT_CHECK_THRESHOLD)
			mobileClearLift();
		setMobile(MOBILE_DOWN_POWER);
		unsigned long timeout = nPgmTime + 2000;
		while (gSensor[mobilePoti].value > MOBILE_BOTTOM && !TimedOut(timeout, TID0(mobileBottom))) sleep(10);
		setMobile(MOBILE_DOWN_HOLD_POWER);
		break;
	}
case mobileBottomSlow:
	{
		gMobileSlow = false;
		if (arg._long && gSensor[mobilePoti].value > MOBILE_LIFT_CHECK_THRESHOLD)
			mobileClearLift();
		//sPID pid;
		//pidInit(pid, 0.04, 0, 3.5, -1, -1, -1, 60);
		velocityClear(mobilePoti);
		unsigned long timeout = nPgmTime + 3000;
		setMobile(-60);
		while (gSensor[mobilePoti].value > MOBILE_HALFWAY + 300 && !TimedOut(timeout, TID1(mobileBottomSlow, 2))) sleep(10);
		sCycleData cycle;
		initCycle(cycle, 10, "mobileBottomSlow");
		//const float kP = 0.02;
		const float kP_vel = 0.001;
		const float kP_pwr = 3.0;
		while (gSensor[mobilePoti].value > MOBILE_BOTTOM + 200 && !TimedOut(timeout, TID1(mobileBottomSlow, 3)))
		{
			//setMobile((MOBILE_BOTTOM - gSensor[mobilePoti].value) * kP);
			//pidCalculate(pid, MOBILE_BOTTOM, gSensor[mobilePoti].value);
			//setMobile((word)pid.output);
			velocityCheck(mobilePoti);
			if (gSensor[mobilePoti].velGood)
			{
				float power = ((MOBILE_BOTTOM + 200 - gSensor[mobilePoti].value) * kP_vel - gSensor[mobilePoti].velocity) * kP_pwr;
				LIM_TO_VAL_SET(power, (gSensor[mobilePoti].value < MOBILE_BOTTOM + 700) ? 10 : 20);
				setMobile((word)power);
			}
			endCycle(cycle);
		}
		setMobile(0);
		while (gSensor[mobilePoti].value > MOBILE_BOTTOM && !TimedOut(timeout, TID1(mobileBottomSlow, 4))) sleep(10);
		arg._long = 0;
		NEXT_STATE(mobileBottom)
	}
case mobileUpToMiddle:
	{
		setMobile(MOBILE_UP_POWER);
		unsigned long timeout = nPgmTime + 1000;
		while (gSensor[mobilePoti].value < MOBILE_MIDDLE_UP && !TimedOut(timeout, TID0(mobileUpToMiddle))) sleep(10);
		setMobile(15);
		arg._long = -1;
		NEXT_STATE(mobileMiddle)
	}
case mobileDownToMiddle:
	{
		if (arg._long)
			mobileClearLift();
		setMobile(MOBILE_DOWN_POWER);
		unsigned long timeout = nPgmTime + 1000;
		while (gSensor[mobilePoti].value > MOBILE_MIDDLE_DOWN && !TimedOut(timeout, TID0(mobileUpToMiddle))) sleep(10);
		setMobile(15);
		arg._long = -1;
		NEXT_STATE(mobileMiddle)
	}
case mobileMiddle:
	while (gSensor[mobilePoti].value < MOBILE_MIDDLE_THRESHOLD) sleep(10);
	arg._long = -1;
	NEXT_STATE(mobileTop)
})

void mobileWaitForSlowHold(TVexJoysticks btn)
{
	unsigned long timeout = nPgmTime + MOBILE_SLOW_HOLD_TIMEOUT;
	while (nPgmTime < timeout)
	{
		if (!gJoy[btn].cur) return;
		sleep(10);
	}
	gMobileSlow = true;
	if (mobileState == mobileBottom)
		mobileSet(mobileBottomSlow);
	writeDebugStreamLine("mobileBottomSlow activated");
}

NEW_ASYNC_VOID_1(mobileWaitForSlowHold, TVexJoysticks);

void handleMobile()
{
	if (mobileState == mobileManaged || nPgmTime < gMobileAutoTimeout)
		return;

	//if (!gSensor[limMobile].value && gSensor[mobilePoti].value < MOBILE_BOTTOM + 200 && nPgmTime > gMobileAutoIgnore)
	//	gMobileAutoEnabled = true;
	//if (gMobileAutoEnabled && gSensor[limMobile].value && gSensor[jmpSkills].value)
	//{
	//	mobileSet(mobileTop, -1);
	//	gMobileAutoTimeout = nPgmTime + MOBILE_AUTO_TIMEOUT;
	//	playSound(soundUpwardTones);
	//	return;
	//}

	if (mobileState == mobileUpToMiddle || mobileState == mobileDownToMiddle || mobileState == mobileMiddle)
	{
		if (RISING(BTN_MOBILE_MIDDLE))
			mobileSet(mobileTop, -1);
		if (RISING(BTN_MOBILE_TOGGLE))
		{
			gMobileSlow = false;
			stackSet(stackDetach, STACK_CLEAR_CONFIG(sfNone, mobileBottom));
			mobileWaitForSlowHoldAsync(BTN_MOBILE_MIDDLE);
		}
	}
	else
	{
		if (RISING(BTN_MOBILE_TOGGLE))
		{
			if (gSensor[mobilePoti].value > MOBILE_HALFWAY)
			{
				if (gNumCones > 3)
					stackSet(stackDetach, STACK_CLEAR_CONFIG(sfNone, mobileBottomSlow));
				else
				{
					gMobileSlow = false;
					stackSet(stackDetach, STACK_CLEAR_CONFIG(sfNone, mobileBottom));
					mobileWaitForSlowHoldAsync(BTN_MOBILE_TOGGLE);
				}
			}
			else
				mobileSet(mobileTop, -1);
		}
		if (RISING(BTN_MOBILE_MIDDLE))
		{
			if (gSensor[mobilePoti].value > MOBILE_HALFWAY)
			{
				stackSet(stackDetach, STACK_CLEAR_CONFIG(sfNone, mobileDownToMiddle));
			}
			else
				mobileSet(mobileUpToMiddle, -1);
		}
	}
}


/* Macros + Autonomous */

bool gLiftAsyncDone;
bool gContinueLoader = false;
bool gLiftTargetReached;

bool gKillDriveOnTimeout = false;

// STACKING ON                     0     1     2     3     4     5     6     7     8     9     10
const int gLiftRaiseTarget[11] = { 1300, 1400, 1600, 1800, 2000, 2150, 2300, 2450, 2600, 2850, LIFT_TOP };
const int gLiftPlaceTarget[11] = { 1050, 1150, 1350, 1500, 1600, 2000, 2100, 2300, 2500, 2700, 2700 };
const int gLiftRaiseTargetS[5] = { 2250, 2350, 2700, 2900, LIFT_TOP };
const int gLiftPlaceTargetS[5] = { 1900, 2000, 2150, 2350, 2550 };

bool gStack = false;
bool gLoader = false;
unsigned long gPrepStart = 0;

MAKE_MACHINE(stack, tStackStates, stackNotRunning,
{
case stackNotRunning:
	liftSet(liftHold);
	armSet(armHold);
	gDriveManual = true;
	break;
case stackPickupGround:
	{
		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gSensor[armPoti].value > ARM_PRESTACK - 300)
		{
			armLowerSimpleAsync(ARM_HORIZONTAL, -127, 15);
			armTimeOut = nPgmTime + 1000;
			timeoutWhileGreaterThanL(armPoti, ARM_PRESTACK, armTimeOut, TID1(stackPickupGround, 1));
		}
		else if (gSensor[armPoti].value < ARM_BOTTOM + 300)
		{
			armRaiseSimpleAsync(ARM_BOTTOM + 300, 127, -10);
		}

		liftLowerSimpleAsync(LIFT_BOTTOM, -127, 0);
		liftTimeOut = nPgmTime + 1200;
		timeoutWhileGreaterThanL(liftPoti, LIFT_BOTTOM + 300, liftTimeOut, TID1(stackPickupGround, 2));

		armLowerSimpleAsync(ARM_BOTTOM, -127, 0);
		armTimeOut = nPgmTime + 1200;
		liftTimeoutWhile(liftLowerSimpleState, liftTimeOut, TID1(stackPickupGround, 3));
		timeoutWhileGreaterThanL(armPoti, ARM_BOTTOM, armTimeOut, TID1(stackPickupGround, 4), false);

		armRaiseSimpleAsync(ARM_PRESTACK - 400, 127, -20, 30, 200);
		armTimeOut = nPgmTime + 500;
		timeoutWhileLessThanL(armPoti, ARM_BOTTOM + 150, armTimeOut, TID1(stackPickupGround, 5));

		NEXT_STATE((arg._long & sfStack) ? stackStack : stackNotRunning)
	}
case stackPickupLoader:
	{
		if (gNumCones >= MAX_STACK)
			NEXT_STATE(stackNotRunning)

		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gSensor[liftPoti].value < LIFT_LOADER + 200)
		{
			liftRaiseSimpleAsync(LIFT_LOADER + 300, 127, -5);
			liftTimeOut = nPgmTime + 600;
			liftTimeoutWhile(liftRaiseSimpleState, liftTimeOut, TID1(stackPickupLoader, 1));
		}

		armLowerSimpleAsync(ARM_HORIZONTAL + 300, -127, 0);
		armTimeOut = nPgmTime + 800;
		liftLowerSimpleAsync(LIFT_LOADER, -127, 0);
		liftTimeOut = nPgmTime + 600;
		armTimeoutWhile(armLowerSimpleState, armTimeOut, TID1(stackPickupLoader, 2));
		liftTimeoutWhile(liftLowerSimpleState, liftTimeOut, TID1(stackPickupLoader, 3));

		NEXT_STATE((arg._long & sfStack) ? stackStack : stackNotRunning)
	}
case stackStationaryPrep:
	{
		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gNumCones >= 5)
			NEXT_STATE(stackNotRunning)

		armRaiseSimpleAsync(ARM_TOP, 127, 0);
		armTimeOut = nPgmTime + 1500;
		liftRaiseSimpleAsync(gLiftRaiseTargetS[gNumCones], 127, (gNumCones >= 4) ? 0 : -10);
		liftTimeOut = nPgmTime + 1500;
		armTimeoutUntil(armHold, armTimeOut, TID1(stackStationaryPrep, 1));
		liftTimeoutWhile(liftRaiseSimpleState, liftTimeOut, TID1(stackStationaryPrep, 1));
		NEXT_STATE(stackNotRunning)
	}
case stackStationary:
	{
		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gNumCones >= 5)
			NEXT_STATE(stackNotRunning)

		armRaiseSimpleAsync(ARM_TOP, 127, 0);
		armTimeOut = nPgmTime + 1000;
		timeoutWhileLessThanL(armPoti, ARM_TOP - 100, armTimeOut, TID1(stackStationary, 1));
		armSet(armManaged);
		setArm(-20);
		liftLowerSimpleAsync(gLiftPlaceTargetS[gNumCones], -127, 25);
		liftTimeOut = nPgmTime + 2000;
		timeoutWhileGreaterThanL(liftPoti, gLiftPlaceTargetS[gNumCones], liftTimeOut, TID1(stackStationary, 2));
		armLowerSimpleAsync(ARM_HORIZONTAL, -127, 25, 35, 200);
		armTimeOut = nPgmTime + 1500;
		timeoutWhileGreaterThanL(armPoti, ARM_HORIZONTAL + 300, armTimeOut, TID1(stackStationary, 3));

		++gNumCones;

		long target = (gNumCones >= 5) ? LIFT_TOP : gLiftRaiseTargetS[gNumCones];
		liftRaiseSimpleAsync(target, 127, (gNumCones >= 4) ? 0 : -15);
		liftTimeOut = nPgmTime + 2000;
		timeoutWhileLessThanL(liftPoti, target, liftTimeOut, TID1(stackStationary, 4));

		gDriveManual = true;

		armRaiseSimpleAsync(ARM_TOP, 127, -15);
		armTimeOut = nPgmTime + 2000;
		armTimeoutWhile(armRaiseSimpleState, armTimeOut, TID1(stackStationary, 5));
		liftTimeoutWhile(liftRaiseSimpleState, liftTimeOut, TID1(stackStationary, 6));

		NEXT_STATE(stackNotRunning)
	}
case stackStack:
	{
		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gNumCones >= MAX_STACK)
			NEXT_STATE(stackNotRunning)

		liftRaiseSimpleAsync(gLiftRaiseTarget[gNumCones], 127, (gNumCones < MAX_STACK - 1) ? -25 : 0);
		liftTimeOut = nPgmTime + 1500;
		timeoutWhileLessThanL(liftPoti, gLiftRaiseTarget[gNumCones] - 400, liftTimeOut, TID1(stackStack, 1));

		armRaiseSimpleAsync(ARM_STACK, 127, 0);
		armTimeOut = nPgmTime + 1000;
		timeoutWhileLessThanL(liftPoti, gLiftRaiseTarget[gNumCones] - 100, liftTimeOut, TID1(stackStack, 2));
		timeoutWhileLessThanL(armPoti, ARM_STACK - 100, armTimeOut, TID1(stackStack, 3));

		liftLowerSimpleAsync(gLiftPlaceTarget[gNumCones], -70, (arg._long & (sfClear | sfReturn) ? 0 : 5));
		liftTimeOut = nPgmTime + 800;
		liftTimeoutWhile(liftLowerSimpleState, liftTimeOut, TID1(stackStack, 4));

		++gNumCones;

		NEXT_STATE((arg._long & (sfDetach | sfClear | sfReturn)) ? stackDetach : stackNotRunning)
	}
case stackDetach:
	if (gNumCones > 0 && gSensor[liftPoti].value < gLiftRaiseTarget[gNumCones - 1])
	{
		if ((arg._long & sfReturn) && gNumCones > 3) {
			liftLowerSimpleAsync((arg._long & sfLoader) ? 2600 : gStack ? LIFT_BOTTOM : 1650, -50, 25);
		}
		else {
			liftSet(liftManaged);
			setLift(-10);
		}
		if (!gStack && gNumCones <= 3)
			armLowerSimpleAsync((gNumCones == 3) ? ARM_RELEASE - 100 : ARM_RELEASE, -127, 45, 100, 80);
		else
			armLowerSimpleAsync(ARM_RELEASE, -127, 0);
		unsigned long armTimeOut = nPgmTime + 800;
		armTimeoutWhile(armLowerSimpleState, armTimeOut, TID0(stackDetach));
		liftReset();
	}
	if (gStack) {
		gStack = false;
		if (gNumCones < MAX_STACK) {
			if (gNumCones == MAX_STACK - 1)
				arg._long &= ~sfReturn;
			if (gLoader) {
				if (gNumCones > 6) {
					arg._long |= sfLoader;
					gLoader = false;
					NEXT_STATE(stackPickupLoader)
				}
			}
			else {
				arg._long &= ~sfLoader;
				NEXT_STATE(stackPickupGround)
			}
		}
	}
	NEXT_STATE((arg._long & sfClear) ? stackClear : (arg._long & sfReturn) ? stackReturn : stackNotRunning)
case stackClear:
	{
		int target = gNumCones == 11 ? LIFT_TOP : gLiftRaiseTarget[gNumCones];
		liftRaiseSimpleAsync(target, 127, gNumCones <= 4 ? -15 : 0);
		unsigned long timeout = nPgmTime + 1500;
		timeoutWhileLessThanL(liftPoti, target, timeout, TID1(stackClear, 1));

		if (gSensor[armPoti].value < ARM_STACK)
		{
			armRaiseSimpleAsync(ARM_TOP, 127, 0);
			timeout = nPgmTime + 1000;
			armTimeoutWhile(armRaiseSimpleState, timeout, TID1(stackClear, 2));
		}

		if (arg._long & sfMobile)
			mobileSet(arg._long >> 16);

		NEXT_STATE(stackNotRunning)
	}
case stackReturn:
	{
		unsigned long armTimeOut;
		unsigned long liftTimeOut;

		if (gNumCones <= 3)
		{
			liftRaiseSimpleAsync(1350, 80, -25);
			liftTimeOut = nPgmTime + 800;
			timeoutWhileLessThanL(liftPoti, LIFT_BOTTOM + 150, liftTimeOut, TID1(stackReturn, 1));
		}

		armLowerSimpleAsync(ARM_HORIZONTAL + 100, -127, 25, 50, 200);
		armTimeOut = nPgmTime + 1000;

		if (gNumCones <= 3)
		{

			liftTimeoutWhile(liftRaiseSimpleState, liftTimeOut, TID1(stackReturn, 2));
		}
		else if (gNumCones <= 7 && (arg._long & sfLoader))
		{
			liftRaiseSimpleAsync(2000, 80, -15);
			liftTimeOut = nPgmTime + 600;
			liftTimeoutWhile(liftRaiseSimpleState, liftTimeOut, TID1(stackReturn, 3));
		}
		else
		{
			liftLowerSimpleAsync((arg._long & sfLoader) ? 2500 : 1650, -127, 25);
			liftTimeOut = nPgmTime + 1000;
			liftTimeoutWhile(liftLowerSimpleState, liftTimeOut, TID1(stackReturn, 4));
		}

		timeoutWhileGreaterThanL(armPoti, ARM_PRESTACK, armTimeOut, TID1(stackReturn, 5));

		armTimeoutWhile(armLowerSimpleState, armTimeOut, TID1(stackReturn, 6));
		NEXT_STATE(stackNotRunning)
	}
})

task failTimeout()
{
	stackReset();
	liftReset();
	armReset();
	mobileReset();
	gDriveManual = true;
	if (competitionState == usercontrolState)
		competitionSet(competitionState);
}

bool TimedOut(unsigned long timeOut, const unsigned char *routine, unsigned short id, bool kill, tSensors senT, short dir, unsigned long elpsdTime)
{
	if (senT != -1)
		velocityCheck(senT);

	if (nPgmTime > timeOut || ( (senT != -1 && gSensor[senT].velGood && elpsdTime > 100) ? abs(gSensor[senT].velocity) < 0.5 : 0 ) )
	{
		tHog();
		char description[40];
		if (id >> 8)
			snprintf(description, 40, "%s %d-%d", routine, id >> 8, (word) (id & 0xFF));
		else if (id)
			snprintf(description, 40, "%s %d", routine, (word) id);
		else
			strcpy(description, routine);
		writeDebugStream("%06d EXCEEDED TIME %d - ", nPgmTime, timeOut);
		writeDebugStreamLine(description);
		if (kill)
		{
			for (tMotor x = port1; x <= port10; ++x)
				gMotor[x].power = motor[x] = 0;
			int current = nCurrentTask;
			if (current == main)
			{
				stopAllButCurrentTasks();
				startTask(main);
			}
			tStart(failTimeout);
			tStop(nCurrentTask);
		}
		return true;
	}
	else
		return false;
}

bool stackRunning()
{
	return stackState != stackNotRunning;
}

bool cancel()
{
	if (stackState != stackNotRunning)
	{
		stackReset();
		liftReset();
		armReset();
		gDriveManual = true;
		writeDebugStreamLine("Stack cancelled");
		return true;
	}
	return false;
}

void handleMacros()
{
	if (RISING(BTN_MACRO_STACK) && gNumCones < MAX_STACK)
	{
		gStack = true;
		gLoader = false;
	}

	if (RISING(BTN_MACRO_LOADER) && gNumCones < MAX_STACK)
	{
		gStack = true;
		gLoader = true;
	}

	if (gStack == true && gNumCones < MAX_STACK)
	{
		if (!stackRunning())
		{
			writeDebugStreamLine("Stacking");
			if (gLoader)
				stackSet(stackPickupLoader, (gNumCones < MAX_STACK - 1) ? (gNumCones >= 4) ? sfStack | sfReturn | sfLoader : sfNone : sfStack);
			else
				stackSet(stackPickupGround, (gNumCones < MAX_STACK - 1) ? sfStack | sfReturn : sfStack | sfDetach);
			gStack = false;
			gLoader = false;
		}
	}

	if (RISING(BTN_MACRO_STATIONARY) && !stackRunning())
	{
		if (gNumCones < ARR_LEN(gLiftRaiseTargetS))
			stackSet((gSensor[liftPoti].value < gLiftRaiseTargetS[gNumCones] - 150) ? stackStationaryPrep : stackStationary, sfNone);
	}

	if (RISING(BTN_MACRO_PRELOAD) && !stackRunning())
	{
		stackSet(stackStack, (gNumCones < MAX_STACK - 1) ? sfReturn : sfNone);
	}

	if (RISING(BTN_MACRO_PICKUP) && !stackRunning())
	{
		stackSet(stackPickupGround, sfNone);
	}

	if (RISING(BTN_MACRO_PREP) && !stackRunning())
	{
		gPrepStart = nPgmTime;
	}

	if (FALLING(BTN_MACRO_PREP) && !stackRunning())
	{
		if (nPgmTime - gPrepStart > 500)
		{
			if (gSensor[liftPoti].value < LIFT_PERIMETER)
				liftRaiseSimpleAsync(LIFT_PERIMETER - 100, 80, -15);
			else
				liftLowerSimpleAsync(LIFT_PERIMETER + 200, -80, 15);
		}
		else
		{
			if (gSensor[liftPoti].value < LIFT_LOADER)
				liftRaiseSimpleAsync(LIFT_LOADER, 80, -15);
			else
				liftLowerSimpleAsync(2400, -80, 15);
		}
		if (gSensor[armPoti].value < ARM_HORIZONTAL)
			armRaiseSimpleAsync(ARM_HORIZONTAL, 127, -25, 35, 200);
		else
			armLowerSimpleAsync(ARM_HORIZONTAL, -127, 25, 35, 200);
	}

	if (RISING(BTN_MACRO_CANCEL)) cancel();

	if (RISING(BTN_MACRO_INC) && gNumCones < 11) {
		++gNumCones;
		writeDebugStreamLine("%06d gNumCones= %d",nPgmTime,gNumCones);
	}

	if (RISING(BTN_MACRO_DEC) && gNumCones > 0) {
		--gNumCones;
		writeDebugStreamLine("%06d gNumCones= %d",nPgmTime,gNumCones);
	}

	if (FALLING(BTN_MACRO_ZERO))	writeDebugStreamLine("%06d MACRO_ZERO Released",nPgmTime,gNumCones);

	if (RISING(BTN_MACRO_ZERO)) {

		gNumCones = 0;
		writeDebugStreamLine("%06d gNumCones= %d",nPgmTime,gNumCones);
	}
}

#include "auto.c"
#include "auto_simple.c"
#include "auto_runs.c"
#include "diagnostics.c"

#ifndef SKILLS_RESET_AT_START
void waitForSkillsReset()
{
	while (!gSensor[btnSetPosition].value)
	{
		updateSensorInput(btnSetPosition);
		sleep(10);
	}
	autoMotorSensorUpdateTaskAsync();
	trackPositionTaskAsync();
	resetPositionFull(gPosition, 8.25, 61.5, 0);
	writeDebugStreamLine("RESET");
}

NEW_ASYNC_VOID_0(waitForSkillsReset);
#endif

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

	competitionSetup();
	mobileSetup();
	liftSetup();
	stackSetup();
	autoSimpleSetup();

	setupInvertedSen(jmpSkills);
	setupDgtIn(lsBarL, 0, 2500);
	setupDgtIn(lsBarR, 0, 2500);

	velocityClear(trackL);
	velocityClear(trackR);

#ifndef SKILLS_RESET_AT_START
	waitForSkillsResetAsync();
#endif

	gJoy[JOY_TURN].deadzone = DZ_TURN;
	gJoy[JOY_THROTTLE].deadzone = DZ_THROTTLE;
	gJoy[JOY_LIFT_DRIVER].deadzone = DZ_LIFT;
	gJoy[JOY_LIFT_PARTNER].deadzone = DZ_LIFT;
	gJoy[JOY_ARM_DRIVER].deadzone = DZ_ARM;
	gJoy[JOY_ARM_PARTNER].deadzone = DZ_ARM;

	enableJoystick(JOY_TURN);
	enableJoystick(JOY_THROTTLE);
	enableJoystick(JOY_LIFT_DRIVER);
	enableJoystick(JOY_LIFT_PARTNER);
	enableJoystick(JOY_ARM_DRIVER);
	enableJoystick(JOY_ARM_PARTNER);
	enableJoystick(BTN_MOBILE_TOGGLE);
	enableJoystick(BTN_MOBILE_MIDDLE);
	enableJoystick(BTN_MACRO_ZERO);
	enableJoystick(BTN_MACRO_STACK);
	enableJoystick(BTN_MACRO_LOADER);
	enableJoystick(BTN_MACRO_PREP);
	enableJoystick(BTN_MACRO_STATIONARY);
	enableJoystick(BTN_MACRO_PRELOAD);
	enableJoystick(BTN_MACRO_PICKUP);
	enableJoystick(BTN_MACRO_CANCEL);
	enableJoystick(BTN_MACRO_INC);
	enableJoystick(BTN_MACRO_DEC);
	MIRROR(BTN_MOBILE_TOGGLE);
	MIRROR(BTN_MOBILE_MIDDLE);
	MIRROR(BTN_MACRO_ZERO);
	MIRROR(BTN_MACRO_STACK);
	MIRROR(BTN_MACRO_PICKUP);
	MIRROR(BTN_MACRO_CANCEL);
	MIRROR(BTN_MACRO_INC);
	MIRROR(BTN_MACRO_DEC);
	MIRROR(Btn7L);
	MIRROR(Btn8R);
}

// This function gets called every 25ms during disabled (DO NOT PUT BLOCKING CODE IN HERE)
void disabled()
{
	sCycleData cycle;
	initCycle(cycle, 25, "disabled");
	while (true) {
		updateSensorInputs();
		selectAuto();
		handleLcd();
		endCycle(cycle);
	}
}

// This task gets started at the begining of the autonomous period
void autonomous()
{
	gAutoTime = nPgmTime;
	writeDebugStreamLine("Auto start %d", gAutoTime);

	startSensors();

	stackReset();
	liftReset();
	armReset();
	mobileReset();
	autoSimpleReset();

	gKillDriveOnTimeout = true;
	gSetTimedOut = true;
	gTimedOut = false;

	//resetPosition(gPosition);
	//resetQuadratureEncoder(trackL);
	//resetQuadratureEncoder(trackR);
	//resetQuadratureEncoder(trackB);

	autoMotorSensorUpdateTaskAsync();
	trackPositionTaskAsync();

	runAuto();

	writeDebugStreamLine("Auto: %d ms", nPgmTime - gAutoTime);

	setDrive(0, 0);
	stackReset();
	liftReset();
	armReset();
	mobileReset();
	autoSimpleReset();

	return_t;
}

// This task gets started at the beginning of the usercontrol period
void usercontrol()
{
	gSetTimedOut = false;
	gTimedOut = false;

	startSensors(); // Initilize the sensors
#ifdef TRACK_IN_DRIVER
	initCycle(gMainCycle, 15, "main");
#else
	initCycle(gMainCycle, 10, "main");
#endif

	updateSensorInput(jmpSkills);

#ifdef TRACK_IN_DRIVER
	trackPositionTaskAsync();
#endif

	//if (gSensor[jmpSkills].value)
	//{
	//	autoMotorSensorUpdateTaskAsync();
	//	trackPositionTaskAsync();

	//	driverSkillsStart();

	//	trackPositionTaskKill();
	//	autoMotorSensorUpdateTaskKill();

	//	mobileSet(mobileTop);

	//	armSet(armHold);
	//}

	stackReset();
	liftReset();
	armReset();
	mobileReset();

	gKillDriveOnTimeout = true;
	gDriveManual = true;
	gMobileCheckLift = true;
	gMobileAutoEnabled = true;
	gStack = false;
	gLoader = false;

	while (true)
	{
		updateSensorInputs();
		updateJoysticks();

		selectAuto();

		handleDrive();
		handleLift();
		handleArm();
		handleMobile();
		handleMacros();

		handleLcd();

		if (RISING(BTN_MACRO_CANCEL))
		{
			writeDebugStreamLine("%f", abs((2.785 * (gSensor[trackL].value - gSensor[trackR].value)) / (360 * 4)));
			resetQuadratureEncoder(trackL);
			resetQuadratureEncoder(trackR);
		}

		updateSensorOutputs();
		updateMotors();
		endCycle(gMainCycle);
	}

	return_t;
}
