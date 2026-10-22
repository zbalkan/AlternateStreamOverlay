#include "stdafx.h"
#include "altstrm_common.h"

// The reader has no OS-dependent initialization or global state.
AlternateStreamReader g_streamReader;

bool hasAlternateStreams(LPCWSTR path)
{
    return g_streamReader.HasAlternateStreams(path);
}
