#include "stdafx.h"

#define _GATANPLUGIN_WIN32_DONT_DEFINE_DLLMAIN

#define _GATANPLUGIN_USES_LIBRARY_VERSION 2
#include "DMPlugInBasic.h"

#define _GATANPLUGIN_USE_CLASS_PLUGINMAIN
#include "DMPlugInMain.h"

using namespace Gatan;

#include "TemplatePlugIn.h"
#include <string>
using namespace std ;

#define MAX_TEMP_STRING   1000

class TemplatePlugIn : 	public Gatan::PlugIn::PlugInMain
{
public:
	int SetShutterNormallyClosed(long camera, long shutter);
	long GetDMVersion();
	int InsertCamera(long camera, BOOL state);
	int IsCameraInserted(long camera);
	int GetNumberOfCameras();
	int SelectCamera(long camera);
	double ExecuteClientScript(char *strScript, BOOL selectCamera);
	int AcquireAndTransferImage(void *array, int dataSize, long *arrSize, long *width,
		long *height);
	void AddCameraSelection(int camera = -1);
	int GetGainReference(float *array, long *arrSize, long *width, 
							long *height, long binning);
	int GetImage(short *array, long *arrSize, long *width, 
		long *height, long processing, double exposure,
		long binning, long top, long left, long bottom, 
		long right, long shutter, double settling, long shutterDelay);
	void QueueScript(char *strScript);
	void SetCurrentCamera(int inVal) {m_iCurrentCamera = inVal;};
	void SetDMVersion(int inVal) {m_iDMVersion = inVal;};
	void SetDebugMode(BOOL inVal) {m_bDebug = inVal;};
	double ExecuteScript(char *strScript);
	void DebugToResult(char *strMessage, char *strPrefix = NULL);
	virtual void Start();
	virtual void Run();
	virtual void Cleanup();
	virtual void End();
	TemplatePlugIn()
	{
		m_bDebug = getenv("SERIALEMCCD_DEBUG") != NULL;
		m_iDMVersion = 340;
		m_iCurrentCamera = 0;
		m_strQueue.resize(0);
	}

	
private:
	BOOL m_bDebug;
	int m_iDMVersion;
	int m_iCurrentCamera;
	string m_strQueue;
	string m_strCommand;
	char m_strTemp[MAX_TEMP_STRING];
};

void TerminateModuleUninitializeCOM();
BOOL WasCOMInitialized();

///
/// This is called when the plugin is loaded.  Whenever DM is
/// launched, it calls 'Start' for each installed plug-in.
/// When it is called, there is no guarantee that any given
/// plugin has already been loaded, so the code should not
/// rely on scripts installed from other plugins.  The primary
/// use is to install script functions.
///
void TemplatePlugIn::Start()
{
}

///
/// This is called when the plugin is loaded, after the 'Start' method.
/// Whenever DM is launched, it calls the 'Run' method for
/// each installed plugin after the 'Start' method has been called
/// for all such plugins and all script packages have been installed.
/// Thus it is ok to use script functions provided by other plugins.
///
void TemplatePlugIn::Run()
{
	DM::Window results = DM::GetResultsWindow( true );
	//PlugIn::gResultOut << "Hello, world" << std::endl;
	if (WasCOMInitialized())
		DebugToResult("SerialEMCCD: COM was initialized through DllMain\n");
		//PlugIn::gResultOut << "COM was initialized through DllMain" << std::endl;
	else {
		m_bDebug = true;
		DebugToResult("DllMain was never called when SerialEMCCD was loaded - trouble!\n");
		//PlugIn::gResultOut << "DllMain was never called - trouble!" << std::endl;
	}
	if (GetDMVersion() < 0) {
		m_bDebug = true;
		DebugToResult("SerialEMCCD: Error getting Digital Micrograph version\n");
	}
}

///
/// This is called when the plugin is unloaded.  Whenever DM is
/// shut down, the 'Cleanup' method is called for all installed plugins
/// before script packages are uninstalled and before the 'End'
/// method is called for any plugin.  Thus, script functions provided
/// by other plugins are still available.  This method should release
/// resources allocated by 'Run'.
///
void TemplatePlugIn::Cleanup()
{
}

