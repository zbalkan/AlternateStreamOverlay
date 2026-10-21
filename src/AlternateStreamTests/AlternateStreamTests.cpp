#include "CppUnitTest.h"
#include "altstrm_common.h"
#include "stream_info_detail.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    struct TemporaryFile
    {
        // GetTempFileNameW requires a MAX_PATH-sized output buffer.
        // Use dynamically managed storage and validate the temporary directory length.
        std::vector<wchar_t> path = std::vector<wchar_t>(MAX_PATH);

        TemporaryFile()
        {
            std::vector<wchar_t> directory(MAX_PATH);
            const DWORD length = GetTempPathW(
                static_cast<DWORD>(directory.size()), directory.data());
            if (length == 0 || length >= directory.size() ||
                !GetTempFileNameW(directory.data(), L"ads", 0, path.data()))
                throw std::runtime_error("Unable to create temporary file");
        }

        ~TemporaryFile()
        {
            DeleteFileW(path.data());
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
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));

            const std::wstring namedPath = std::wstring(fixture.path.data()) + L":unit_test";
            CreateStream(namedPath);
            Assert::IsTrue(reader.HasAlternateStreams(fixture.path.data()));

            const auto names = reader.ListAlternateStreams(fixture.path.data());
            Assert::IsTrue(std::find(names.begin(), names.end(),
                L":unit_test:$DATA") != names.end());

            Assert::IsTrue(DeleteFileW(namedPath.c_str()) != FALSE);
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));
        }
    };

    TEST_CLASS(StreamPreviewTests)
    {
    public:
        TEST_METHOD(ValidUtf8AndBinaryFallback)
        {
            std::wstring text;
            const BYTE valid[] = { 'A', 0xE2, 0x82, 0xAC };
            Assert::IsTrue(TryDecodeUtf8Preview(valid, sizeof(valid), false, text));
            Assert::AreEqual(L"A\u20AC", text.c_str());

            const BYTE invalid[] = { 'A', 0xC0, 0xAF };
            Assert::IsFalse(TryDecodeUtf8Preview(invalid, sizeof(invalid), false, text));
            const BYTE nul[] = { 'A', 0, 'B' };
            Assert::IsFalse(TryDecodeUtf8Preview(nul, sizeof(nul), false, text));
        }

        TEST_METHOD(TruncationOnlyDiscardsIncompleteUtf8Suffix)
        {
            std::wstring text;
            const BYTE split[] = { 'A', 0xE2, 0x82 };
            Assert::IsTrue(TryDecodeUtf8Preview(split, sizeof(split), true, text));
            Assert::AreEqual(L"A", text.c_str());
            Assert::IsFalse(TryDecodeUtf8Preview(split, sizeof(split), false, text));

            const BYTE malformed[] = { 'A', 0xF0, 0x80 };
            Assert::IsFalse(TryDecodeUtf8Preview(malformed, sizeof(malformed), true, text));
            const BYTE fourByteSplit[] = { 'A', 0xF0, 0x9F, 0x98 };
            Assert::IsTrue(TryDecodeUtf8Preview(fourByteSplit, sizeof(fourByteSplit), true, text));
            Assert::AreEqual(L"A", text.c_str());
            const BYTE loneContinuation[] = { 'A', 0x80 };
            Assert::IsFalse(TryDecodeUtf8Preview(loneContinuation, sizeof(loneContinuation), true, text));
            const BYTE invalidSecond[] = { 'A', 0xE0, 0x80 };
            Assert::IsFalse(TryDecodeUtf8Preview(invalidSecond, sizeof(invalidSecond), true, text));
            const BYTE complete[] = { 'A', 0xE2, 0x82, 0xAC };
            Assert::IsTrue(TryDecodeUtf8Preview(complete, sizeof(complete), true, text));
            Assert::AreEqual(L"A\u20AC", text.c_str());
            const BYTE invalidInside[] = { 0xFF, 'A', 0xE2 };
            Assert::IsFalse(TryDecodeUtf8Preview(invalidInside, sizeof(invalidInside), true, text));
        }

        TEST_METHOD(ExactPreviewBoundary)
        {
            constexpr size_t PreviewLimit = 65535;
            std::vector<BYTE> data(PreviewLimit, 'A');
            std::wstring text;
            Assert::IsTrue(TryDecodeUtf8Preview(data.data(), data.size(), false, text));
            Assert::AreEqual(PreviewLimit, text.size());

            // 65535-byte display limit cuts off the final byte of a euro sign.
            data[PreviewLimit - 2] = 0xE2;
            data[PreviewLimit - 1] = 0x82;
            Assert::IsTrue(TryDecodeUtf8Preview(data.data(), data.size(), true, text));
            Assert::AreEqual(PreviewLimit - 2, text.size());
            Assert::IsFalse(TryDecodeUtf8Preview(data.data(), data.size(), false, text));
        }
    };


    TEST_CLASS(HexPreviewTests)
    {
    public:
        TEST_METHOD(EmptyAndPartialRows)
        {
            Assert::IsTrue(FormatHexPreview(nullptr, 0).empty());
            const BYTE data[] = { 0, 'A', 0x7F };
            const std::wstring expected =
                L"00000000  00 41 7F                 |.A.     |\r\n";
            Assert::AreEqual(expected.c_str(),
                FormatHexPreview(data, sizeof(data)).c_str());
        }

        TEST_METHOD(EightByteRowsAndOffsets)
        {
            const BYTE data[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I' };
            const std::wstring expected =
                L"00000000  41 42 43 44 45 46 47 48  |ABCDEFGH|\r\n"
                L"00000008  49                       |I       |\r\n";
            Assert::AreEqual(expected.c_str(),
                FormatHexPreview(data, sizeof(data)).c_str());
        }

        TEST_METHOD(PreviewLimitAndFinalRow)
        {
            constexpr size_t PreviewLimit = 65535;
            std::vector<BYTE> data(PreviewLimit + 1, 'A');
            const std::wstring formatted = FormatHexPreview(data.data(), data.size());
            constexpr size_t RowWidth = 47;
            constexpr size_t RowCount = (PreviewLimit + 7) / 8;
            Assert::AreEqual(RowCount * RowWidth, formatted.size());
            Assert::AreEqual(
                L"0000FFF8  41 41 41 41 41 41 41     |AAAAAAA |\r\n",
                formatted.substr(formatted.size() - RowWidth).c_str());
        }
    };


    TEST_CLASS(StreamInfoRecordTests)
    {
    public:
        TEST_METHOD(RejectsMissingOrTruncatedHeaders)
        {
            stream_info_detail::Record record = {};
            std::vector<BYTE> buffer(64, 0);
            Assert::IsFalse(stream_info_detail::ReadRecord(nullptr, 64, 0, record));
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), 8, 0, record));
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 65, record));
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 1, record));
        }

        TEST_METHOD(RejectsMalformedLengthsAndOffsets)
        {
            constexpr size_t Header = offsetof(FILE_STREAM_INFO, StreamName);
            std::vector<BYTE> buffer(64, 0);
            auto* entry = reinterpret_cast<FILE_STREAM_INFO*>(buffer.data());
            entry->StreamNameLength = sizeof(WCHAR);
            entry->StreamName[0] = L'A';
            stream_info_detail::Record record = {};
            Assert::IsTrue(stream_info_detail::ReadRecord(
                buffer.data(), buffer.size(), 0, record));
            Assert::AreEqual(static_cast<size_t>(sizeof(WCHAR)), record.nameBytes);

            entry->StreamNameLength = 0;
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            entry->StreamNameLength = 1;
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            entry->StreamNameLength = static_cast<DWORD>(buffer.size());
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));

            entry->StreamNameLength = sizeof(WCHAR);
            entry->NextEntryOffset = 1;
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            entry->NextEntryOffset = static_cast<DWORD>(Header);
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            entry->NextEntryOffset = 32;
            entry->StreamNameLength = 16;
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            entry->StreamNameLength = sizeof(WCHAR);
            entry->NextEntryOffset = 128;
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
        }

        TEST_METHOD(ReadsAlignedChainedRecords)
        {
            std::vector<BYTE> buffer(64, 0);
            auto* first = reinterpret_cast<FILE_STREAM_INFO*>(buffer.data());
            first->NextEntryOffset = 32;
            first->StreamNameLength = sizeof(WCHAR);
            first->StreamName[0] = L'A';

            auto* second = reinterpret_cast<FILE_STREAM_INFO*>(buffer.data() + 32);
            second->NextEntryOffset = 0;
            second->StreamNameLength = sizeof(WCHAR);
            second->StreamName[0] = L'B';

            stream_info_detail::Record record = {};
            Assert::IsTrue(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record));
            Assert::AreEqual(static_cast<size_t>(32), record.nextOffset);
            Assert::AreEqual(L'A', record.name[0]);
            Assert::IsTrue(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 32, record));
            Assert::AreEqual(static_cast<size_t>(0), record.nextOffset);
            Assert::AreEqual(L'B', record.name[0]);
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 48, record));
        }
    };

}
