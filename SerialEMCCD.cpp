// SerialEMCCD.cpp : Implementation of DLL Exports.


// Note: Proxy/Stub Information
//      To build a separate proxy/stub DLL, 
//      run nmake -f SerialEMCCDps.mk in the project directory.

#include "stdafx.h"
#include "resource.h"
#include <initguid.h>
#include "SerialEMCCD.h"

#include "SerialEMCCD_i.c"
#include "DMCamera.h"


CComModule _Module;

static BOOL initialized = false;
static BOOL registered = false;
void TerminateModuleUninitializeCOM();
BOOL WasCOMInitialized();

BEGIN_OBJECT_MAP(ObjectMap)
OBJECT_ENTRY(CLSID_DMCamera, CDMCamera)
END_OBJECT_MAP()

/////////////////////////////////////////////////////////////////////////////
// DLL Entry Point

extern "C"
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
	HRESULT hRes;
    if (dwReason == DLL_PROCESS_ATTACH)
    {
		hRes = CoInitialize(NULL);
        _ASSERTE(SUCCEEDED(hRes));
		if (FAILED(hRes))
			return false;
        _Module.Init(ObjectMap, hInstance, &LIBID_SERIALEMCCDLib);
		initialized = true;
        DisableThreadLibraryCalls(hInstance);
        hRes = _Module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, 
            REGCLS_MULTIPLEUSE);
        _ASSERTE(SUCCEEDED(hRes));
		if (FAILED(hRes))
			return false;
		registered = true;
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
		TerminateModuleUninitializeCOM();
	}
    return TRUE;    // ok
}

/////////////////////////////////////////////////////////////////////////////
// Used to determine whether the DLL can be unloaded by OLE

STDAPI DllCanUnloadNow(void)
{
    return (_Module.GetLockCount()==0) ? S_OK : S_FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// Returns a class factory to create an object of the requested type

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _Module.GetClassObject(rclsid, riid, ppv);
}

/////////////////////////////////////////////////////////////////////////////
// DllRegisterServer - Adds entries to the system registry

STDAPI DllRegisterServer(void)
{
    // registers object, typelib and all interfaces in typelib
    return _Module.RegisterServer(TRUE);
}

/////////////////////////////////////////////////////////////////////////////
// DllUnregisterServer - Removes entries from the system registry

STDAPI DllUnregisterServer(void)
{
    return _Module.UnregisterServer(TRUE);
}


void TerminateModuleUninitializeCOM() 
{
	if (registered)
		_Module.RevokeClassObjects();
	if (initialized) {
        _Module.Term();
		CoUninitialize();
	}
	registered = false;
	initialized = false;
}

BOOL WasCOMInitialized()
{
	return initialized;
}
