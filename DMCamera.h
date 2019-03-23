// DMCamera.h: Definition of the CDMCamera class
//
// See Copyright.txt for copyright and limitations
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
	STDMETHOD(SetNoDMSettling)(/*[in]*/long camera);
	STDMETHOD(SetShutterNormallyClosed)(/*[in]*/long camera, /*[in]*/long shutter);
	STDMETHOD(GetDMCapabilities)(/*[out]*/BOOL *canSelectShutter, /*[out]*/BOOL *canSetSettling, /*[out]*/BOOL *openShutterWorks);
  STDMETHOD(GetDMVersion)(/*[out]*/long *version);
  STDMETHOD(GetDMVersionAndBuild)(/*[out]*/long *version, /*[out]*/long *build);
  STDMETHOD(InsertCamera)(/*[in]*/long camera, /*[in]*/BOOL state);
	STDMETHOD(IsCameraInserted)(/*[in]*/long camera, /*[out]*/BOOL *inserted);
	STDMETHOD(GetNumberOfCameras)(/*[out]*/long *numCameras);
	STDMETHOD(SelectCamera)(/*[in]*/long camera);
	STDMETHOD(SetReadMode)(/*[in]*/long mode, /*[in]*/double scaling);
  STDMETHOD(SetK2Parameters)(/*[in]*/long mode, /*[in]*/double scaling, /*[in]*/long hardwareProc, /*[in]*/BOOL doseFrac, /*[in]*/double frameTime, /*[in]*/BOOL alignFrames, /*[in]*/BOOL saveFrames, /*[in]*/long filtSize, /*[in, size_is(filtSize)]*/long filter[]);
	STDMETHOD(GetGainReference)(/*[out, size_is(*arrSize)]*/float array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/long binning);
	STDMETHOD(GetAcquiredImage)(/*[out, size_is(*arrSize)]*/short array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/long processing, /*[in]*/double exposure,
		/*[in]*/long binning, /*[in]*/long top, /*[in]*/long left, /*[in]*/long bottom, /*[in]*/long right,
		/*[in]*/long shutter, /*[in]*/double settling, /*[in]*/long shutterDelay, /*[in]*/long divideBy2,
    /*[in]*/long corrections);
	STDMETHOD(GetDarkReference)(/*[out, size_is(*arrSize)]*/short array[], /*[in, out]*/long *arrSize, 
		/*[out]*/long *width, /*[out]*/long *height, /*[in]*/double exposure,
		/*[in]*/long binning, /*[in]*/long top, /*[in]*/long left, /*[in]*/long bottom, /*[in]*/long right,
		/*[in]*/long shutter, /*[in]*/double settling, /*[in]*/long divideBy2, /*[in]*/long corrections);
	STDMETHOD(QueueScript)(/*[in]*/long size, /*[in, size_is(size)]*/long script[]);
  STDMETHOD(SaveFrameMdoc)(/*[in]*/long size, /*[in, size_is(size)]*/long mdoc[], /*[in]*/long flags);
	STDMETHOD(SetCurrentCamera)(/*[in]*/long camera);
	STDMETHOD(SetDMVersion)(/*[in]*/long version);
	STDMETHOD(SetDebugMode)(/*[in]*/long debug);
	STDMETHOD(ExecuteScript)(/*[in]*/long size, /*[in, size_is(size)]*/long script[],
		/*[in]*/BOOL selectCamera, /*[out]*/double *retval);
	STDMETHOD(GetTestArray)(/*[in]*/long width, /*[in]*/long height, /*[out]*/long *retSize, /*[out, size_is(*retSize)]*/short array[], /*[out]*/long *time, /*[out]*/unsigned long *receiptTime, /*[out]*/unsigned long *returnTime);
  STDMETHOD(GetDSProperties)(/*[in]*/long timeout, /*[in]*/double addedFlyback, /*[in]*/double margin, /*[out]*/double *flyback, /*[out]*/double *lineFreq, /*[out]*/double *rotOffset, /*[out]*/long *doFlip);
  STDMETHOD(AcquireDSImage)(/*[out, size_is(*arrSize)]*/short array[], /*[in, out]*/long *arrSize, /*[in, out]*/long *width, /*[out]*/long *height, /*[in]*/double rotation, /*[in]*/double pixelTime, /*[in]*/long lineSync, /*[in]*/long continuous, /*[in]*/long numChan, /*[in, size_is(numChan)]*/long channels[], /*[in]*/long divideBy2);
  STDMETHOD(ReturnDSChannel)(/*[out, size_is(*arrSize)]*/short array[], /*[in, out]*/long *arrSize, /*[in, out]*/long *width, /*[out]*/long *height, /*[in]*/long channel, /*[in]*/long divideBy2);
  STDMETHOD(StopDSAcquisition)();
  STDMETHOD(SetupFileSaving)(/*[in]*/long rotationFlip, /*[in]*/BOOL filePerImage, /*[in]*/double pixelSize, /*[in]*/long nameSize, /*[in, size_is(nameSize)]*/long names[], /*[out]*/long *error);
  STDMETHOD(GetFileSaveResult)(/*[out]*/long *numSaved, /*[out]*/long *error);
  STDMETHOD(SetupFileSaving2)(/*[in]*/long rotationFlip, /*[in]*/BOOL filePerImage, /*[in]*/double pixelSize, /*[in]*/long flags, /*[in]*/double dummy1, /*[in]*/double dummy2, /*[in]*/double dummy3, /*[in]*/double dummy4, /*[in]*/long nameSize, /*[in, size_is(nameSize)]*/long names[], /*[out]*/long *error);
  STDMETHOD(GetDefectList)(/*[out, size_is(*arrSize)]*/short xyPairs[], /*[in, out]*/long *arrSize, /*[out]*/long *numPoints, /*[out]*/long *numTotal);
  STDMETHOD(SetK2Parameters2)(/*[in]*/long mode, /*[in]*/double scaling, /*[in]*/long hardwareProc, /*[in]*/BOOL doseFrac, /*[in]*/double frameTime, /*[in]*/BOOL alignFrames, /*[in]*/BOOL saveFrames, /*[in]*/long rotationFlip, /*[in]*/long flags, /*[in]*/double dummy1, /*[in]*/double dummy2, /*[in]*/double dummy3, /*[in]*/double dummy4, /*[in]*/long filtSize, /*[in, size_is(filtSize)]*/long filter[]);
  STDMETHOD(IsGpuAvailable)(/*[in]*/long gpuNum, /*[out]*/long *available, /*[out]*/double *gpuMemory);
  STDMETHOD(SetupFrameAligning)(/*[in]*/long aliBinning, /*[in]*/double rad2Filt1, /*[in]*/double rad2Filt2, /*[in]*/double rad2Filt3, /*[in]*/double sigma2Ratio, /*[in]*/double truncLimit, /*[in]*/long alignFlags, /*[in]*/long gpuFlags, /*[in]*/long numAllVsAll, /*[in]*/long groupSize, /*[in]*/long shiftLimit, /*[in]*/long antialiasType, /*[in]*/long refineIter, 
    /*[in]*/double stopIterBelow, /*[in]*/double refRad2, /*[in]*/long nSumAndGrab, /*[in]*/long frameStartEnd, /*[in]*/long dumInt2, /*[in]*/double dumDbl1, /*[in]*/long stringSize, /*[in, size_is(stringSize)]*/long strings[], /*[out]*/long *error);
  STDMETHOD(FrameAlignResults)(/*[out]*/double *rawDist, /*[out]*/double *smoothDist, /*[out]*/double *resMean, /*[out]*/double *maxResMax, /*[out]*/double *meanRawMax, /*[out]*/double *maxRawMax, /*[out]*/long *crossHalf, /*[out]*/long *crossQuarter, /*[out]*/long *crossEighth, /*[out]*/long *halfNyq, /*[out]*/long *dumInt1, /*[out]*/double *dumDbl1, /*[out]*/double *dumDbl2);
  STDMETHOD(MakeAlignComFile)(/*[in]*/long flags, /*[in]*/long dumInt1, /*[in]*/double dumDbl1, /*[in]*/double dumDbl2, /*[in]*/long stringSize, /*[in], size_is(stringSize)*/long strings[], /*[out]*/long *error);
  STDMETHOD(ReturnDeferredSum)(/*[out, size_is(*arrSize)]*/short array[], /*[in, out]*/long *arrSize, /*[in, out]*/long *width, /*[out]*/long *height);
  STDMETHOD(StopContinuousCamera)();
	STDMETHOD(GetPluginVersion)(/*[out]*/long *version);
	STDMETHOD(GetLastError)(/*[out]*/long *error);
	STDMETHOD(FreeK2GainReference)(/*[in]*/long which);
	STDMETHOD(WaitUntilReady)(/*[in]*/long which);
  STDMETHOD(GetLastDoseRate)(/*[out]*/double *doseRate);
};

#endif // !defined(AFX_DMCAMERA_H__05E3210A_0D63_47FB_AA3E_CABD30B8E8A4__INCLUDED_)
