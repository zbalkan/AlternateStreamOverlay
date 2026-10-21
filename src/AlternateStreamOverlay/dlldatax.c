// Wrapper for the merged MIDL proxy/stub implementation.
#include "targetver.h"

#ifdef _MERGE_PROXYSTUB // merge proxy stub DLL

#define REGISTER_PROXY_DLL //DllRegisterServer, etc.

#define USE_STUBLESS_PROXY	//defined only with MIDL switch /Oicf

#pragma comment(lib, "rpcns4.lib")
#pragma comment(lib, "rpcrt4.lib")

#define ENTRY_PREFIX	Prx

// The MIDL-generated sources use packing and C initialization conventions
// accepted by MSVC but diagnosed by clang-tidy's Clang frontend.
// Suppress only these generated-source diagnostics during Clang analysis.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpragma-pack"
#pragma clang diagnostic ignored "-Wmissing-braces"
#pragma clang diagnostic ignored "-Wextern-initializer"
#endif

#include "dlldata.c"
#include "AlternateStreamOverlay_p.c"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif //_MERGE_PROXYSTUB
