#include "CppUnitTest.h"
#include "altstrm_common.h"
#include <algorithm>
#include <string>
#include <stdexcept>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    struct TemporaryFile
    {
        wchar_t path[MAX_PATH] = {};

        TemporaryFile()
        {
            wchar_t directory[MAX_PATH] = {};
            if (!GetTempPathW(MAX_PATH, directory) ||
                !GetTempFileNameW(directory, L"ads", 0, path))
                throw std::runtime_error("Unable to create temporary file");
        }

        ~TemporaryFile()
        {
            DeleteFileW(path);
        }

        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;
    };

    void CreateStream(const std::wstring& name)
    {
        HandleW file(CreateFileW(name.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        Assert::IsTrue(file.get() != INVALID_HANDLE_VALUE,
            L"Failed to create alternate stream");
    }
}

namespace AlternateStreamTests
{
    TEST_CLASS(StreamReaderTests)
    {
    public:
        TEST_METHOD(RejectsInvalidHandlesAndPaths)
        {
            AlternateStreamReader reader;
            Assert::IsFalse(reader.HasAlternateStreams(static_cast<HANDLE>(nullptr)));
            Assert::IsFalse(reader.HasAlternateStreams(INVALID_HANDLE_VALUE));
            Assert::IsFalse(reader.HasAlternateStreams(static_cast<LPCWSTR>(nullptr)));
            Assert::IsFalse(reader.HasAlternateStreams(L""));
            Assert::IsTrue(reader.ListAlternateStreams(static_cast<HANDLE>(nullptr)).empty());
            Assert::IsTrue(reader.ListAlternateStreams(INVALID_HANDLE_VALUE).empty());
            Assert::IsTrue(reader.ListAlternateStreams(static_cast<LPCWSTR>(nullptr)).empty());
            Assert::IsTrue(reader.ListAlternateStreams(L"").empty());
        }

        TEST_METHOD(DistinguishesUnnamedFromNamedStreams)
        {
            TemporaryFile fixture;
            AlternateStreamReader reader;
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path));

            const std::wstring namedPath = std::wstring(fixture.path) + L":unit_test";
            CreateStream(namedPath);
            Assert::IsTrue(reader.HasAlternateStreams(fixture.path));

            const auto names = reader.ListAlternateStreams(fixture.path);
            Assert::IsTrue(std::find(names.begin(), names.end(),
                L":unit_test:$DATA") != names.end());

            Assert::IsTrue(DeleteFileW(namedPath.c_str()) != FALSE);
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path));
        }
    };
}
