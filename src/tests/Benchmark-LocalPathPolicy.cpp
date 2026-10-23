// Temporary K3 baseline benchmark. Remove after collecting CI results.
#include "../AlternateStreamOverlay/LocalPathPolicy.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int wmain()
{
    constexpr int FileCount = 10000;
    constexpr int Samples = 3;
    WCHAR directory[MAX_PATH] = {};
    WCHAR path[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, directory) ||
        !GetTempFileNameW(directory, L"aso", 0, path))
        return 1;
    DeleteFileW(path);
    if (!CreateDirectoryW(path, nullptr))
        return 1;

    const std::wstring root(path);
    std::vector<std::wstring> files;
    files.reserve(FileCount);
    for (int i = 0; i < FileCount; ++i)
    {
        const auto file = root + L"\\item-" + std::to_wstring(i) + L".txt";
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            for (const auto& created : files)
                DeleteFileW(created.c_str());
            RemoveDirectoryW(root.c_str());
            return 2;
        }
        CloseHandle(handle);
        files.emplace_back(file);
    }

    using clock = std::chrono::steady_clock;
    std::vector<double> durations;
    durations.reserve(FileCount * Samples);
    int accepted = 0;
    for (int pass = 0; pass < Samples; ++pass)
    {
        // The policy's 250-ms thread-local cache naturally expires during
        // longer passes; do not reset it between measurements.
        for (const auto& file : files)
        {
            const auto begin = clock::now();
            const bool eligible = CanEnumerateExplorerPath(file.c_str());
            const auto end = clock::now();
            accepted += eligible ? 1 : 0;
            durations.push_back(
                std::chrono::duration<double, std::micro>(end - begin).count());
        }
    }
    std::sort(durations.begin(), durations.end());
    const auto percentile = [&](double fraction)
    {
        return durations[static_cast<size_t>(
            fraction * static_cast<double>(durations.size() - 1))];
    };
    std::printf("configuration,files,passes,calls,accepted,p50_us,p95_us,p99_us,max_us\n");
    #ifdef ASO_BENCHMARK_NO_CACHE
    constexpr const char* configuration = "no_cache_no_subst";
#elif defined(ASO_BENCHMARK_NO_SUBST)
    constexpr const char* configuration = "cache_no_subst";
#else
    constexpr const char* configuration = "baseline";
#endif
    std::printf("%s,%d,%d,%zu,%d,%.3f,%.3f,%.3f,%.3f\n",
        configuration, FileCount, Samples, durations.size(), accepted, percentile(0.50),
        percentile(0.95), percentile(0.99), durations.back());

    for (const auto& file : files)
        DeleteFileW(file.c_str());
    RemoveDirectoryW(root.c_str());
    return accepted == FileCount * Samples ? 0 : 3;
}
