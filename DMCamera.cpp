// DMCamera.cpp : Implementation of CSerialEMCCDApp and DLL registration.

#include "stdafx.h"
#include "SerialEMCCD.h"
#include "DMCamera.h"
#include "TemplatePlugIn.h"

extern PlugInWrapper gPlugInWrapper;

/////////////////////////////////////////////////////////////////////////////
//

STDMETHODIMP CDMCamera::InterfaceSupportsErrorInfo(REFIID riid)
{
	static const IID* arr[] = 
	{
		&IID_IDMCamera,
	};

	for (int i=0;i<sizeof(arr)/sizeof(arr[0]);i++)
	{
		if (InlineIsEqualGUID(*arr[i],riid))
			return S_OK;
	}
	return S_FALSE;
}


STDMETHODIMP CDMCamera::GetTestArray(long width, long height, long *retSize, short array[], 
									 long *time, unsigned long *receiptTime, unsigned long *returnTime)
{
	HRESULT retval = E_OUTOFMEMORY;  // SAY NOT OK
 	int iStart = 64;
	int i, j, iVal;
	int iDir = 1;
	int iBlockY = (height + 127)/ 128;
	int iBlockX = (width + 63) / 64;
	//short int *pdata;

	*receiptTime = ::GetTickCount();
	short int *cPixels = new short int[width * height];
	if (!cPixels)
		return retval;

	for (j = 0; j < height; j++)  {
		iVal = iStart;
		for (i = 0; i < width; i++) {
			cPixels[j * width + i] = 16*iVal;
			if (i % iBlockX == 0) 
				iVal += iDir; 
			if (iVal < -5)
				iVal = -5;
			if (iVal > 300)
				iVal = 300;
		}
		if (j % iBlockY == 0)
			iStart++;
		// iStart += j % 2;
		iDir = -iDir;
	}

	DWORD startTime = ::GetTickCount();
			// If that is OK, copy the data
			//for (i = 0; i < width * height; i++)
			//	*pdata++ = cPixels[i];
	//array =  new short int[width * height];  should not be needed!
	memcpy(array, cPixels, width * height * 2);
	*retSize = width * height;
	DWORD endTime = ::GetTickCount();
	*time = endTime - startTime;
	delete [] cPixels;
	*returnTime = ::GetTickCount();
	return S_OK;
}

STDMETHODIMP CDMCamera::ExecuteScript(long size, long script[], BOOL selectCamera,
									  double *retval)
{
	*retval = gPlugInWrapper.ExecuteScript((char *)script, selectCamera);
	if (*retval == SCRIPT_ERROR_RETURN)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::SetDebugMode(long debug)
{
	gPlugInWrapper.SetDebugMode(debug);
	return S_OK;
}

STDMETHODIMP CDMCamera::SetDMVersion(long version)
{
	gPlugInWrapper.SetDMVersion(version);
	return S_OK;
}

STDMETHODIMP CDMCamera::SetCurrentCamera(long camera)
{
	gPlugInWrapper.SetCurrentCamera(camera);
	return S_OK;
}

STDMETHODIMP CDMCamera::QueueScript(long size, long script[])
{
	gPlugInWrapper.QueueScript((char *)script);
	return S_OK;
}

STDMETHODIMP CDMCamera::GetAcquiredImage(short array[], long *arrSize, long *width, 
										 long *height, long processing, double exposure,
										 long binning, long top, long left, long bottom, 
										 long right, long shutter, double settling, 
										 long shutterDelay, long divideBy2, long corrections)
{
	int retval = gPlugInWrapper.GetImage(array, arrSize, width, height, processing,
		exposure, binning, top, left, bottom, right, shutter, settling, shutterDelay, divideBy2, corrections);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetDarkReference(short array[], long *arrSize, long *width, 
										 long *height, double exposure,
										 long binning, long top, long left, long bottom, 
										 long right, long shutter, double settling, long divideBy2, long corrections) 
{
	int retval = gPlugInWrapper.GetImage(array, arrSize, width, height, DARK_REFERENCE,
		exposure, binning, top, left, bottom, right, shutter, settling, 0, divideBy2, corrections);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetGainReference(float array[], long *arrSize, long *width, long *height, long binning)
{
	int retval = gPlugInWrapper.GetGainReference(array, arrSize, width, height, binning);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::SelectCamera(long camera)
{
	int retval = gPlugInWrapper.SelectCamera(camera);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetNumberOfCameras(long *numCameras)
{
	*numCameras = gPlugInWrapper.GetNumberOfCameras();
	if (*numCameras < 0)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::IsCameraInserted(long camera, BOOL *inserted)
{
	int retval = gPlugInWrapper.IsCameraInserted(camera);
	*inserted = retval > 0;
	if (retval < 0)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::InsertCamera(long camera, BOOL state)
{
	int retval = gPlugInWrapper.InsertCamera(camera, state);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetDMVersion(long *version)
{
	*version = gPlugInWrapper.GetDMVersion();
	if (*version < 0)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetDMCapabilities(BOOL *canSelectShutter, BOOL *canSetSettling,
											BOOL *openShutterWorks)
{
	long version = gPlugInWrapper.GetDMVersion();
	if (version < 0)
		return E_FAIL;
	*canSelectShutter = version < OLD_SELECT_SHUTTER_BROKEN || 
		version >= NEW_SELECT_SHUTTER_OK;
	*canSetSettling = version < OLD_SETTLING_BROKEN || version >= NEW_SETTLING_OK;
	*openShutterWorks = version < OLD_OPEN_SHUTTER_BROKEN || 
		version >= NEW_OPEN_SHUTTER_OK;
	return S_OK;
}

STDMETHODIMP CDMCamera::SetShutterNormallyClosed(long camera, long shutter)
{
  int retval = gPlugInWrapper.SetShutterNormallyClosed(camera, shutter);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::SetNoDMSettling(long camera)
{
	gPlugInWrapper.SetNoDMSettling(camera);
	return S_OK;
}
