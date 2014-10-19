// DMCamera.cpp : Implementation of COM object methods that call in to TemplatePlugin

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

STDMETHODIMP CDMCamera::SetReadMode(long mode, double scaling)
{
	gPlugInWrapper.SetReadMode(mode, scaling);
	return S_OK;
}

STDMETHODIMP CDMCamera::SetK2Parameters(long readMode, double scaling, long hardwareProc,
                                        BOOL doseFrac, double frameTime, BOOL alignFrames,
                                        BOOL saveFrames, long filtSize, long filter[])
{
	gPlugInWrapper.SetK2Parameters(readMode, scaling, hardwareProc, doseFrac, frameTime,
    alignFrames, saveFrames, -1, 0, 0., 0., 0., 0., (char *)filter);
	return S_OK;
}

// This new version was added to set rotation/flip for dose fractionation shots that
// are NOT saving frames, so that DM can be kept from doing the operation for GMS >= 2.3.1
STDMETHODIMP CDMCamera::SetK2Parameters2(long readMode, double scaling, long hardwareProc,
                                         BOOL doseFrac, double frameTime, 
                                         BOOL alignFrames, BOOL saveFrames, 
                                         long rotationFlip, long flags, double dummy1, 
                                         double dummy2, double dummy3, double dummy4, 
                                         long filtSize, long filter[])
{
	gPlugInWrapper.SetK2Parameters(readMode, scaling, hardwareProc, doseFrac, frameTime,
    alignFrames, saveFrames, rotationFlip, flags, dummy1, dummy2, dummy3, dummy4, 
    (char *)filter);
	return S_OK;
}

STDMETHODIMP CDMCamera::SetupFileSaving(long rotationFlip, BOOL filePerImage, 
                                        double pixelSize, long nameSize, long names[],
                                        long *error)
{
  SetupFileSaving2(rotationFlip, filePerImage, pixelSize, 0, 0., 0., 0., 0., nameSize,
    names, error);
  return S_OK;
}

// Sets up file saving for the next acquisition.
//   rotationFlip is the operation (4 for flip around Y BEFORE plus CCW rotation / 90)
//      that needs to be applied to unrotated dose frac images (unless 
//      K2_SKIP_FRAME_ROTFLIP is set) and whose inverse needs to be applied to keep 
//      DM from doing rotation/flip for GMS >= 2.3.1
//   filePerImage is a flag for one file per image
//   pixelSize should be in Angstroms
//   flags are as defined in TemplatePlugin.h:
//      K2_SAVE_RAW_PACKED - save non-normalized frames as 4/8 bit for super-res/counting
//      K2_COPY_GAIN_REF   - Copy gain reference as needed if saving non-normalized frames
//      K2_RUN_COMMAND     - Not suppported yet
//      K2_SAVE_LZW_TIFF   - Save in TIFF with LZW compression
//      K2_SAVE_ZIP_TIFF   - Save in TIFF with ZIP compression
//      K2_SAVE_SYNCHRO    - Acquire stack synchronously with script calls
//      K2_SAVE_DEFECTS    - Save a defect list as needed
//      K2_EARLY_RETURN    - Return early, with no sum, or sum of subset of frames
//      K2_ASYNC_IN_RAM    - Acquire stack in DM asynchronously into RAM
//      K2_SKIP_FRAME_ROTFLIP - Save frames in native orientation, skipping rotation/flip
//   numGrabSum is relevant when doing an early return; it should be set from an unsigned
//      int with the number of frames to sum in the low 16 bits and, for GMS >= 2.3.1,
//      the number of frames to grab into a local stack in the high 16 bits.  The local
//      stack is needed because a single-shot cannot be done in GMS 2.3.1 until the stack
//      in DM has been fully accessed. 
//   nameSize should contain the number of longs passed in names
//   names should contain concatenated null-terminated strings as follows:
//      directory name
//      root name for files
//      full name of the gain reference, if K2_COPY_GAIN_REF is set in flags
//      full defect string, if K2_SAVE_DEFECTS is set
//      command to run, if K2_RUN_COMMAND is set
//   
// The caller is responsible for knowing how much memory is available and for choosing
// whether to set K2_ASYNC_IN_RAM (with or without an early return) and for setting the
// number of frames to grab with an early return (with or without a RAM stack in DM).  
// The RAM stack in DM is stored in native chip sizeX * sizeY * 2 bytes per frame 
// regardless of mode; the grabbed stack is stored in twice that space for super-res; 
// memory is freed from the RAM stack in DM as soon as the frame is accessed.  See
// SerialEM code for handling of this.
//
STDMETHODIMP CDMCamera::SetupFileSaving2(long rotationFlip, BOOL filePerImage, 
                                        double pixelSize, long flags, double numGrabSum,
                                        double dummy2, double dummy3, double dummy4,
                                        long nameSize, long names[], long *error)
{
  char *cnames = (char *)names;
  char *command = NULL;
  char *refName = NULL;
  char *defects = NULL;
  int rootind = (int)strlen(cnames) + 1;
  int nextInd = rootind;
  if (flags & K2_COPY_GAIN_REF) {
    nextInd += (int)strlen(&cnames[nextInd]) + 1;
    refName = &cnames[nextInd];
  }
  if (flags & K2_SAVE_DEFECTS) {
    nextInd += (int)strlen(&cnames[nextInd]) + 1;
    defects = &cnames[nextInd];
  }
  if (flags & K2_RUN_COMMAND) {
    nextInd += (int)strlen(&cnames[nextInd]) + 1;
    command = &cnames[nextInd];
  }
  gPlugInWrapper.SetupFileSaving(rotationFlip, filePerImage, pixelSize, flags, numGrabSum,
    dummy2, dummy3, dummy4, cnames, &cnames[rootind], refName, defects, command, error);
  return S_OK;
}

STDMETHODIMP CDMCamera::GetDefectList(short xyPairs[], long *arrSize, long *numPoints, 
                                      long *numTotal)
{
  if (gPlugInWrapper.GetDefectList(xyPairs, arrSize, numPoints, numTotal))
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::GetFileSaveResult(long *numSaved, long *error)
{
  gPlugInWrapper.GetFileSaveResult(numSaved, error);
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

STDMETHODIMP CDMCamera::GetDSProperties(long timeout, double addedFlyback, double margin,
                                        double *flyback, double *lineFreq, 
                                        double *rotOffset, long *doFlip)
{
	int retval = gPlugInWrapper.GetDSProperties(timeout, addedFlyback, margin, flyback, 
    lineFreq, rotOffset, doFlip);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::AcquireDSImage(short array[], long *arrSize, long *width, 
                                       long *height, double rotation, double pixelTime, 
                                       long lineSync, long continuous, long numChan, 
                                       long channels[], long divideBy2)
{
	int retval = gPlugInWrapper.AcquireDSImage(array, arrSize, width, height, rotation, 
    pixelTime, lineSync, continuous, numChan, channels, divideBy2);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::ReturnDSChannel(short array[], long *arrSize, long *width, 
                                        long *height, long channel, long divideBy2)
{
	int retval = gPlugInWrapper.ReturnDSChannel(array, arrSize, width, height, channel,
    divideBy2);
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::StopDSAcquisition()
{
  int retval = gPlugInWrapper.StopDSAcquisition();
	if (retval)
		return E_FAIL;
	return S_OK;
}

STDMETHODIMP CDMCamera::StopContinuousCamera()
{
  int retval = gPlugInWrapper.StopContinuousCamera();
	if (retval)
		return E_FAIL;
	return S_OK;
}
