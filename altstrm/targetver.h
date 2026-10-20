#pragma once

// Windows 10 is the minimum supported operating system.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef WINVER
#define WINVER 0x0A00
#endif

// Reject accidental builds for Windows releases predating Windows 10.
#if _WIN32_WINNT < 0x0A00 || WINVER < 0x0A00
#error AlternateStreamOverlay requires Windows 10 or later.
#endif