///
/// This is called when the plugin is unloaded.  Whenever DM is shut
/// down, the 'End' method is called for all installed plugins after
/// all script packages have been unloaded, and other installed plugins
/// may have already been completely unloaded, so the code should not
/// rely on scripts installed from other plugins.  This method should
/// release resources allocated by 'Start', and in particular should
/// uninstall all installed script functions.
///
void TemplatePlugIn::End()
{
	TerminateModuleUninitializeCOM();
}


void TemplatePlugIn::DebugToResult(char *strMessage, char *strPrefix)
{
	if (!m_bDebug) return;
	double time = (::GetTickCount() % (DWORD)3600000) / 1000.;
	char timestr[20];
	sprintf(timestr, "%.3f ", time);
	DM::OpenResultsWindow();
	DM::Result(timestr);
	if (strPrefix)
		DM::Result(strPrefix);
	else
		DM::Result("DMCamera Debug :\n");
	DM::Result(strMessage );
}


double TemplatePlugIn::ExecuteScript(char *strScript)
{
	double retval;
	char retstr[128];
	DebugToResult(strScript, "DMCamera executing script :\n");
	if (m_bDebug) {
		char last = strScript[strlen(strScript) - 1];
		if (last != '\n' && last != '\r')
			DM::Result("\n");
	}
	try {
		retval = DM::ExecuteScriptString(strScript);
	}
	catch (exception exc) {
		DebugToResult("Exception thrown while executing script\n");
		return SCRIPT_ERROR_RETURN;
		// Do we need to clean up error?  Do we want to catch this?
	}
	sprintf(retstr, "Return value = %g\n", retval);
	DebugToResult(retstr);
	return retval;
}

double TemplatePlugIn::ExecuteClientScript(char *strScript, BOOL selectCamera)
{
	m_strCommand.resize(0);
	if (selectCamera)
		AddCameraSelection();
	m_strCommand += strScript;
	char last = m_strCommand[m_strCommand.length() - 1];
	if (last != '\n' && last != '\r')
		m_strCommand += "\n";
	return ExecuteScript((char *)m_strCommand.c_str());
}

void TemplatePlugIn::QueueScript(char *strScript)
{
	m_strQueue += strScript;
	char last = m_strQueue[m_strQueue.length() - 1];
	if (last != '\n' && last != '\r')
		m_strQueue += "\n";
	DebugToResult("QueueScript called, queue is now:\n");
	if (m_bDebug)
		DM::Result((char *)m_strQueue.c_str());
}

