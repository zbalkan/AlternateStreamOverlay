#pragma once
#include <windows.h>

// Non-throwing Win32 lock guard suitable for in-process shell extensions.
class ExclusiveSrwLock
{
public:
    explicit ExclusiveSrwLock(SRWLOCK& lock) : lock_(lock)
    {
        AcquireSRWLockExclusive(&lock_);
    }

    ~ExclusiveSrwLock()
    {
        ReleaseSRWLockExclusive(&lock_);
    }

    ExclusiveSrwLock(const ExclusiveSrwLock&) = delete;
    ExclusiveSrwLock& operator=(const ExclusiveSrwLock&) = delete;

private:
    SRWLOCK& lock_;
};
