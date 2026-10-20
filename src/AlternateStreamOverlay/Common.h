#pragma once

#include <vector>
#include <string>
#include "altstrm_common.h"

bool hasAlternateStreams(LPCWSTR pwszPath);
std::vector<std::wstring> listAlternateStreams(LPCWSTR pwszPath);
