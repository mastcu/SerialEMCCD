// DMCamera.h: Definition of the CDMCamera class
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DMCAMERA_H__05E3210A_0D63_47FB_AA3E_CABD30B8E8A4__INCLUDED_)
#define AFX_DMCAMERA_H__05E3210A_0D63_47FB_AA3E_CABD30B8E8A4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CDMCamera

class CDMCamera : 
	public IDispatchImpl<IDMCamera, &IID_IDMCamera, &LIBID_SERIALEMCCDLib>, 
	public ISupportErrorInfo,
	public CComObjectRoot,
	public CComCoClass<CDMCamera,&CLSID_DMCamera>
{
public:
	CDMCamera() {}
BEGIN_COM_MAP(CDMCamera)
	COM_INTERFACE_ENTRY(IDispatch)
	COM_INTERFACE_ENTRY(IDMCamera)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
END_COM_MAP()
//DECLARE_NOT_AGGREGATABLE(CDMCamera) 
// Remove the comment from the line above if you don't want your object to 
// support aggregation. 

DECLARE_REGISTRY_RESOURCEID(IDR_DMCamera)
// ISupportsErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid);

// IDMCamera
public:
	STDMETHOD(InsertCamera)(/*[in]*/long camera, /*[in]*/BOOL state);
	STDMETHOD(IsCameraInserted)(/*[in]*/long camera, /*[out]*/BOOL *inserted);
	STDMETHOD(GetNumberOfCameras)(/*[out]*/long *numCameras);
	STDMETHOD(SelectCamera)(/*[in]*/long camera);
	STDMETHOD(GetGainReference)(/*[out, size_is(*arrSize)]*/float array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/long binning);
	STDMETHOD(GetAcquiredImage)(/*[out, size_is(arrSize)]*/short array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/long processing, /*[in]*/double exposure,
		/*[in]*/long binning, /*[in]*/long top, /*[in]*/long left, /*[in]*/long bottom, /*[in]*/long right,
		/*[in]*/long shutter, /*[in]*/double settling, /*[in]*/long shutterDelay);
	STDMETHOD(GetDarkReference)(/*[out, size_is(arrSize)]*/short array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/double exposure,
		/*[in]*/long binning, /*[in]*/long top, /*[in]*/long left, /*[in]*/long bottom, /*[in]*/long right,
		/*[in]*/long shutter, /*[in]*/double settling);
	STDMETHOD(QueueScript)(/*[in]*/long size, /*[in, size_is(size)]*/long script[]);
	STDMETHOD(SetCurrentCamera)(/*[in]*/long camera);
	STDMETHOD(SetDMVersion)(/*[in]*/long version);
	STDMETHOD(SetDebugMode)(/*[in]*/long debug);
	STDMETHOD(ExecuteScript)(/*[in]*/long size, /*[in, size_is(size)]*/long script[],
		/*[in]*/BOOL selectCamera, /*[out]*/double *retval);
	STDMETHOD(GetTestArray)(/*[in]*/long width, /*[in]*/long height, /*[out]*/long *retSize, /*[out, size_is(*retSize)]*/short array[], /*[out]*/long *time, /*[out]*/unsigned long *receiptTime, /*[out]*/unsigned long *returnTime);
};

#endif // !defined(AFX_DMCAMERA_H__05E3210A_0D63_47FB_AA3E_CABD30B8E8A4__INCLUDED_)
