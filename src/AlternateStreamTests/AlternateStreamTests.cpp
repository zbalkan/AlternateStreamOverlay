#include "CppUnitTest.h"
#include "altstrm_common.h"
#include "stream_info_detail.h"
#include "../AlternateStreamOverlay/LocalPathPolicy.h"
#include "../AlternateStreamOverlay/resource.h"
#include <Prsht.h>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>
#include <utility>
#include <ShlObj.h>
#include <wrl/client.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    class TemporaryFileError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

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
                throw TemporaryFileError("Unable to create temporary file");
        }

        ~TemporaryFile()
        {
            DeleteFileW(path.data());
        }

        TemporaryFile(const TemporaryFile&) = delete;
        TemporaryFile& operator=(const TemporaryFile&) = delete;
    };

    // Each test owns a unique registry subtree, even when assertions throw.
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

    Microsoft::WRL::ComPtr<IClassFactory> GetClassFactory(
        HMODULE moduleHandle, REFCLSID clsid)
    {
        if (moduleHandle == nullptr)
            Assert::Fail(L"Unable to load overlay DLL");

        using GetClassObject = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
        const auto getClassObject = reinterpret_cast<GetClassObject>(
            GetProcAddress(moduleHandle, "DllGetClassObject"));
        if (getClassObject == nullptr)
            Assert::Fail(L"DllGetClassObject not exported");

        Microsoft::WRL::ComPtr<IClassFactory> factory;
        Assert::IsTrue(SUCCEEDED(getClassObject(clsid, __uuidof(IClassFactory),
            reinterpret_cast<void**>(factory.GetAddressOf()))));
        return factory;
    }

    Microsoft::WRL::ComPtr<IClassFactory> GetContextClassFactory(HMODULE moduleHandle)
    {
        // CLSID_AlternateStreamContext from AlternateStreamOverlay.idl.
        const CLSID clsid = { 0x4A26024B, 0x5499, 0x45BE,
            { 0xA2, 0x3F, 0x4A, 0x65, 0xE8, 0x88, 0x88, 0xD0 } };
        return GetClassFactory(moduleHandle, clsid);
    }

    Microsoft::WRL::ComPtr<IClassFactory> GetOverlayClassFactory(HMODULE moduleHandle)
    {
        // CLSID_AlternateStreamOverlayIcon from AlternateStreamOverlay.idl.
        const CLSID clsid = { 0xFBAB8C58, 0x59C9, 0x4522,
            { 0xB7, 0xE8, 0x1A, 0x6A, 0xA2, 0x3B, 0x31, 0x8C } };
        return GetClassFactory(moduleHandle, clsid);
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

    // Verify the retained property-page inventory without relying on IContextMenu.
    void AssertPropertyPageStreamCount(IShellPropSheetExt* extension, int expected)
    {
        HPROPSHEETPAGE page = nullptr;
        Assert::AreEqual(static_cast<HRESULT>(S_OK),
            extension->AddPages(CapturePropertyPage, reinterpret_cast<LPARAM>(&page)));
        Assert::IsTrue(page != nullptr);

        PROPSHEETHEADERW header = {};
        header.dwSize = sizeof(header);
        header.dwFlags = PSH_MODELESS | PSH_NOAPPLYNOW;
        header.pszCaption = L"Alternate stream test";
        header.nPages = 1;
        header.phpage = &page;

        const INT_PTR result = PropertySheetW(&header);
        if (result == 0 || result == -1)
        {
            DestroyPropertySheetPage(page);
            Assert::Fail(L"Unable to create the test property sheet");
        }

        const HWND sheet = reinterpret_cast<HWND>(result);
        const HWND current = reinterpret_cast<HWND>(
            SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0));
        const HWND combo = current == nullptr ? nullptr : GetDlgItem(current, IDC_COMBO1);
        const int count = combo == nullptr ? -1 :
            static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
        DestroyWindow(sheet);
        Assert::AreEqual(expected, count);
    }
}

namespace AlternateStreamTests
{

