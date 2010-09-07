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
#define MAX_CAMERAS  4

class TemplatePlugIn : 	public Gatan::PlugIn::PlugInMain
{
public:
	void SetNoDMSettling(long camera);
	int SetShutterNormallyClosed(long camera, long shutter);
	long GetDMVersion();
	int InsertCamera(long camera, BOOL state);
	int IsCameraInserted(long camera);
	int GetNumberOfCameras();
	int SelectCamera(long camera);
	double ExecuteClientScript(char *strScript, BOOL selectCamera);
	int AcquireAndTransferImage(void *array, int dataSize, long *arrSize, long *width,
		long *height, long divideBy2, long transpose);
	void AddCameraSelection(int camera = -1);
	int GetGainReference(float *array, long *arrSize, long *width, 
							long *height, long binning);
	int GetImage(short *array, long *arrSize, long *width, long *height, long processing,
    double exposure, long binning, long top, long left, long bottom, long right, 
    long shutter, double settling, long shutterDelay, long divideBy2, long corrections);
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
    for (int i = 0; i < MAX_CAMERAS; i++)
      m_iDMSettlingOK[i] = 1;
	}

	
private:
	BOOL m_bDebug;
	int m_iDMVersion;
	int m_iCurrentCamera;
  int m_iDMSettlingOK[MAX_CAMERAS];
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
	char last, retstr[128];
	DebugToResult(strScript, "DMCamera executing script :\n");
	if (m_bDebug) {

    // 11/15/06: switch to this from using strlen which falied with DMSDK 3.8.2
    for (int i = 0; strScript[i]; i++)
      last = strScript[i];
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
							long right, long shutter, double settling, long shutterDelay,
              long divideBy2, long corrections)
{
  //sprintf(m_strTemp, "Entering GetImage with divideBy2 %d\n", divideBy2);
  //DebugToResult(m_strTemp);
  // Workaround for Downing camera problem, inexplicable wild values coming through
  if (divideBy2 > 1 || divideBy2 < -1)
    divideBy2 = 0;
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

    // Specify corrections if incoming value is >= 0
    // As of DM 3.9.3 (3.9?) need to modify only the allowed coorections to avoid an
    // overscan image in simulator, so change 255 to 49
    if (corrections >= 0) {
      sprintf(m_strTemp, "CM_SetCorrections(acqParams, 49, %d)\n", corrections);
		  m_strCommand += m_strTemp;
    }
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

  // 5/23/06: drift settling in Record set from DM was being applied, so set settling
  // unconditionally as long as settling is OK (not in Faux camera)
	if (m_iDMVersion >= NEW_SETTLING_OK && m_iDMSettlingOK[m_iCurrentCamera]) {
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
    else {
			// Specify the other shutter as being closed first; 1 means closed
      sprintf(m_strTemp, "CM_SetCurrentShutterState(camera, %d, 1)\n", 
        shutter > 0 ? 1 : 0);
  		m_strCommand += m_strTemp;
      sprintf(m_strTemp, "CM_SetCurrentShutterState(camera, %d, 0)\n", 
        shutter > 0 ? 0 : 1);
  		m_strCommand += m_strTemp;
    }
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

  //sprintf(m_strTemp, "Calling AcquireAndTransferImage with divideBy2 %d\n", divideBy2);
  //DebugToResult(m_strTemp);
	int retval = AcquireAndTransferImage((void *)array, 2, arrSize, width, height,
    divideBy2, 0);	

	return retval;
}

int TemplatePlugIn::GetGainReference(float *array, long *arrSize, long *width, 
									long *height, long binning)
{
  // It seems that the gain reference is not flipped when images are
  long transpose = 0;
	if (m_iDMVersion >= NEW_CAMERA_MANAGER) {
    m_strCommand.resize(0);
	  AddCameraSelection();
    m_strCommand += "Number transpose = CM_Config_GetDefaultTranspose(camera)\n"
      "Exit(transpose)";
  	double retval = ExecuteScript((char *)m_strCommand.c_str());
	  if (retval != SCRIPT_ERROR_RETURN)
      transpose = (int)retval;
  }

  m_strCommand.resize(0);
	AddCameraSelection();
	sprintf(m_strTemp, "Image img := SSCGetGainReference(%d)\n"
		"KeepImage(img)\n"
		"number retval = GetImageID(img)\n"
		"Exit(retval)", binning);
	m_strCommand += m_strTemp;
	return AcquireAndTransferImage((void *)array, 4, arrSize, width, height, 0, transpose);
}

int TemplatePlugIn::AcquireAndTransferImage(void *array, int dataSize, long *arrSize, 
											long *width, long *height, long divideBy2, long transpose)
{
	long ID;
	DM::Image image;
	long outLimit = *arrSize;
  int byteSize, i, j;
  unsigned int *uiData;
  int *iData;
  unsigned short *usData;
  short *outData;
  short *sData;
  float *flIn, *flOut, flTmp;
  bool isInteger;

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
    isInteger = DM::ImageIsDataTypeInteger(image.get());
    if (byteSize != dataSize && !(dataSize == 2 && byteSize == 4 && 
      (isInteger || DM::ImageIsDataTypeFloat(image.get())))) {
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

      // Do a simple copy if sizes match and not dividing by 2 and not trasnposing
      if (dataSize == byteSize && !divideBy2) {
        if (!(transpose & 1)) {
          DebugToResult("Copying data\n");
          memcpy(array, imageL.get(), *width * *height * dataSize);
        } else {

          // Otherwise transpose floats around Y axis
          DebugToResult("Copying float data with transposition around Y\n");
          for (j = 0; j < *height; j++) {
            flIn = (float *)imageL.get() + j * *width;
            flOut = (float *)array + (j + 1) * *width - 1;
            for (i = 0; i < *width; i++)
              *flOut-- = *flIn++;
          }
        }

        // Do further transpositions in place for float data
        if (transpose & 2) {
          DebugToResult("Transposing float data around X in place\n");
          for (j = 0; j < *height / 2; j++) {
            flIn = (float *)array + j * *width;
            flOut = (float *)array + (*height - j - 1) * *width;
            for (i = 0; i < *width; i++) {
              flTmp = *flIn;
              *flIn++ = *flOut;
              *flOut++ = flTmp;
            }
          }
        }
        if ((transpose & 256) && *width == *height) {
          DebugToResult("Transposing float data around diagonal in place\n");
          for (j = 0; j < *height; j++) {
            flIn = (float *)array + j * *width + j;
            flOut = flIn;
            for (i = j; i < *width; i++) {
              flTmp = *flIn;
              *flIn++ = *flOut;
              *flOut = flTmp;
              flOut += *width;
            }
          }
        } 


      } else if (divideBy2) {

        // Divide by 2
        outData = (short *)array;
        if (DM::ImageIsDataTypeUnsignedInteger(image.get())) {
          if (byteSize == 2) {

            // unsigned short to short
            DebugToResult("Dividing unsigned shorts by 2\n");
            usData = (unsigned short *)imageL.get();
            for (i = 0; i < *width * *height; i++)
              outData[i] = (short)(usData[i] / 2);
          } else {

            // unsigned long to short
            DebugToResult("Dividing unsigned integers by 2\n");
            uiData = (unsigned int *)imageL.get();
            for (i = 0; i < *width * *height; i++)
              outData[i] = (short)(uiData[i] / 2);
          }
        } else if (isInteger) {
          if (byteSize == 2) {

            // signed short to short
            DebugToResult("Dividing signed shorts by 2\n");
            sData = (short *)imageL.get();
            for (i = 0; i < *width * *height; i++)
              outData[i] = sData[i] / 2;
          } else {

            // signed long to short
            DebugToResult("Dividing signed integers by 2\n");
            iData = (int *)imageL.get();
            for (i = 0; i < *width * *height; i++)
              outData[i] = (short)(iData[i] / 2);
          }
        } else {

          // Float to short
          DebugToResult("Dividing floats by 2\n");
          flIn = (float *)imageL.get();
          for (i = 0; i < *width * *height; i++)
            outData[i] = (short)(flIn[i] / 2);
        }

      } else {

        // No division by 2: Convert long integers to unsigned shorts
        usData = (unsigned short *)array;
        if (DM::ImageIsDataTypeUnsignedInteger(image.get())) {
        
          // If these are long integers and they are unsigned, just transfer
          DebugToResult("Converting unsigned integers to unsigned shorts\n");
          uiData = (unsigned int *)imageL.get();
          for (i = 0; i < *width * *height; i++)
            usData[i] = (unsigned short)uiData[i];
        } else if (isInteger) {

          // Otherwise need to truncate at zero to copy signed to unsigned
          DebugToResult("Converting signed integers to unsigned shorts with "
            "truncation\n");
          iData = (int *)imageL.get();
          for (i = 0; i < *width * *height; i++) {
            if (iData[i] >= 0)
              usData[i] = (unsigned short)iData[i];
            else
              usData[i] = 0;
          }
        } else {

          //Float to unsigned with truncation
          DebugToResult("Converting floats to unsigned shorts with truncation\n");
          flIn = (float *)imageL.get();
          for (i = 0; i < *width * *height; i++) {
            if (flIn[i] >= 0)
              usData[i] = (unsigned short)flIn[i];
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

// Set selected shutter normally closed - also set other shutter normally open
int TemplatePlugIn::SetShutterNormallyClosed(long camera, long shutter)
{
	if (m_iDMVersion < SET_IDLE_STATE_OK)
    return 0;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
						"Object cameraList = CM_GetCameras(manager)\n"
						"Object camera = ObjectAt(cameraList, %d)\n"
						"CM_SetIdleShutterState(camera, %d, 1)\n"
            "CM_SetIdleShutterState(camera, %d, 0)\n", camera, shutter, 1 - shutter);
	m_strCommand += m_strTemp;
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
	return 0;
}

void TemplatePlugIn::SetNoDMSettling(long camera)
{
  if (camera >= 0 && camera < MAX_CAMERAS)
    m_iDMSettlingOK[camera] = 0;
}


// Global instances of the plugin and the wrapper class for calling into this file
TemplatePlugIn gTemplatePlugIn;

PlugInWrapper gPlugInWrapper;

//////////////////////////////////////////////////////////////////////////
// THE WRAPPER FUNCTIONS
//
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
							long right, long shutter, double settling, long shutterDelay,
              long divideBy2, long corrections)
{
	return gTemplatePlugIn.GetImage(array, arrSize, width, height, processing, exposure,
		binning, top, left, bottom, right, shutter, settling, shutterDelay, divideBy2, 
    corrections);
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

void PlugInWrapper::SetNoDMSettling(long camera)
{
  gTemplatePlugIn.SetNoDMSettling(camera);
}

