// stdafx.h : include file for standard system include files,
//      or project specific include files that are used frequently,
//      but are changed infrequently

#if !defined(AFX_STDAFX_H__B890E7EC_057F_4E79_881E_58135A873E70__INCLUDED_)
#define AFX_STDAFX_H__B890E7EC_057F_4E79_881E_58135A873E70__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define STRICT
#ifndef _WIN32_WINNT
#ifdef _WIN64
#define _WIN32_WINNT 0x0502
#else
#define _WIN32_WINNT 0x0500
#endif
#endif
#define _ATL_APARTMENT_THREADED
#define NOMINMAX

// Adding this makes all the GMS builds have manifests with 21022 and 30729 which fails to
// register in some sites.  The GMS 3 build still has both but maybe that will be OK
//#ifndef _BIND_TO_CURRENT_VCLIBS_VERSION								 // Force the CRT/MFC version to be put into the manifest
//#define _BIND_TO_CURRENT_VCLIBS_VERSION 1
//#endif

#include <atlbase.h>
//You may derive a class from CComModule and use it if you want to override
//something, but do not change the name of _Module
extern CComModule _Module;
#include <atlcom.h>

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__B890E7EC_057F_4E79_881E_58135A873E70__INCLUDED)