    TEST_CLASS(ComInitializationTests)
    {
    public:
        TEST_METHOD(OverlayInterfaceValidatesArguments)
        {
            ScopedModule module(GetOverlayDllPath().c_str());
            auto factory = GetOverlayClassFactory(module.handle);
            Microsoft::WRL::ComPtr<IShellIconOverlayIdentifier> overlay;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IShellIconOverlayIdentifier),
                reinterpret_cast<void**>(overlay.GetAddressOf()))));

            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(nullptr, 0));
            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(L"", 0));
            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(L"\\\\server\\share\\file.txt", 0));
            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(L"C:\\unused-file.txt", FILE_ATTRIBUTE_OFFLINE));

            int priority = -1;
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                overlay->GetPriority(&priority));
            Assert::AreEqual(10, priority);
            Assert::AreEqual(static_cast<HRESULT>(E_POINTER),
                overlay->GetPriority(nullptr));

            // The loaded DLL path can exceed MAX_PATH in CI checkouts.
            std::vector<WCHAR> iconFile(32768);
            int index = -1;
            DWORD flags = 0;
            Assert::AreEqual(static_cast<HRESULT>(E_POINTER),
                overlay->GetOverlayInfo(nullptr,
                    static_cast<int>(iconFile.size()), &index, &flags));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                overlay->GetOverlayInfo(iconFile.data(), 0, &index, &flags));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                overlay->GetOverlayInfo(iconFile.data(),
                    static_cast<int>(iconFile.size()), &index, &flags));
            Assert::IsTrue(iconFile[0] != L'\0');
            Assert::AreEqual(0, index);
            Assert::AreEqual(static_cast<DWORD>(ISIOI_ICONFILE | ISIOI_ICONINDEX), flags);
        }

        TEST_METHOD(OverlayInfoRejectsTruncatedIconPath)
        {
            ScopedModule module(GetOverlayDllPath().c_str());
            auto factory = GetOverlayClassFactory(module.handle);
            Microsoft::WRL::ComPtr<IShellIconOverlayIdentifier> overlay;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IShellIconOverlayIdentifier),
                reinterpret_cast<void**>(overlay.GetAddressOf()))));

            std::array<WCHAR, 1> iconFile = { L'X' };
            int index = -1;
            DWORD flags = 0xFFFFFFFF;
            Assert::AreEqual(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER),
                overlay->GetOverlayInfo(iconFile.data(),
                    static_cast<int>(iconFile.size()), &index, &flags));
            Assert::AreEqual(L'\0', iconFile.front());
            Assert::AreEqual(0, index);
            Assert::AreEqual(static_cast<DWORD>(0), flags);
        }

        TEST_METHOD(OverlayMembershipTracksNamedDataStream)
        {
            ScopedModule module(GetOverlayDllPath().c_str());
            auto factory = GetOverlayClassFactory(module.handle);
            Microsoft::WRL::ComPtr<IShellIconOverlayIdentifier> overlay;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IShellIconOverlayIdentifier),
                reinterpret_cast<void**>(overlay.GetAddressOf()))));

            TemporaryFile fixture;
            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(fixture.path.data(), FILE_ATTRIBUTE_NORMAL));

            CreateStream(std::wstring(fixture.path.data()) + L":overlay-test");
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                overlay->IsMemberOf(fixture.path.data(), FILE_ATTRIBUTE_NORMAL));

            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(fixture.path.data(), FILE_ATTRIBUTE_OFFLINE));
            Assert::IsTrue(DeleteFileW(
                (std::wstring(fixture.path.data()) + L":overlay-test").c_str()) != FALSE);
            Assert::AreEqual(static_cast<HRESULT>(S_FALSE),
                overlay->IsMemberOf(fixture.path.data(), FILE_ATTRIBUTE_NORMAL));
        }

        TEST_METHOD(ContextHandlerDoesNotExposeContextMenu)
        {
            ScopedModule module(GetOverlayDllPath().c_str());
            auto factory = GetContextClassFactory(module.handle);
            Microsoft::WRL::ComPtr<IContextMenu> menu;
            Assert::AreEqual(static_cast<HRESULT>(E_NOINTERFACE),
                factory->CreateInstance(nullptr, __uuidof(IContextMenu),
                    reinterpret_cast<void**>(menu.GetAddressOf())));
            Assert::IsTrue(menu.Get() == nullptr);

            Microsoft::WRL::ComPtr<IShellExtInit> initializer;
            Assert::IsTrue(SUCCEEDED(factory->CreateInstance(nullptr,
                __uuidof(IShellExtInit),
                reinterpret_cast<void**>(initializer.GetAddressOf()))));
            Microsoft::WRL::ComPtr<IShellPropSheetExt> propertyPages;
            Assert::IsTrue(SUCCEEDED(initializer.As(&propertyPages)));
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
            Microsoft::WRL::ComPtr<IShellPropSheetExt> propertyPages;
            Assert::IsTrue(SUCCEEDED(initializer.As(&propertyPages)));

            TemporaryFile fixture;
            CreateStream(std::wstring(fixture.path.data()) + L":lifecycle");
            Microsoft::WRL::ComPtr<IDataObject> data;
            data.Attach(new DropDataObject(fixture.path.data()));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            AssertPropertyPageStreamCount(propertyPages.Get(), 1);

            // Every failed reinitialization must discard the prior selection.
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, nullptr, nullptr));
            AssertPropertyPageStreamCount(propertyPages.Get(), 0);

            Microsoft::WRL::ComPtr<IDataObject> nullGlobal;
            nullGlobal.Attach(new DropDataObject(fixture.path.data(),
                DropDataObject::MediumCase::NullGlobal));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, nullGlobal.Get(), nullptr));
            AssertPropertyPageStreamCount(propertyPages.Get(), 0);

            Microsoft::WRL::ComPtr<IDataObject> wrongMedium;
            wrongMedium.Attach(new DropDataObject(fixture.path.data(),
                DropDataObject::MediumCase::WrongMedium));
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            Assert::AreEqual(static_cast<HRESULT>(E_INVALIDARG),
                initializer->Initialize(nullptr, wrongMedium.Get(), nullptr));
            AssertPropertyPageStreamCount(propertyPages.Get(), 0);

            // A successful reinitialization must restore the inventory.
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                initializer->Initialize(nullptr, data.Get(), nullptr));
            AssertPropertyPageStreamCount(propertyPages.Get(), 1);

            // Cover accepted and rejected property-page reference ownership.
            Assert::AreEqual(static_cast<HRESULT>(E_POINTER),
                propertyPages->AddPages(nullptr, 0));
            initializer.Reset();

            HPROPSHEETPAGE acceptedPage = nullptr;
            Assert::AreEqual(static_cast<HRESULT>(S_OK),
                propertyPages->AddPages(CapturePropertyPage,
                    reinterpret_cast<LPARAM>(&acceptedPage)));
            Assert::IsTrue(acceptedPage != nullptr);
            Assert::IsTrue(DestroyPropertySheetPage(acceptedPage) != FALSE);

            Assert::AreEqual(static_cast<HRESULT>(E_FAIL),
                propertyPages->AddPages(RejectPropertyPage, 0));

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

        TEST_METHOD(BoundedEnumerationIndicatesAlternateStreams)
        {
            StreamEnumeration result;
            result.status = StreamStatus::Ok;
            result.hasNamedStreams = true;
            Assert::IsTrue(IndicatesAlternateStreams(result));

            result.hasNamedStreams = false;
            Assert::IsFalse(IndicatesAlternateStreams(result));

            // The bounded native query exceeds 4 MiB. One unnamed stream
            // cannot account for that amount of FILE_STREAM_INFO data.
            result.status = StreamStatus::Truncated;
            Assert::IsTrue(IndicatesAlternateStreams(result));

            result.status = StreamStatus::Error;
            result.hasNamedStreams = true;
            Assert::IsFalse(IndicatesAlternateStreams(result));
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

    TEST_CLASS(PreviewNoticeTests)
    {
    public:
        TEST_METHOD(DistinguishesIncompleteInventoryFromUnsafeNames)
        {
            const auto clean = FormatPreviewDisplayText(false, false, L"Preview");
            Assert::AreEqual(L"Preview", clean.c_str());

            const auto unsafe = FormatPreviewDisplayText(false, true, L"Preview");
            Assert::IsTrue(unsafe.find(L"Some stream names cannot be previewed safely.") !=
                std::wstring::npos);
            Assert::IsTrue(unsafe.find(L"Stream enumeration is incomplete or failed.") ==
                std::wstring::npos);
            Assert::IsTrue(unsafe.find(L"\r\n\r\nPreview") != std::wstring::npos);

            const auto incomplete = FormatPreviewDisplayText(true, false, L"Preview");
            Assert::IsTrue(incomplete.find(L"Stream enumeration is incomplete or failed.") !=
                std::wstring::npos);
            Assert::IsTrue(incomplete.find(L"Some stream names cannot be previewed safely.") ==
                std::wstring::npos);

            const auto both = FormatPreviewDisplayText(true, true, L"Preview");
            Assert::IsTrue(both.find(L"Stream enumeration is incomplete or failed.") !=
                std::wstring::npos);
            Assert::IsTrue(both.find(L"Some stream names cannot be previewed safely.") !=
                std::wstring::npos);
            Assert::IsTrue(both.find(L"\r\n\r\nPreview") != std::wstring::npos);
        }

        TEST_METHOD(NoticeSurvivesLoadingFailureAndEmptyPreviews)
        {
            const auto empty = FormatPreviewDisplayText(false, false, L"");
            Assert::IsTrue(empty.empty());

            const auto warningOnly = FormatPreviewDisplayText(false, true, L"");
            Assert::AreEqual(L"Some stream names cannot be previewed safely.",
                warningOnly.c_str());

            const auto loading = FormatPreviewDisplayText(false, true,
                L"Loading alternate stream...");
            Assert::IsTrue(loading.find(L"Some stream names cannot be previewed safely.") !=
                std::wstring::npos);
            Assert::IsTrue(loading.find(L"Loading alternate stream...") != std::wstring::npos);

            const auto failure = FormatPreviewDisplayText(true, true,
                L"Unable to read alternate stream.");
            Assert::IsTrue(failure.find(L"Stream enumeration is incomplete or failed.") !=
                std::wstring::npos);
            Assert::IsTrue(failure.find(L"Some stream names cannot be previewed safely.") !=
                std::wstring::npos);
            Assert::IsTrue(failure.find(L"Unable to read alternate stream.") !=
                std::wstring::npos);
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
            Assert::IsFalse(reader.HasAlternateStreams(static_cast<LPCWSTR>(nullptr)));
            Assert::IsFalse(reader.HasAlternateStreams(L""));
            Assert::IsTrue(reader.EnumerateStreams(static_cast<LPCWSTR>(nullptr)).status == StreamStatus::Error);
            Assert::IsTrue(reader.EnumerateStreams(L"").status == StreamStatus::Error);
        }

        TEST_METHOD(EnumerationReportsInvalidInput)
        {
            AlternateStreamReader reader;
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
            const auto result = reader.EnumerateStreams(fixture.path.data());
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

        TEST_METHOD(NativeStreamNamesRoundTripThroughValidation)
        {
            TemporaryFile fixture;
            AlternateStreamReader reader;
            // Exercise actual filesystem names, not only synthetic FILE_STREAM_INFO.
            // Skip volumes without named-stream support.
            HandleW file(CreateFileW(fixture.path.data(), 0,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            Assert::IsTrue(file.get() != INVALID_HANDLE_VALUE);
            DWORD flags = 0;
            Assert::IsTrue(GetVolumeInformationByHandleW(file.get(), nullptr, 0,
                nullptr, nullptr, &flags, nullptr, 0) != FALSE);
            if ((flags & FILE_NAMED_STREAMS) == 0)
                return;

            const std::wstring streamName = L":g2-\u00E9\u6F22\u5B57";
            const auto streamPath = std::wstring(fixture.path.data()) + streamName;
            CreateStream(streamPath);

            const auto inventory = reader.EnumerateStreams(fixture.path.data());
            Assert::IsTrue(inventory.status == StreamStatus::Ok);
            const auto expected = streamName + L":$DATA";
            Assert::IsTrue(std::find(inventory.streams.begin(), inventory.streams.end(),
                expected) != inventory.streams.end());
            Assert::IsTrue(stream_info_detail::IsNamedDataStreamName(
                expected.data(), expected.size()));

            Assert::IsTrue(DeleteFileW(streamPath.c_str()) != FALSE);
        }

        TEST_METHOD(DistinguishesUnnamedFromNamedStreams)
        {
            TemporaryFile fixture;
            AlternateStreamReader reader;
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));

            const std::wstring namedPath = std::wstring(fixture.path.data()) + L":unit_test";
            CreateStream(namedPath);
            Assert::IsTrue(reader.HasAlternateStreams(fixture.path.data()));

            const auto inventory = reader.EnumerateStreams(fixture.path.data());
            Assert::IsTrue(inventory.status == StreamStatus::Ok);
            Assert::IsTrue(std::find(inventory.streams.begin(), inventory.streams.end(),
                L":unit_test:$DATA") != inventory.streams.end());

            Assert::IsTrue(DeleteFileW(namedPath.c_str()) != FALSE);
            Assert::IsFalse(reader.HasAlternateStreams(fixture.path.data()));
        }
    };

    TEST_CLASS(StreamPreviewTests)
    {
    public:
        TEST_METHOD(HexCheckboxAlwaysChangesPreviewFormat)
        {
            const BYTE malformed[] = { '[', 'Z', 'o', 'n', 'e', ']', 0xC0, 0xAF };
            const auto plain = FormatStreamPreview(
                malformed, sizeof(malformed), false);
            const auto hex = FormatStreamPreview(
                malformed, sizeof(malformed), true);
            Assert::IsTrue(plain.find(L"[Zone]") == 0);
            Assert::IsTrue(plain.find(L'\uFFFD') != std::wstring::npos);
            Assert::IsTrue(plain.find(L"00000000") == std::wstring::npos);
            Assert::IsTrue(hex.find(L"00000000") == 0);
            Assert::IsTrue(plain != hex);

            const BYTE nul[] = { 'A', 0, 'B' };
            const auto text = FormatStreamPreview(nul, sizeof(nul), false);
            Assert::AreEqual(static_cast<size_t>(3), text.size());
            Assert::AreEqual(L'A', text.front());
            Assert::AreEqual(L'\uFFFD', text[1]);
            Assert::AreEqual(L'B', text.back());
        }

        TEST_METHOD(IncompleteUtf8TailUsesReplacementCharacter)
        {
            const BYTE input[] = { 'A', 0xC0, 0xAF, 'B', 0xE2, 0x82 };
            const auto text = FormatStreamPreview(input, sizeof(input), false);
            Assert::IsTrue(text.find(L'\uFFFD') != std::wstring::npos);
            Assert::AreEqual(L'\uFFFD', text.back());
        }

        TEST_METHOD(ValidUtf8Preview)
        {
            const BYTE valid[] = { 'A', 0xE2, 0x82, 0xAC };
            const auto text = FormatStreamPreview(valid, sizeof(valid), false);
            Assert::AreEqual(L"A\u20AC", text.c_str());
        }

        TEST_METHOD(BoundedPreviewBoundaryShowsReplacementCharacter)
        {
            constexpr size_t PreviewLimit = 65535;
            std::vector<BYTE> data(PreviewLimit, 'A');
            const auto complete = FormatStreamPreview(data.data(), data.size(), false);
            Assert::AreEqual(PreviewLimit, complete.size());
            Assert::AreEqual(L'A', complete.back());
            data[PreviewLimit - 2] = 0xE2;
            data[PreviewLimit - 1] = 0x82;
            const auto text = FormatStreamPreview(data.data(), data.size(), false);
            Assert::AreEqual(PreviewLimit - 1, text.size());
            Assert::AreEqual(L'\uFFFD', text.back());

            const BYTE fourByteSplit[] = { 'A', 0xF0, 0x9F, 0x98 };
            const auto fourByteText = FormatStreamPreview(
                fourByteSplit, sizeof(fourByteSplit), false);
            Assert::AreEqual(L'\uFFFD', fourByteText.back());
        }

        TEST_METHOD(InvalidUtf8TailIsNotSilentlyDiscarded)
        {
            const BYTE invalidSecond[] = { 'A', 0xE0, 0x80 };
            const BYTE loneContinuation[] = { 'A', 0x80 };
            const auto malformed = FormatStreamPreview(
                invalidSecond, sizeof(invalidSecond), false);
            const auto orphan = FormatStreamPreview(
                loneContinuation, sizeof(loneContinuation), false);
            Assert::AreEqual(L'\uFFFD', malformed.back());
            Assert::AreEqual(L'\uFFFD', orphan.back());
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
