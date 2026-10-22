#include "CppUnitTest.h"
#include "altstrm_common.h"
#include "stream_info_detail.h"
#include "../AlternateStreamOverlay/LocalPathPolicy.h"
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>
#include <ShlObj.h>
#include <wrl/client.h>

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

    // Minimal CF_HDROP provider; GetData supplies a fresh, caller-owned medium.
    class DropDataObject final : public IDataObject
    {
    public:
        enum class MediumCase { Valid, NullGlobal, WrongMedium };

        explicit DropDataObject(const std::wstring& path,
            MediumCase mediumCase = MediumCase::Valid)
            : path_(path), mediumCase_(mediumCase) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
        {
            if (object == nullptr) return E_POINTER;
            *object = nullptr;
            if (iid == __uuidof(IUnknown) || iid == __uuidof(IDataObject))
            {
                *object = static_cast<IDataObject*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = --references_;
            if (remaining == 0) delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
        {
            if (format == nullptr || medium == nullptr) return E_POINTER;
            if (format->cfFormat != CF_HDROP || format->tymed != TYMED_HGLOBAL)
                return DV_E_FORMATETC;

            *medium = {};
            if (mediumCase_ == MediumCase::NullGlobal)
            {
                medium->tymed = TYMED_HGLOBAL;
                return S_OK;
            }
            if (mediumCase_ == MediumCase::WrongMedium)
            {
                // Wrong representation and missing interface payload.
                medium->tymed = TYMED_ISTREAM;
                medium->pstm = nullptr;
                return S_OK;
            }

            // Reserve space for the path terminator and the second NUL
            // required by CF_HDROP, checking arithmetic before allocation.
            constexpr size_t maxCharacters =
                ((std::numeric_limits<size_t>::max)() - sizeof(DROPFILES)) / sizeof(WCHAR);
            if (path_.size() > maxCharacters - 2)
                return E_OUTOFMEMORY;
            const size_t bytes = sizeof(DROPFILES) +
                (path_.size() + 2) * sizeof(WCHAR);
            HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (storage == nullptr) return E_OUTOFMEMORY;
            void* memory = GlobalLock(storage);
            if (memory == nullptr)
            {
                GlobalFree(storage);
                return E_OUTOFMEMORY;
            }
            auto* drop = static_cast<DROPFILES*>(memory);
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            auto* name = reinterpret_cast<WCHAR*>(
                static_cast<BYTE*>(memory) + sizeof(DROPFILES));
            std::copy(path_.begin(), path_.end(), name);
            // GMEM_ZEROINIT supplies the terminating double NUL.
            GlobalUnlock(storage);

            *medium = {};
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = storage;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }

    private:
        std::wstring path_;
        MediumCase mediumCase_;
        ULONG references_ = 1;
    };

    // Distinguish local test fixture setup failures from test assertions.
    class OverlayDllPathError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    std::wstring GetOverlayDllPath()
    {
        // CI can provide an explicit build artifact. Test Explorer does not
        // inherit the workflow environment, so resolve the solution layout.
        std::vector<WCHAR> buffer(32768);
        const DWORD overrideLength = GetEnvironmentVariableW(
            L"ASO_OVERLAY_DLL", buffer.data(), static_cast<DWORD>(buffer.size()));
        if (overrideLength != 0)
        {
            if (overrideLength >= buffer.size())
                throw OverlayDllPathError("ASO_OVERLAY_DLL exceeds the path buffer");
            return std::wstring(buffer.data(), overrideLength);
        }

        HMODULE testModule = nullptr;
        if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CreateStream), &testModule))
            throw OverlayDllPathError("Unable to locate the test module");

        const DWORD length = GetModuleFileNameW(
            testModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
            throw OverlayDllPathError("Unable to retrieve the test module path");

        const std::wstring testPath(buffer.data(), length);
        const size_t lastSlash = testPath.find_last_of(L"\\/");
        const size_t configSlash = lastSlash == std::wstring::npos
            ? std::wstring::npos : testPath.find_last_of(L"\\/", lastSlash - 1);
        if (configSlash == std::wstring::npos)
            throw OverlayDllPathError("Unexpected MSTest output directory");

        // Default solution layout:
        // src\x64\Debug\AlternateStreamTests.dll
        // src\AlternateStreamOverlay\x64\Debug\AlternateStreamOverlay.dll
        const std::wstring config = testPath.substr(
            configSlash + 1, lastSlash - configSlash - 1);
        const std::wstring testDirectory = testPath.substr(0, lastSlash + 1);
        return testDirectory + L"..\\..\\AlternateStreamOverlay\\x64\\" +
            config + L"\\AlternateStreamOverlay.dll";
    }

    struct ScopedModule
    {
        explicit ScopedModule(LPCWSTR path) : handle(LoadLibraryW(path)) {}
        ~ScopedModule() { if (handle != nullptr) FreeLibrary(handle); }
        ScopedModule(const ScopedModule&) = delete;
        ScopedModule& operator=(const ScopedModule&) = delete;
        HMODULE handle;
    };

    Microsoft::WRL::ComPtr<IClassFactory> GetContextClassFactory(HMODULE moduleHandle)
    {
        if (moduleHandle == nullptr)
            Assert::Fail(L"Unable to load overlay DLL");

        using GetClassObject = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
        const auto getClassObject = reinterpret_cast<GetClassObject>(
            GetProcAddress(moduleHandle, "DllGetClassObject"));
        if (getClassObject == nullptr)
            Assert::Fail(L"DllGetClassObject not exported");

        // CLSID_AlternateStreamContext from AlternateStreamOverlay.idl.
        const CLSID clsid = { 0x4A26024B, 0x5499, 0x45BE,
            { 0xA2, 0x3F, 0x4A, 0x65, 0xE8, 0x88, 0x88, 0xD0 } };
        Microsoft::WRL::ComPtr<IClassFactory> factory;
        Assert::IsTrue(SUCCEEDED(getClassObject(clsid, __uuidof(IClassFactory),
            reinterpret_cast<void**>(factory.GetAddressOf()))));
        return factory;
    }

    BOOL CALLBACK CapturePropertyPage(HPROPSHEETPAGE page, LPARAM context)
    {
        *reinterpret_cast<HPROPSHEETPAGE*>(context) = page;
        return TRUE;
    }

    BOOL CALLBACK RejectPropertyPage(HPROPSHEETPAGE, LPARAM)
    {
        return FALSE;
    }
}

