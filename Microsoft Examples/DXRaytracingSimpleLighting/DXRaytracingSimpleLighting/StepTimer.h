#pragma once

class StepTimer {
public:
	StepTimer () :
		m_ElapsedTicks (0),
		m_TotalTicks (0),
		m_LeftOverTicks (0),
		m_FrameCount (0),
		m_FramesPerSecond (0),
		m_FramesThisSecond (0),
		m_QpcSecondCounter (0),
		m_IsFixedTimeStep (false),
		m_TargetElapsedTicks (TicksPerSecond / 60) {
		QueryPerformanceFrequency (&m_QpcFrequency);
		QueryPerformanceCounter (&m_QpcLastTime);

		// Initialize max delta to 1/10 of a second.
		m_QpcMaxDelta = m_QpcFrequency.QuadPart / 10;
	}

	// Get elapsed time since the previous Update call.
	UINT64 GetElapsedTicks () const { return m_ElapsedTicks; }
	double GetElapsedSeconds () const { return TicksToSeconds (m_ElapsedTicks); }

	// Get total time since the start of the program.
	UINT64 GetTotalTicks () const { return m_TotalTicks; }
	double GetTotalSeconds () const { return TicksToSeconds (m_TotalTicks); }

	// Get total number of updates since start of the program.
	UINT32 GetFrameCount () const { return m_FrameCount; }

	// Get the current framerate.
	UINT32 GetFramePerSecond () const { return m_FramesPerSecond; }

	// Set whether to use fixed or variable timestep mode.
	void SetFixedTimeStep (bool isFixedTimeStep) { m_IsFixedTimeStep = isFixedTimeStep; }

	// Set how often to call Update when in fixed timestep mode.
	void SetTargetElapsedTicks (UINT64 targetElapsed) { m_TargetElapsedTicks = targetElapsed; }
	void SetTargetElapsedSeconds (double targetElapsed) { m_TargetElapsedTicks = SecondsToTicks (targetElapsed); }

	// Integer format represents time using 10,000,000 ticks per second.
	static const UINT64 TicksPerSecond = 10000000;

	static double TicksToSeconds (UINT64 ticks) { return static_cast<double> (ticks) / TicksPerSecond; }
	static UINT64 SecondsToTicks (double seconds) { return static_cast<UINT64> (seconds * TicksPerSecond); }

	void ResetElapsedTime () {
		QueryPerformanceCounter (&m_QpcLastTime);

		m_LeftOverTicks = 0;
		m_FramesPerSecond = 0;
		m_FramesThisSecond = 0;
		m_QpcSecondCounter = 0;
	}

	typedef void(*LPUPDATEFUNC) (void);

	// Update timer state, calling the specified Update function the appropriate number of times.
	void Tick (LPUPDATEFUNC update = nullptr) {
		// Query the current time.
		LARGE_INTEGER currentTime;

		QueryPerformanceCounter (&currentTime);

		UINT64 timeDelta = currentTime.QuadPart - m_QpcLastTime.QuadPart;

		m_QpcLastTime = currentTime;
		m_QpcSecondCounter += timeDelta;

		// Clamp excessively large time deltas (e.g. after paused in the debugger).
		if (timeDelta > m_QpcMaxDelta) {
			timeDelta = m_QpcMaxDelta;
		}

		// Convert QPC units into a canonical tick format. This can't overflow due to the previous clamp.
		timeDelta *= TicksPerSecond;
		timeDelta /= m_QpcFrequency.QuadPart;

		UINT32 lastFrameCount = m_FrameCount;

		if (m_IsFixedTimeStep) {
			if (abs (static_cast<int> (timeDelta - m_TargetElapsedTicks)) < TicksPerSecond / 4000) {
				timeDelta = m_TargetElapsedTicks;
			}

			m_LeftOverTicks += timeDelta;

			while (m_LeftOverTicks >= m_TargetElapsedTicks) {
				m_ElapsedTicks = m_TargetElapsedTicks;
				m_TotalTicks += m_TargetElapsedTicks;
				m_LeftOverTicks -= m_TargetElapsedTicks;
				m_FrameCount++;

				if (update) {
					update ();
				}
			}
		} else {
			m_ElapsedTicks = timeDelta;
			m_TotalTicks += timeDelta;
			m_LeftOverTicks = 0;
			m_FrameCount++;

			if (update) {
				update ();
			}
		}

		// Track the current framerate.
		if (m_FrameCount != lastFrameCount) {
			m_FramesThisSecond++;
		}

		if (m_QpcSecondCounter >= static_cast<UINT64> (m_QpcFrequency.QuadPart)) {
			m_FramesPerSecond = m_FramesThisSecond;
			m_FramesThisSecond = 0;
			m_QpcSecondCounter %= m_QpcFrequency.QuadPart;
		}
	}

private:
	LARGE_INTEGER m_QpcFrequency;
	LARGE_INTEGER m_QpcLastTime;
	UINT64 m_QpcMaxDelta;

	UINT64 m_ElapsedTicks;
	UINT64 m_TotalTicks;
	UINT64 m_LeftOverTicks;

	UINT32 m_FrameCount;
	UINT32 m_FramesPerSecond;
	UINT32 m_FramesThisSecond;
	UINT64 m_QpcSecondCounter;

	bool m_IsFixedTimeStep;
	UINT64 m_TargetElapsedTicks;
};