// Common pathway for obtaining an acquired image or a dark reference
int TemplatePlugIn::GetImage(short *array, long *arrSize, long *width, 
							long *height, long processing, double exposure,
							long binning, long top, long left, long bottom, 
							long right, long shutter, double settling, long shutterDelay)
{
	m_strCommand.resize(0);
	AddCameraSelection();

	// Add and clear the queue
	if (!m_strQueue.empty()) {
		m_strCommand += m_strQueue;
		m_strQueue.resize(0);
	}

	// Set up acquisition parameters
	if (m_iDMVersion >= NEW_CAMERA_MANAGER) {
		
		// Convert the processing argument to the new format
		int newProc;
		switch (processing) {
		case DARK_REFERENCE: 
		case UNPROCESSED:
			newProc = NEWCM_UNPROCESSED;
			break;
		case DARK_SUBTRACTED:
			newProc = NEWCM_DARK_SUBTRACTED;
			break;
		case GAIN_NORMALIZED:
			newProc = NEWCM_GAIN_NORMALIZED;
			break;
		}
		sprintf(m_strTemp, "Object acqParams = CM_CreateAcquisitionParameters_FullCCD"
			"(camera, %d, %g, %d, %d)\n"
			"CM_SetBinnedReadArea(camera, acqParams, %d, %d, %d, %d)\n",
			newProc, exposure, binning, binning, top, left, bottom, right);
		m_strCommand += m_strTemp;

		// Turn off defect correction for raw images and dark references
		//if (newProc == NEWCM_UNPROCESSED)
		//	m_strCommand += "CM_SetCorrections(acqParams, 1, 0)\n";
	}

	// Old version, set the settling and the alternate shutter through notes
	if (m_iDMVersion < OLD_SETTLING_BROKEN) {
		sprintf(m_strTemp, 
			"SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", %g)\n",	settling);
		m_strCommand += m_strTemp;
	}

	if (m_iDMVersion >= NEW_SETTLING_OK && settling > 0.) {
		sprintf(m_strTemp, "CM_SetSettling(acqParams, %g)\n", settling);
		m_strCommand += m_strTemp;
	}
	
	if (m_iDMVersion < OLD_SELECT_SHUTTER_BROKEN && shutter >= 0) {
		sprintf(m_strTemp, "SetPersistentNumberNote"
			"(\"MSC:Parameters:2:Alternate Shutter\", %d)\n", shutter);
		m_strCommand += m_strTemp;
	}

	if (m_iDMVersion >= NEW_SELECT_SHUTTER_OK && shutter >= 0) {
		sprintf(m_strTemp, "CM_SetShutterIndex(acqParams, %d)\n", shutter);
		m_strCommand += m_strTemp;
	}

	// Open shutter if a delay is set
	if (shutterDelay) {
		if (m_iDMVersion < NEW_OPEN_SHUTTER_OK)
			m_strCommand += "SSCOpenShutter()\n";
		else
			// TODO: parameterize the shutter that needs to be opened?
			m_strCommand += "CM_SetCurrentShutterState(camera, 1, 0)\n";
		sprintf(m_strTemp, "Delay(%d)\n", shutterDelay);
		m_strCommand += m_strTemp;
		// Probably unneeded
		/*
		if (m_iDMVersion < NEW_OPEN_SHUTTER_OK)
			m_strCommand += "SSCCloseShutter()\n";
		else
			m_strCommand += "CM_SetCurrentShutterState(camera, 1, 1)\n";
		*/
	}

	// Get the image acquisition command
	if (m_iDMVersion < NEW_CAMERA_MANAGER) {
		switch (processing) {
		case UNPROCESSED:
			m_strCommand += "Image img := SSCUnprocessedBinnedAcquire";
			break;
		case DARK_SUBTRACTED:
			m_strCommand += "Image img := SSCDarkSubtractedBinnedAcquire";
			break;
		case GAIN_NORMALIZED:
			m_strCommand += "Image img := SSCGainNormalizedBinnedAcquire";
			break;
		case DARK_REFERENCE:
			m_strCommand += "Image img := SSCGetDarkReference";
			break;
		}

		sprintf(m_strTemp, "(%f, %d, %d, %d, %d, %d)\n", exposure, binning, top, left, 
			bottom, right);
		m_strCommand += m_strTemp;
	} else {
		if (processing == DARK_REFERENCE)
			// This command has an inverted sense, 1 means keep shutter closed
			// Need to use this to get defect correction
			m_strCommand += "CM_SetShutterExposure(acqParams, 1)\n"
					"Image img := CM_AcquireImage(camera, acqParams)\n";
//			"Image img := CM_CreateImageForAcquire(camera, acqParams, \"temp\")\n"
//						"CM_AcquireDarkReference(camera, acqParams, img, NULL)\n";
		else
			m_strCommand += "Image img := CM_AcquireImage(camera, acqParams)\n";
	}
	
	// Restore drift settling to zero if it was set
	if (m_iDMVersion < OLD_SETTLING_BROKEN && settling > 0.)
		m_strCommand += "SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", 0.)\n";
	
	// Final calls to retain image and return its ID
	sprintf(m_strTemp, "KeepImage(img)\n"
		"number retval = GetImageID(img)\n"
		"Exit(retval)");
	m_strCommand += m_strTemp;

	int retval = AcquireAndTransferImage((void *)array, 2, arrSize, width, height);	

	return retval;
}

int TemplatePlugIn::GetGainReference(float *array, long *arrSize, long *width, 
									long *height, long binning)
{
	m_strCommand.resize(0);
	AddCameraSelection();
	sprintf(m_strTemp, "Image img := SSCGetGainReference(%d)\n"
		"KeepImage(img)\n"
		"number retval = GetImageID(img)\n"
		"Exit(retval)", binning);
	m_strCommand += m_strTemp;
	return AcquireAndTransferImage((void *)array, 4, arrSize, width, height);	
}

