/* Functions */
bool correctBtnIn(tSensors sen)
{
	int value = SensorValue[sen];
	return value >= gSensor[sen].dgtMin && value <= gSensor[sen].dgtMax;
}

void updateSensorOutput(tSensors sen)
{
	SensorValue[sen] = gSensor[sen].lstValue = gSensor[sen].value;
}

void updateSensorOutputs()
{
	for (ubyte i = 0; i < kNumbOfTotalSensors; ++i)
	{
		if (gSensor[i].cls == snclsOutput)
			updateSensorOutput(i);
	}
}

void updateSensorInput(tSensors sen)
{
	gSensor[sen].lstValue = gSensor[sen].value;

	switch (gSensor[sen].mode)
	{
	case snmdDgtIn:
		gSensor[sen].value = correctBtnIn(sen);
		break;
	case snmdInverted:
		gSensor[sen].value = !SensorValue[sen];
		break;
	default:
		gSensor[sen].value = SensorValue[sen];
		break;
	}

#ifdef CHECK_POTI_JUMPS
	if (SensorType[sen] == sensorPotentiometer && abs(gSensor[sen].value - gSensor[sen].lstValue) > 400 && ++gSensor[sen].filterAcc < 10)
	{
		if (gSensor[sen].filterAcc == 1)
			writeDebugStreamLine("%d port %d jumped from %d to %d", nPgmTime, sen - port1 + 1, gSensor[sen].lstValue, gSensor[sen].value);
		gSensor[sen].value = gSensor[sen].lstValue;
	}
	else
		gSensor[sen].filterAcc = 0;
#endif
}

void updateSensorInputs()
{
	for (ubyte i = 0; i < kNumbOfTotalSensors; ++i)
	{
		if (gSensor[i].cls == snclsInput)
			updateSensorInput(i);
	}
}

tSensorClass checkSenClass(tSensors sen)
{
	if (SensorType[sen] == sensorNone)
		return snclsNone;
	else if (SensorType[sen] == sensorDigitalOut || SensorType[sen] == sensorLEDtoVCC)
		return snclsOutput;
	else
		return snclsInput;
}

void setupDgtIn(tSensors sen, int min, int max)
{
	gSensor[sen].mode = snmdDgtIn;
	gSensor[sen].dgtMin = min;
	gSensor[sen].dgtMax = max;
}

void setupInvertedSen(tSensors sen)
{
	gSensor[sen].mode = snmdInverted;
}

void resetQuadratureEncoder(tSensors sen)
{
	gSensor[sen].value = 0;
	SensorValue[sen] = 0;
}

bool safetyCheck(tSensors sen, unsigned long failedTime, float failedVal, unsigned long safetyMovingTime)
{
	unsigned long curTime = nPgmTime;
	tHog();
	if (curTime - gSensor[sen].failedCheckTime > 0)
	{
		if (curTime - gSensor[sen].failedCheckTime == 0) return gSensor[sen].failed && curTime - gSensor[sen].failedStartTime >= failedTime;
		int senVal = gSensor[sen].value;
		float val = abs((float)(senVal - gSensor[sen].safetyVal) / (float)(curTime - gSensor[sen].failedCheckTime));
		gSensor[sen].failedCheckTime = nPgmTime;
		if (val < failedVal && curTime - gSensor[sen].safetyStartTime > safetyMovingTime)
		{
			if (!gSensor[sen].failed)
			{
				gSensor[sen].failed = true;
				gSensor[sen].failedStartTime = curTime;
			}
		}
		else
			gSensor[sen].failed = false;
		gSensor[sen].safetyVal = senVal;
	}
	tRelease();
	return gSensor[sen].failed && curTime - gSensor[sen].failedStartTime >= failedTime;
}

void safetyClear(tSensors sen)
{
	gSensor[sen].failed = false;
	gSensor[sen].failedCheckTime = nPgmTime;
	gSensor[sen].safetyStartTime = nPgmTime;
}

void safetySet(tSensors sen)
{
	if (!gSensor[sen].failed)
	{
		gSensor[sen].failed = true;
		gSensor[sen].failedStartTime = nPgmTime;
	}
	gSensor[sen].failedCheckTime = nPgmTime;
}

void velocityCheck(tSensors sen)
{
	tHog();
	unsigned long time = nPgmTime;
	if (!gSensor[sen].velCount || time - gSensor[sen].velData[gSensor[sen].velCount - 1].timestamp >= 20)
	{
		int sensor = gSensor[sen].value;
		if (gSensor[sen].velCount == SENSOR_VEL_POINT_COUNT)
		{
			gSensor[sen].velCount = SENSOR_VEL_POINT_COUNT - 1;
			memmove(&gSensor[sen].velData[0], &gSensor[sen].velData[1], sizeof(sSensorVelPoint) * (SENSOR_VEL_POINT_COUNT - 1));
		}
		sSensorVelPoint& data = gSensor[sen].velData[gSensor[sen].velCount++];
		data.value = sensor;
		data.timestamp = time;
		if (gSensor[sen].velCount > 1)
		{
			gSensor[sen].lstVelocity = gSensor[sen].velocity;
			sSensorVelPoint& old = gSensor[sen].velData[0];
			gSensor[sen].velocity = (float)(sensor - old.value) / (float)(time - old.timestamp);
			gSensor[sen].velGood = true;
		}
	}
	tRelease();
}

void velocityClear(tSensors sen)
{
	tHog();
	gSensor[sen].velCount = 0;
	gSensor[sen].velocity = gSensor[sen].lstVelocity = 0;
	gSensor[sen].velGood = false;
	velocityCheck(sen);
	tRelease();
}

void startSensor(tSensors sen)
{
	if (gSensor[sen].cls == snclsInput)
		gSensor[sen].value = gSensor[sen].lstValue = SensorValue[sen];
}

void startSensors()
{
	for (ubyte i = 0; i < kNumbOfTotalSensors; ++i)
		startSensor(i);
}

void setupSensors()
{
	// Setup the pointers for the sensor values and set the base type
	for (ubyte i = 0; i < kNumbOfTotalSensors; ++i)
	{
		gSensor[i].cls = checkSenClass(i);
		gSensor[i].port = (tSensors)i;

		gSensor[i].lstVelocity = 0;
		gSensor[i].velocity = 0;
		gSensor[i].velGood = false;
		gSensor[i].velCount = 0;

#ifdef CHECK_POTI_JUMPS
		gSensor[i].filterAcc = 0;
#endif

		startSensor(i);
	}
}
