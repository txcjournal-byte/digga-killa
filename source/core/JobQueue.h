#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace digga
{

/** A single background worker for everything heavy (decoding, analysis,
    generation, KILL). Jobs run in the order they were added and never on the
    audio thread. */
class JobQueue
{
public:
    JobQueue();
    ~JobQueue();

    void add (std::function<void()> job);

    /** Drops queued jobs and waits for the running one to finish. */
    void cancelAll();

private:
    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JobQueue)
};

} // namespace digga
