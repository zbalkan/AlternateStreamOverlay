#include "stdafx.h"
#include "altstrm_common.h"

bool hasAlternateStreams(LPCWSTR path)
{
    const AlternateStreamReader reader;
    return reader.HasAlternateStreams(path);
}
