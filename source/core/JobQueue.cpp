#include "core/JobQueue.h"

namespace digga
{

JobQueue::JobQueue()
    : pool (juce::ThreadPoolOptions{}
                .withThreadName ("DiggaKilla worker")
                .withNumberOfThreads (1))
{
}

JobQueue::~JobQueue()
{
    cancelAll();
}

void JobQueue::add (std::function<void()> job)
{
    pool.addJob (std::move (job));
}

void JobQueue::cancelAll()
{
    pool.removeAllJobs (true, 10000);
}

} // namespace digga