namespace AlternateStreamTests
{

    TEST_CLASS(ComInitializationTests)
    {
    public:
        TEST_METHOD(GetCommandStringRejectsUnsupportedFlags)
        {
            ScopedModule module(GetOverlayDllPath().c_str());
            auto factory = GetContextClassFactory(module.handle);
            Microsoft::WRL::ComPtr<IContextMenu> menu;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IContextMenu),
                reinterpret_cast<void**>(menu.GetAddressOf()))));

            WCHAR buffer[64] = {};
            auto* bytes = reinterpret_cast<LPSTR>(buffer);
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                menu->GetCommandString(0, GCS_VERBW, nullptr, bytes, 64));
            Assert::AreEqual(L"alternatestreams", buffer);
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                menu->GetCommandString(0, GCS_VERBICONW, nullptr, bytes, 64));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                menu->GetCommandString(0, GCS_VERBW | 0x40, nullptr, bytes, 64));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                menu->GetCommandString(0, GCS_VALIDATEW, nullptr, nullptr, 0));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                menu->GetCommandString(1, GCS_VALIDATEW, nullptr, nullptr, 0));
        }

        TEST_METHOD(FailedReinitializationDiscardsPreviousSelection)
        {
            const std::wstring dllPath = GetOverlayDllPath();
            ScopedModule module(dllPath.c_str());
            auto factory = GetContextClassFactory(module.handle);

            Microsoft::WRL::ComPtr<IShellExtInit> initializer;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IShellExtInit),
                reinterpret_cast<void**>(initializer.GetAddressOf()))));
            Microsoft::WRL::ComPtr<IContextMenu> menu;
            Assert::IsTrue(SUCCEEDED(initializer.As(&menu)));

            TemporaryFile fixture;
            CreateStream(std::wstring(fixture.path.data()) + L":lifecycle");
            Microsoft::WRL::ComPtr<IDataObject> data;
            data.Attach(new DropDataObject(fixture.path.data()));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));

            CMINVOKECOMMANDINFO command = {};
            command.cbSize = sizeof(command);
            command.lpVerb = MAKEINTRESOURCEA(0);
            // Missing data object must clear the previously initialized selection.
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, nullptr, nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_FAIL),
                menu->InvokeCommand(&command));

            // A successful GetData call may still supply a malformed medium.
            // Each malformed reinitialization must discard a valid selection.
            Microsoft::WRL::ComPtr<IDataObject> nullGlobal;
            nullGlobal.Attach(new DropDataObject(fixture.path.data(),
                DropDataObject::MediumCase::NullGlobal));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, nullGlobal.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_FAIL),
                menu->InvokeCommand(&command));

            Microsoft::WRL::ComPtr<IDataObject> wrongMedium;
            wrongMedium.Attach(new DropDataObject(fixture.path.data(),
                DropDataObject::MediumCase::WrongMedium));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, wrongMedium.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_FAIL),
                menu->InvokeCommand(&command));

            // Unexpected callback arguments must fail without touching COM state.
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                menu->InvokeCommand(nullptr));

            // The same COM instance must remain reusable after those failures.
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));

            // The shell property-page callback owns a COM reference until
            // DestroyPropertySheetPage invokes PSPCB_RELEASE. Cover both the
            // accepted and rejected page paths without displaying a dialog.
            Microsoft::WRL::ComPtr<IShellPropSheetExt> propertyPages;
            Assert::IsTrue(SUCCEEDED(initializer.As(&propertyPages)));
            Assert::AreEqual(static_cast<HRESULT>(E_POINTER),
                propertyPages->AddPages(nullptr, 0));

            // Leave only the test's IShellPropSheetExt reference outstanding,
            // so an unbalanced page callback cannot be masked by other refs.
            menu.Reset();
            initializer.Reset();

            HPROPSHEETPAGE acceptedPage = nullptr;
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                propertyPages->AddPages(CapturePropertyPage,
                    reinterpret_cast<LPARAM>(&acceptedPage)));
            Assert::IsTrue(acceptedPage != nullptr);
            Assert::IsTrue(DestroyPropertySheetPage(acceptedPage) != FALSE);

            // Rejection destroys the page inside AddPages and must not
            // prematurely release the COM object used by this test.
            Assert::AreEqual(static_cast<HRESULT>(E_FAIL),
                propertyPages->AddPages(RejectPropertyPage, 0));

            // After rejection, the caller's sole reference must still keep
            // the object alive and allow acquisition of another interface.
            Microsoft::WRL::ComPtr<IShellExtInit> recovered;
            Assert::IsTrue(SUCCEEDED(propertyPages.As(&recovered)));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                recovered->Initialize(nullptr, data.Get(), nullptr));
            recovered.Reset();
            propertyPages.Reset();
            factory.Reset();
            data.Reset();
        }
    };

    TEST_CLASS(EnumerationResultTests)
    {
    public:
        TEST_METHOD(ExistenceProbeMatchesNamedStreamInventory)
        {
            TemporaryFile fixture;
            AlternateStreamReader reader;
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));
            CreateStream(std::wstring(fixture.path.data()) + L":probe");
            Assert::IsTrue(reader.HasAlternateStreams(fixture.path.data()));

            const auto inventory = reader.EnumerateStreams(fixture.path.data());
            Assert::IsTrue(inventory.status == StreamStatus::Ok);
            Assert::IsTrue(inventory.hasNamedStreams);
            Assert::IsTrue(inventory.streams.size() >= 2);
            Assert::IsTrue(DeleteFileW((std::wstring(fixture.path.data()) +
                L":probe").c_str()) != FALSE);
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));
        }

        TEST_METHOD(UncertainAndUndisplayableResultsRequireNotice)
        {
            StreamEnumeration result;
            result.status = StreamStatus::Ok;
            Assert::IsFalse(result.NeedsNotice());

            result.undisplayableNames = 1;
            result.hasNamedStreams = true;
            Assert::IsTrue(result.NeedsNotice());

            result.undisplayableNames = 0;
            result.status = StreamStatus::Truncated;
            Assert::IsTrue(result.NeedsNotice());

            result.status = StreamStatus::Error;
            Assert::IsTrue(result.NeedsNotice());
        }
    };

    TEST_CLASS(ExplorerPathPolicyTests)
    {
    public:
        TEST_METHOD(RejectsRemoteAndUnsupportedPathForms)
        {
            Assert::IsFalse(CanEnumerateExplorerPath(L"\\\\server\\share\\file.txt"));
            Assert::IsFalse(CanEnumerateExplorerPath(L"\\\\?\\UNC\\server\\share\\file.txt"));
            Assert::IsFalse(CanEnumerateExplorerPath(L"relative.txt"));
            Assert::IsFalse(CanEnumerateExplorerPath(L""));
        }

        TEST_METHOD(RejectsDirectoryJunctionAncestor)
        {
            TemporaryFile fixture;
            if (!CanEnumerateExplorerPath(fixture.path.data()))
            {
                Logger::WriteMessage(L"SKIP: temporary directory is ineligible for Explorer path probing.");
                return;
            }

            const std::wstring root = fixture.path.data();
            const std::wstring link = root + L".junction";
            const std::wstring target = root + L".target";
            Assert::IsTrue(CreateDirectoryW(target.c_str(), nullptr) != FALSE,
                L"Unable to create junction target");

            // Setup failures must not turn this regression into a false pass.
            // Junctions are directory reparse points, unlike plain filenames.
            const std::wstring command = L"cmd.exe /d /c mklink /J \"" +
                link + L"\" \"" + target + L"\" >nul 2>&1";
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');
            STARTUPINFOW startup = {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process = {};
            const BOOL started = CreateProcessW(nullptr, mutableCommand.data(),
                nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                &startup, &process);
            DWORD waitResult = WAIT_FAILED;
            DWORD exitCode = STILL_ACTIVE;
            if (started)
            {
                waitResult = WaitForSingleObject(process.hProcess, 10000);
                if (waitResult != WAIT_OBJECT_0)
                {
                    // Do not let a timed-out mklink race fixture cleanup.
                    TerminateProcess(process.hProcess, 1);
                    WaitForSingleObject(process.hProcess, INFINITE);
                }
                else
                {
                    GetExitCodeProcess(process.hProcess, &exitCode);
                }
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }

            const DWORD attributes = GetFileAttributesW(link.c_str());
            const bool isJunction = attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            bool rejectsChild = false;
            bool rejectsLeaf = false;
            if (isJunction)
            {
                rejectsChild = !CanEnumerateExplorerPath(link + L"\\child.txt");
                rejectsLeaf = !CanEnumerateExplorerPath(link);
            }
            // Perform cleanup before any assertion can abort this test.
            if (isJunction)
                RemoveDirectoryW(link.c_str());
            RemoveDirectoryW(target.c_str());

            Assert::IsTrue(started != FALSE, L"Unable to launch junction setup");
            Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), waitResult,
                L"Junction setup did not finish");
            Assert::AreEqual(static_cast<DWORD>(0), exitCode,
                L"mklink junction setup failed");
            Assert::IsTrue(isJunction, L"mklink did not create a reparse point");
            Assert::IsTrue(rejectsChild, L"Junction ancestor was accepted");
            Assert::IsTrue(rejectsLeaf, L"Junction leaf was accepted");
        }

        TEST_METHOD(AcceptsOrdinaryLocalFile)
        {
            TemporaryFile fixture;
            if (!CanEnumerateExplorerPath(fixture.path.data()))
                return; // TEMP may be on an ineligible drive or reparse-backed path.
            Assert::IsTrue(CanEnumerateExplorerPath(fixture.path.data()));
        }
    };

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

        TEST_METHOD(EnumerationReportsInvalidInput)
        {
            AlternateStreamReader reader;
            const auto invalidHandle = reader.EnumerateStreams(INVALID_HANDLE_VALUE);
            Assert::IsTrue(invalidHandle.status == StreamStatus::Error);
            Assert::AreEqual(static_cast<DWORD>(ERROR_INVALID_HANDLE), invalidHandle.error);
            Assert::IsFalse(invalidHandle.hasNamedStreams);
            Assert::IsTrue(invalidHandle.streams.empty());

            const auto invalidPath = reader.EnumerateStreams(static_cast<LPCWSTR>(nullptr));
            Assert::IsTrue(invalidPath.status == StreamStatus::Error);
            Assert::AreEqual(static_cast<DWORD>(ERROR_INVALID_PARAMETER), invalidPath.error);
        }

        TEST_METHOD(EnumerationHonorsVolumeStreamCapability)
        {
            TemporaryFile fixture;
            HandleW file(CreateFileW(fixture.path.data(), 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            Assert::IsTrue(file.get() != INVALID_HANDLE_VALUE);

            DWORD flags = 0;
            Assert::IsTrue(GetVolumeInformationByHandleW(file.get(), nullptr, 0,
                nullptr, nullptr, &flags, nullptr, 0) != FALSE);

            AlternateStreamReader reader;
            const auto result = reader.EnumerateStreams(file.get());
            if ((flags & FILE_NAMED_STREAMS) == 0)
            {
                // Run on FAT32/exFAT as well as NTFS: unsupported volumes
                // contain no ADS, not an incomplete or failed ADS inventory.
                Assert::IsTrue(result.status == StreamStatus::Ok);
                Assert::IsFalse(result.hasNamedStreams);
                Assert::IsTrue(result.streams.empty());
            }
            else
            {
                Assert::IsTrue(result.status == StreamStatus::Ok);
                Assert::IsFalse(result.hasNamedStreams);
            }
        }

        TEST_METHOD(UnsupportedStreamQueryRequiresConfirmedVolumeCapability)
        {
            // Inject native query errors and volume capabilities rather than
            // relying on whichever filesystem hosts the CI temporary file.
            const DWORD errors[] = {
                ERROR_INVALID_PARAMETER, ERROR_INVALID_FUNCTION, ERROR_NOT_SUPPORTED
            };
            for (const DWORD error : errors)
            {
                Assert::IsTrue(stream_info_detail::IsUnsupportedStreamQuery(
                    error, true, 0));
                Assert::IsFalse(stream_info_detail::IsUnsupportedStreamQuery(
                    error, true, FILE_NAMED_STREAMS));
                Assert::IsFalse(stream_info_detail::IsUnsupportedStreamQuery(
                    error, false, 0));
            }
            Assert::IsFalse(stream_info_detail::IsUnsupportedStreamQuery(
                ERROR_ACCESS_DENIED, true, 0));
            Assert::IsFalse(stream_info_detail::IsUnsupportedStreamQuery(
                ERROR_MORE_DATA, true, 0));
        }

        TEST_METHOD(EnumerationPreservesNamedStreamEvidence)
        {
            TemporaryFile fixture;
            AlternateStreamReader reader;
            const auto before = reader.EnumerateStreams(fixture.path.data());
            Assert::IsTrue(before.status == StreamStatus::Ok);
            Assert::IsFalse(before.hasNamedStreams);

            const std::wstring namedPath = std::wstring(fixture.path.data()) + L":status_test";
            CreateStream(namedPath);
            const auto after = reader.EnumerateStreams(fixture.path.data());
            Assert::IsTrue(after.status == StreamStatus::Ok);
            Assert::IsTrue(after.hasNamedStreams);
            Assert::IsTrue(std::find(after.streams.begin(), after.streams.end(),
                L":status_test:$DATA") != after.streams.end());
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

        TEST_METHOD(RejectsEmbeddedNulButAllowsUnpairedSurrogates)
        {
            std::vector<BYTE> buffer(64, 0);
            auto* entry = reinterpret_cast<FILE_STREAM_INFO*>(buffer.data());
            entry->NextEntryOffset = 0;
            entry->StreamNameLength = 4 * sizeof(WCHAR);
            entry->StreamName[0] = L':';
            entry->StreamName[1] = L'A';
            entry->StreamName[2] = L'\0';
            entry->StreamName[3] = L'B';

            stream_info_detail::Record record = {};
            Assert::IsFalse(stream_info_detail::ReadRecord(
                buffer.data(), buffer.size(), 0, record));

            // NTFS can return isolated UTF-16 surrogates; do not enforce
            // Unicode scalar-value validity on raw stream names.
            entry->StreamName[2] = static_cast<WCHAR>(0xD800);
            Assert::IsTrue(stream_info_detail::ReadRecord(
                buffer.data(), buffer.size(), 0, record));
            Assert::AreEqual(static_cast<size_t>(4 * sizeof(WCHAR)), record.nameBytes);
        }

        TEST_METHOD(ValidatesNamesBeforeUsingThemAsStreamPaths)
        {
            using stream_info_detail::IsNamedDataStreamName;
            auto valid = [](const std::wstring& name) {
                return IsNamedDataStreamName(name.data(), name.size());
            };

            Assert::IsTrue(valid(L":report:$DATA"));
            Assert::IsTrue(valid(L":a b:$DATA"));
            std::wstring surrogate = L":a:$DATA";
            surrogate[1] = static_cast<WCHAR>(0xD800);
            Assert::IsTrue(valid(surrogate)); // Preserve raw UTF-16 names.

            Assert::IsFalse(valid(L"::$DATA")); // The unnamed default stream.
            Assert::IsFalse(valid(L":report:$INDEX_ALLOCATION"));
            Assert::IsFalse(valid(L":report"));
            Assert::IsFalse(valid(L"report:$DATA"));
            Assert::IsFalse(valid(L":one:two:$DATA"));
            Assert::IsFalse(valid(L":../escape:$DATA"));
            Assert::IsFalse(valid(L":a\\b:$DATA"));
            Assert::IsFalse(valid(L":a/b:$DATA"));
            std::wstring malformed = L":ab:$DATA";
            malformed[2] = L'\0';
            Assert::IsFalse(valid(malformed));
            malformed[2] = static_cast<WCHAR>(1);
            Assert::IsFalse(valid(malformed));
            Assert::IsFalse(IsNamedDataStreamName(nullptr, 10));
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
            if (!stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 0, record) ||
                record.name == nullptr)
                Assert::Fail(L"First aligned stream record is invalid");
            Assert::AreEqual(static_cast<size_t>(32), record.nextOffset);
            Assert::AreEqual(L'A', record.name[0]);
            if (!stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 32, record) ||
                record.name == nullptr)
                Assert::Fail(L"Second aligned stream record is invalid");
            Assert::AreEqual(static_cast<size_t>(0), record.nextOffset);
            Assert::AreEqual(L'B', record.name[0]);
            Assert::IsFalse(stream_info_detail::ReadRecord(buffer.data(), buffer.size(), 48, record));
        }
    };

}
