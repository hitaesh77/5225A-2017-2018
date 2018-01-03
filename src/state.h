#define PUSH_STATE(machine, state, code) \
{ \
	int machine##Last = _##machine##State; \
	machine##Set(state); \
	{ code } \
	machine##Set(machine##Last); \
}

#define IS_CONFIGURED(machine) (_##machine##State != -1)

#define MAKE_MACHINE(machine, states, base, handler) \
states _##machine##State = -1; \
void machine##Internal(states state) \
{ \
	switch (_##machine##State = state) \
	handler \
} \
NEW_ASYNC_VOID_1(machine##Internal, states); \
void machine##Setup() \
{ \
	if (IS_CONFIGURED(machine)) \
		machine##InternalKill(); \
	machine##InternalAsync(base); \
	writeDebugStreamLine("Initialized state machine " #machine " in base state " #base); \
} \
void machine##Set(states state) \
{ \
	machine##InternalKill(); \
	writeDebugStreamLine(#machine " %d -> %d", _##machine##State, state); \
	machine##InternalAsync(state); \
} \
void machine##Reset() \
{ \
	machine##Set(base); \
}
