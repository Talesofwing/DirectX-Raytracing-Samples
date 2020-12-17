#pragma once

// Helper class for animation and simulation timing.
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

    // Get the current frameRate.
    UINT32 GetFramePerSecond () const { return m_FramesPerSecond; }

    // Set whether to use fixed or variable timestep mode.
    void SetTargetElapsedTicks (UINT64 targetElapsed) { m_TargetElapsedTicks = targetElapsed; }
    void SetTargetElapsedSeconds (double targetElapsed) { m_TargetElapsedTicks = SecondsToTicks (targetElapsed); }

    // Integer format represents time using 10,000,000 ticks per second.
    static const UINT64 TicksPerSecond = 10000000;

    static double TicksToSeconds (UINT64 ticks) { return static_cast<double> (ticks) / TicksPerSecond; }
    static UINT64 SecondsToTicks (double seconds) { return static_cast<UINT64> (seconds * TicksPerSecond); }

    // After an intentional timing discontinuity (for instance a blocking IO operation)
    // call this to avoid having the fixed timestep logic attempt a set of catch-up
    // Update calls.

    void ResetElapsedTime () {
        QueryPerformanceCounter (&m_QpcLastTime);

        m_LeftOverTicks = 0;
        m_FramesPerSecond = 0;
        m_FramesThisSecond = 0;
        m_QpcSecondCounter = 0;
    }

    typedef void (*LPUPDATEFUNC) (void);

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

        // Convert QPC units into a canonical tick format. This cannot overflow due to the previous clamp.
        timeDelta *= TicksPerSecond;
        timeDelta /= m_QpcFrequency.QuadPart;

        UINT32 lastFrameCount = m_FrameCount;

        if (m_IsFixedTimeStep) {
            // Fixed timestep update logic

            // If the app is running very close to the target elapsed time (within 1/4 of a millisecond) just clamp
            // the clock to exactly match the target value. This prevents tiny and irrelevant errors
            // from accumulating over time. Without this clamping, a game that requested a 60 fps
            // fixed update, running with vsync enabled on a 59.94 NTSC display, would eventually
            // accumulate enough tiny errors that it would drop a frame. It is better to just round
            // samll deviations down to zero to leave things running smoothly.

            if (abs (static_cast<int>(timeDelta - m_TargetElapsedTicks)) < TicksPerSecond / 4000) {
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
            // variable timestep update logic.
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
    // Source timing data uses QPC units.
    LARGE_INTEGER m_QpcFrequency;
    LARGE_INTEGER m_QpcLastTime;
    UINT64 m_QpcMaxDelta;

    // Derived timing data uses a canonical
    UINT64 m_ElapsedTicks;
    UINT64 m_TotalTicks;
    UINT64 m_LeftOverTicks;
    
    // Members for tracking the framerate.
    UINT32 m_FrameCount;
    UINT32 m_FramesPerSecond;
    UINT32 m_FramesThisSecond;
    UINT64 m_QpcSecondCounter;

    // Members for configuring fixed timestep mode.
    bool m_IsFixedTimeStep;
    UINT64 m_TargetElapsedTicks;
};