int TemplatePlugIn::AcquireAndTransferImage(void *array, int dataSize, long *arrSize, 
											long *width, long *height)
{
	long ID;
	DM::Image image;
	long outLimit = *arrSize;
  int byteSize, i;
  unsigned int *uiData;
  int *iData;
  unsigned short *usData;

	// Set these values to zero in case of error returns
	*width = 0;
	*height = 0;
	*arrSize = 0;

	// Execute the command string as developed
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	
	// If error, zero out the return values and return error code
	if (retval == SCRIPT_ERROR_RETURN)
		return (int)retval;

	// Could there be exceptions? Play it safe
	try {
		// Get the image
		ID = (long)(retval + 0.01);
		if (!DM::GetImageFromID(image, ID)) {
			DebugToResult("Image not found from ID\n");
			return IMAGE_NOT_FOUND;
		}
		
		// Check the data type (may need to be fancier)
		byteSize = DM::ImageGetDataElementByteSize(image.get());
    if (byteSize != dataSize && !(dataSize == 2 && byteSize == 4 && 
      DM::ImageIsDataTypeInteger(image.get()))) {
			DebugToResult("Image data are not of the expected type\n");
			return WRONG_DATA_TYPE;
		}

		// Get the size and adjust if necessary to fit output array
		DM::GetSize( image.get(), width, height );
		if (*width * *height > outLimit) {
			DebugToResult("Warning: image is larger than the supplied array\n");
			*height = outLimit / *width;
		}
		
		// Get data pointer and transfer the data
		{
			GatanPlugIn::ImageDataLocker imageL( image );
      if (dataSize == byteSize) {
			  memcpy(array, imageL.get(), *width * *height * dataSize);
      } else {
        usData = (unsigned short *)array;
        if (DM::ImageIsDataTypeUnsignedInteger(image.get())) {
        
          // If these are long integers and they are unsigned, just transfer
          DebugToResult("Converting unsigned integers to unsigned shorts");
          uiData = (unsigned int *)imageL.get();
          for (i = 0; i < *width * *height; i++)
            usData[i] = (unsigned short)uiData[i];
        } else {

          // Otherwise need to truncate at zero to copy signed to unsigned
          DebugToResult("Converting signed integers to unsigned shorts with truncation");
          iData = (int *)imageL.get();
          for (i = 0; i < *width * *height; i++) {
            if (iData[i] >= 0)
              usData[i] = (unsigned short)iData[i];
            else
              usData[i] = 0;
          }
        }
      }
		}
		DM::DeleteImage(image.get());
	}
	catch (exception exc) {
		DebugToResult("Caught an exception from a call to DM:: function\n");
		return DM_CALL_EXCEPTION;
	}
	*arrSize = *width * *height;
	return 0;
}

// Add commands for selecting a camera to the command string
void TemplatePlugIn::AddCameraSelection(int camera)
{
	if (camera < 0)
		camera = m_iCurrentCamera;
	if (m_iDMVersion >= NEW_CAMERA_MANAGER)
		sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"Object camera = ObjectAt(cameraList, %d)\n"
						"CM_SelectCamera(manager, camera)\n", camera);
	else
		sprintf(m_strTemp, "MSCSelectCamera(%d)\n", camera);
	m_strCommand += m_strTemp;
}

// Make camera be the current camera and execute selection script
int TemplatePlugIn::SelectCamera(long camera)
{
	m_iCurrentCamera = camera;
	m_strCommand.resize(0);
	AddCameraSelection();
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
	return 0;
}

// Return number of cameras or -1 for error
int TemplatePlugIn::GetNumberOfCameras()
{
	m_strCommand.resize(0);
	if (m_iDMVersion < NEW_CAMERA_MANAGER)
		m_strCommand += "number num = MSCGetCameraCount()\n"
						"Exit(num)";
	else
		m_strCommand += "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"number listsize = SizeOfList(cameraList)\n"
						"Exit(listsize)";
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return -1;
	return (int)(retval + 0.1);
}

// Determine insertion state of given camera: return -1 for error, 0 if out, 1 if in
int TemplatePlugIn::IsCameraInserted(long camera)
{
	m_strCommand.resize(0);
	if (m_iDMVersion < NEW_CAMERA_MANAGER)
		sprintf(m_strTemp, "MSCSelectCamera(%d)\n"
						"number inserted = MSCIsCameraIn()\n"
						"Exit(inserted)", camera);
	else
		sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"Object camera = ObjectAt(cameraList, %d)\n"
						"number inserted = CM_GetCameraInserted(camera)\n"
						"Exit(inserted)", camera);
	m_strCommand += m_strTemp;
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return -1;
	return (int)(retval + 0.1);
}

