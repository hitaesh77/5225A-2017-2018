CONFIGS
{
	CONFIG_INIT

	CONFIG(DRIVER(MANUAL_X_Y))
	{
		DEADZONE(Ch1, 10)
		DEADZONE(Ch3, 10)

		HIGH(Ch1, gArmTarget.x += (float) value / 500.0;)
		HIGH(Ch3, gArmTarget.y += (float) value / 500.0;)
	}
}
