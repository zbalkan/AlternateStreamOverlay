// dllmain.h : Declaration of module class.

class CAlternateStreamOverlayModule : public CAtlDllModuleT< CAlternateStreamOverlayModule >
{
public:
    DECLARE_LIBID(LIBID_AlternateStreamOverlayLib)
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_ALTERNATESTREAMOVERLAY, "{869100FF-B8AB-406F-A8E2-C8B561E7CC81}")
};

extern class CAlternateStreamOverlayModule _AtlModule;