int TemplatePlugIn::InsertCamera(long camera, BOOL state)
{
	m_strCommand.resize(0);
	if (m_iDMVersion < NEW_CAMERA_MANAGER)
		sprintf(m_strTemp, "MSCSelectCamera(%d)\n"
						"MSCSetCameraIn(%d)", camera, state ? 1 : 0);
	else
		sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"Object camera = ObjectAt(cameraList, %d)\n"
						"CM_SetCameraInserted(camera, %d)\n", camera, state ? 1 : 0);
	m_strCommand += m_strTemp;
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
	return 0;
}

// Get version from DM, return it and set internal version number
long TemplatePlugIn::GetDMVersion()
{
	unsigned int code;
	m_strCommand.resize(0);
	m_strCommand += "number version\n"
					"GetApplicationInfo(2, version)\n"
					"Exit(version)";
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return -1;
	code = (unsigned int)(retval + 0.1);
  // They don't support the last digit
//	m_iDMVersion = 1000 * (code >> 24) + 100 * ((code >> 16) & 0xff) + 
//		10 * ((code >> 8) & 0xff) + (code & 0xff);
	m_iDMVersion = 100 * (code >> 24) + 10 * ((code >> 16) & 0xff) + 
		((code >> 8) & 0xff);
  sprintf(m_strTemp, "retval = %g, code = %x, version = %d", retval, code, m_iDMVersion);
  DebugToResult(m_strTemp);
  return m_iDMVersion;
}

int TemplatePlugIn::SetShutterNormallyClosed(long camera, long shutter)
{
	if (m_iDMVersion < SET_IDLE_STATE_OK)
    return 0;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"Object camera = ObjectAt(cameraList, %d)\n"
						"CM_SetIdleShutterState(camera, %d, 1)\n", camera, shutter);
	m_strCommand += m_strTemp;
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
	return 0;
}


// Global instances of the plugin and the wrapper class for calling into this file
TemplatePlugIn gTemplatePlugIn;

PlugInWrapper gPlugInWrapper;

// THE WRAPPER FUNCTIONS
PlugInWrapper::PlugInWrapper()
{
}


BOOL PlugInWrapper::GetCameraBusy()
{
	return false;
}

double PlugInWrapper::ExecuteScript(char *strScript, BOOL selectCamera)
{
	return gTemplatePlugIn.ExecuteClientScript(strScript, selectCamera);
}

void PlugInWrapper::SetDebugMode(int inVal)
{
	gTemplatePlugIn.SetDebugMode(inVal != 0);
}

void PlugInWrapper::SetDMVersion(long inVal)
{
	gTemplatePlugIn.SetDMVersion((int)inVal);
}

void PlugInWrapper::SetCurrentCamera(long inVal)
{
	gTemplatePlugIn.SetCurrentCamera((int)inVal);
}

void PlugInWrapper::QueueScript(char *strScript)
{
	gTemplatePlugIn.QueueScript(strScript);
}

int PlugInWrapper::GetImage(short *array, long *arrSize, long *width, 
							long *height, long processing, double exposure,
							long binning, long top, long left, long bottom, 
							long right, long shutter, double settling, long shutterDelay)
{
	return gTemplatePlugIn.GetImage(array, arrSize, width, height, processing, exposure,
		binning, top, left, bottom, right, shutter, settling, shutterDelay);
}

int PlugInWrapper::GetGainReference(float *array, long *arrSize, long *width, 
									long *height, long binning)
{
	return gTemplatePlugIn.GetGainReference(array, arrSize, width, height, binning);
}


int PlugInWrapper::SelectCamera(long camera)
{
	return gTemplatePlugIn.SelectCamera(camera);
}

int PlugInWrapper::GetNumberOfCameras()
{
	return gTemplatePlugIn.GetNumberOfCameras();
}

int PlugInWrapper::IsCameraInserted(long camera)
{
	return gTemplatePlugIn.IsCameraInserted(camera);
}

int PlugInWrapper::InsertCamera(long camera, BOOL state)
{
	return gTemplatePlugIn.InsertCamera(camera, state);
}

long PlugInWrapper::GetDMVersion()
{
	return gTemplatePlugIn.GetDMVersion();
}

int PlugInWrapper::SetShutterNormallyClosed(long camera, long shutter)
{
  return gTemplatePlugIn.SetShutterNormallyClosed(camera, shutter);
}

