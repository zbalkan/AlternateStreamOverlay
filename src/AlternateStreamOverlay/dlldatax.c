// Wrapper for the merged MIDL proxy/stub implementation.
#include "targetver.h"

#ifdef _MERGE_PROXYSTUB // merge proxy stub DLL

#define REGISTER_PROXY_DLL //DllRegisterServer, etc.

#define USE_STUBLESS_PROXY	//defined only with MIDL switch /Oicf

#pragma comment(lib, "rpcns4.lib")
#pragma comment(lib, "rpcrt4.lib")

#define ENTRY_PREFIX	Prx

// MIDL generates these files for MSVC. Clang-tidy's Clang frontend
// diagnoses their SDK-driven C initialization and packing conventions.
// Scope the exceptions to the generated inclusions, not handwritten code.
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
