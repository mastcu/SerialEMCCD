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

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include "Shared\mrcfiles.h"
#include "Shared\iimage.h"
#include "Shared\b3dutil.h"

#define MAX_TEMP_STRING   1000
#define MAX_FILTER_NAME   64
#define MAX_CAMERAS  10
#define MAX_DS_CHANNELS 8
#define ID_MULTIPLIER 10000000
#define SUPERRES_FRAME_SCALE 16
enum {CHAN_UNUSED = 0, CHAN_ACQUIRED, CHAN_RETURNED};
enum {NO_SAVE = 0, SAVE_FRAMES};
enum {NO_DEL_IM = 0, DEL_IMAGE};
static int sReadModes[3] = {K2_LINEAR_READ_MODE, K2_COUNTING_READ_MODE, 
K2_SUPERRES_READ_MODE};


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
	void SetReadMode(long mode, double scaling);
  void SetK2Parameters(long readMode, double scaling, long hardwareProc, BOOL doseFrac, 
    double frameTime, BOOL alignFrames, BOOL saveFrames, char *filter);
  void SetupFileSaving(long rotationFlip, BOOL filePerImage, double pixelSize, long flags,
    double dummy1, double dummy2, double dummy3, double dummy4, char *dirName, 
    char *rootName, char *refName, char *command, long *error);
  void GetFileSaveResult(long *numSaved, long *error);
  int CopyK2ReferenceIfNeeded();
	double ExecuteClientScript(char *strScript, BOOL selectCamera);
	int AcquireAndTransferImage(void *array, int dataSize, long *arrSize, long *width,
		long *height, long divideBy2, long transpose, long delImage, long saveFrames);
  void  ProcessImage(void *imageData, void *array, int dataSize, 
											            long width, long height, long divideBy2, 
                                  long transpose, int byteSize, bool isInteger,
                                  bool isUnsignedInt);
  void RotateFlip(short int *array, int mode, int nx, int ny, int operation, 
                    short int *brray, int *nxout, int *nyout);
  int CopyStringIfChanged(char *newStr, char **memberStr, int &changed, long *error);
	void AddCameraSelection(int camera = -1);
	int GetGainReference(float *array, long *arrSize, long *width, 
							long *height, long binning);
	int GetImage(short *array, long *arrSize, long *width, long *height, long processing,
    double exposure, long binning, long top, long left, long bottom, long right, 
    long shutter, double settling, long shutterDelay, long divideBy2, long corrections);
	void QueueScript(char *strScript);
	void SetCurrentCamera(int inVal) {m_iCurrentCamera = inVal;};
	void SetDMVersion(int inVal) {m_iDMVersion = inVal;};
  int GetDSProperties(long timeout, double addedFlyback, double margin, double *flyback, 
    double *lineFreq, double *rotOffset, long *doFlip);
  int AcquireDSImage(short array[], long *arrSize, long *width, 
    long *height, double rotation, double pixelTime, 
    long lineSync, long continuous, long numChan, long channels[], long divideBy2);
  int ReturnDSChannel(short array[], long *arrSize, long *width, 
    long *height, long channel, long divideBy2);
  int StopDSAcquisition();
	void SetDebugMode(BOOL inVal) {m_bDebug = inVal;};
	double ExecuteScript(char *strScript);
	void DebugToResult(const char *strMessage, const char *strPrefix = NULL);
	void ErrorToResult(const char *strMessage, const char *strPrefix = NULL);
	virtual void Start();
	virtual void Run();
	virtual void Cleanup();
	virtual void End();
	TemplatePlugIn();

  int m_iDebugVal;
	
private:
	BOOL m_bDebug;
  BOOL m_bGMS2;
	int m_iDMVersion;
	int m_iCurrentCamera;
  int m_iDMSettlingOK[MAX_CAMERAS];
	string m_strQueue;
	string m_strCommand;
	char m_strTemp[MAX_TEMP_STRING];
  int m_iDSAcquired[MAX_DS_CHANNELS];
  BOOL m_bContinuousDS;
  int m_iDSparamID;
  int m_iDSimageID;
  double m_dLastReturnTime;
  double m_dContExpTime;
  int m_iExtraDSdelay;
  double m_dFlyback;
  double m_dLineFreq;
  double m_dSyncMargin;
  int m_iReadMode;
  float m_fFloatScaling;
  BOOL m_bDoseFrac;
  double m_dFrameTime;
  BOOL m_bAlignFrames;
  char m_strFilterName[MAX_FILTER_NAME];
  BOOL m_bSaveFrames;
  int m_iHardwareProc;
  char *m_strRootName;
  char *m_strSaveDir;
  BOOL m_bFilePerImage;
  int m_iFramesSaved;
  int m_iErrorFromSave;
  double m_dPixelSize;
  int m_iRotationFlip;
  int m_iSaveFlags;
  BOOL m_bWriteTiff;
  int m_iTiffCompression;
  int m_iTiffQuality;
  char *m_strPostSaveCom;
  char *m_strGainRefToCopy;
  char *m_strLastRefName;
  char *m_strLastRefDir;
};

// Declarations of global functions called from here
void TerminateModuleUninitializeCOM();
BOOL WasCOMInitialized();
int GetSocketInitialization(int &wsaError);
int StartSocket(int &wsaError);
void ShutdownSocket(void);

TemplatePlugIn::TemplatePlugIn()
{
  m_bDebug = getenv("SERIALEMCCD_DEBUG") != NULL;
  m_iDebugVal = 0;
  if (m_bDebug)
    m_iDebugVal = atoi(getenv("SERIALEMCCD_DEBUG"));

  m_iDMVersion = 340;
  m_iCurrentCamera = 0;
  m_strQueue.resize(0);
  for (int i = 0; i < MAX_CAMERAS; i++)
    m_iDMSettlingOK[i] = 1;
  for (int j = 0; j < MAX_DS_CHANNELS; j++)
    m_iDSAcquired[j] = CHAN_UNUSED;
#ifdef GMS2
  m_bGMS2 = true;
#else
  m_bGMS2 = false;
#endif
  m_bContinuousDS = false;
  m_iDSparamID = 0;
  m_iDSimageID = 0;
  m_iExtraDSdelay = 10;
  m_dFlyback = 400.;
  m_dLineFreq = 60.;
  m_dSyncMargin = 10.;
  m_iReadMode = -1;
  m_fFloatScaling = 1.;
  m_iHardwareProc = 6;
  m_bDoseFrac = false;
  m_bSaveFrames = false;
  m_strRootName = NULL;
  m_strSaveDir = NULL;
  m_strPostSaveCom = NULL;
  m_strGainRefToCopy = NULL;
  m_strLastRefName = NULL;
  m_strLastRefDir = NULL;
  m_iSaveFlags = 0;
  m_bFilePerImage = true;
  m_bWriteTiff = false;
  m_iTiffCompression = 5;  // 5 is LZW, 8 is ZIP
  m_iTiffQuality = -1;
  m_iRotationFlip = 7;
  m_dPixelSize = 5.;
  const char *temp = getenv("SERIALEMCCD_ROOTNAME");
  if (temp)
    m_strRootName = _strdup(temp);
  temp = getenv("SERIALEMCCD_SAVEDIR");
  if (temp)
    m_strSaveDir = _strdup(temp);
}


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
int StartSocket(int &wsaError);
void TemplatePlugIn::Run()
{
  int socketRet, wsaError;
  char buff[80];
#ifndef GMS2
	DM::Window results = DM::GetResultsWindow( true );
#endif
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
  DebugToResult("Going to start socket\n");
  socketRet = StartSocket(wsaError);
  if (socketRet) {
    sprintf(buff, "Socket initialization failed, return value %d, WSA error %d\n", 
      socketRet, wsaError);
    ErrorToResult(buff, "SerialEMCCD: ");
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
  ShutdownSocket();
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


void TemplatePlugIn::DebugToResult(const char *strMessage, const char *strPrefix)
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

// Outputs messages to the results window upon error; just the message itself if in 
// debug mode, or a supplied or defulat prefix first if not in debug mode
void TemplatePlugIn::ErrorToResult(const char *strMessage, const char *strPrefix)
{
  if (m_bDebug) {
    DebugToResult(strMessage);
  } else {
  	DM::OpenResultsWindow();
	  if (strPrefix)
		  DM::Result(strPrefix);
    else
		  DM::Result("\nAn error occurred acquiring an image for SerialEM:\n");
    DM::Result(strMessage);
  }
}

/*
 * Executes a script, first printing it in the Results window if in debug mode
 */
double TemplatePlugIn::ExecuteScript(char *strScript)
{
	double retval;
	char last, retstr[128];
	DebugToResult(strScript, "DMCamera executing script :\n\n");
	if (m_bDebug) {

    // 11/15/06: switch to this from using strlen which falied with DMSDK 3.8.2
    for (int i = 0; strScript[i]; i++)
      last = strScript[i];
		if (last != '\n' && last != '\r')
			DM::Result("\n");
  	DM::Result("\n");
	}
	try {
		retval = DM::ExecuteScriptString(strScript);
	}
	catch (exception exc) {
		DebugToResult("Exception thrown while executing script");
    if (!m_bDebug) {
      DM::OpenResultsWindow();
      DM::Result("\nAn exception occurred executing this script for SerialEM:\n\n\n");
      DM::Result(strScript);
    }
    DM::Result("\n\n\nTo determine the cause, copy the above script with Ctrl-C,\n"
      "  open a script window with Ctrl-K, paste in there with Ctrl-V,\n"
      "  and execute the script with Ctrl-Enter\n");
		return SCRIPT_ERROR_RETURN;
	}
	sprintf(retstr, "Return value = %g\n", retval);
	DebugToResult(retstr);
	return retval;
}

// The external call to execute a script, optionally placing commands to select the
// camera first
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

// Add a command to a script to be executed in the future
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

/*
 * Common pathway for obtaining an acquired image or a dark reference
 */
int TemplatePlugIn::GetImage(short *array, long *arrSize, long *width, 
							long *height, long processing, double exposure,
							long binning, long top, long left, long bottom, 
							long right, long shutter, double settling, long shutterDelay,
              long divideBy2, long corrections)
{
  int saveFrames = NO_SAVE;

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

  // Commands for K2 camera
  if (m_iReadMode >= 0) {
    if (m_bDoseFrac) {
      sprintf(m_strTemp, "Object k2dfa = alloc(K2_DoseFracAcquisition)\n"
        "k2dfa.DoseFrac_SetHardwareProcessing(%d)\n"
        "k2dfa.DoseFrac_SetAlignOption(%d)\n"
        "k2dfa.DoseFrac_SetFrameExposure(%f)\n", m_iReadMode ? m_iHardwareProc : 0,
        m_bAlignFrames ? 1 : 0, m_dFrameTime);
      m_strCommand += m_strTemp;
      if (m_bAlignFrames) {
        sprintf(m_strTemp, "k2dfa.DoseFrac_SetFilter(\"%s\")\n", m_strFilterName);
        m_strCommand += m_strTemp;
      }
    }
    sprintf(m_strTemp, "CM_SetReadMode(acqParams, %d)\n"
      "K2_SetHardwareProcessing(camera, %d)\n"
      "Number wait_time_s\n"
      "CM_PrepareCameraForAcquire(manager, camera, acqParams, NULL, wait_time_s)\n"
      "Sleep(wait_time_s)\n", sReadModes[m_iReadMode], m_iReadMode ? m_iHardwareProc : 0);
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
    else if (m_iReadMode >= 0 && m_bDoseFrac)
      m_strCommand += "Image stack\n"
      "Image img := k2dfa.DoseFrac_AcquireImage(camera, acqParams, stack)\n";

		else
			m_strCommand += "Image img := CM_AcquireImage(camera, acqParams)\n";
	}
	
	// Restore drift settling to zero if it was set
	if (m_iDMVersion < OLD_SETTLING_BROKEN && settling > 0.)
		m_strCommand += "SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", 0.)\n";
	
	// Final calls to retain image and return its ID
	sprintf(m_strTemp, "KeepImage(img)\n"
		"number retval = GetImageID(img)\n");
	m_strCommand += m_strTemp;
  if (m_bSaveFrames && m_iReadMode >= 0 && m_bDoseFrac && m_strSaveDir && m_strRootName) {
    saveFrames = SAVE_FRAMES;
  	sprintf(m_strTemp, "KeepImage(stack)\n"
      "number stackID = GetImageID(stack)\n"
      //"Result(retval + \"  \" + stackID + \"\\n\")\n"
	  	"retval = retval + %d * stackID\n", ID_MULTIPLIER);
  	m_strCommand += m_strTemp;
  } else if (m_bSaveFrames) {
    sprintf(m_strTemp, "Save set but %d %d   %s   %s\n", m_iReadMode, m_bDoseFrac ? 1 : 0,
      m_strSaveDir ? m_strSaveDir : "NO DIR", m_strRootName ? m_strRootName : "NO ROOT");
    DebugToResult(m_strTemp);
  }
  sprintf(m_strTemp, "Exit(retval)");
	m_strCommand += m_strTemp;

  //sprintf(m_strTemp, "Calling AcquireAndTransferImage with divideBy2 %d\n", divideBy2);
  //DebugToResult(m_strTemp);
	int retval = AcquireAndTransferImage((void *)array, 2, arrSize, width, height,
    divideBy2, 0, DEL_IMAGE, saveFrames);	

	return retval;
}

/*
 * Call for returning a gain reference
 */
int TemplatePlugIn::GetGainReference(float *array, long *arrSize, long *width, 
									long *height, long binning)
{
  // It seems that the gain reference is not flipped when images are
  int retval, tmp;
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
	retval = AcquireAndTransferImage((void *)array, 4, arrSize, width, height, 0, transpose, 
    DEL_IMAGE, NO_SAVE);
  if (transpose & 256) {
    tmp = *width;
    *width = *height;
    *height = tmp;
  }
  return retval;
}

#define SET_ERROR(a) if (doingStack) \
        m_iErrorFromSave = a; \
      else \
        errorRet = a; \
      continue;

/*
 * Common routine for executing the current command script, getting an image ID back from
 * it, and copy the image into the supplied array with various transformations
 */
int TemplatePlugIn::AcquireAndTransferImage(void *array, int dataSize, long *arrSize, 
											long *width, long *height, long divideBy2, long transpose, 
                      long delImage, long saveFrames)
{
	long ID, imageID, stackID, frameDivide = divideBy2;
	DM::Image image;
	long outLimit = *arrSize;
  int byteSize, i, j, numDim, loop, outByteSize, numLoop = 1;
  unsigned short *usData;
  unsigned char *bData, *packed;
  unsigned char lowbyte;
  GatanPlugIn::ImageDataLocker *imageLp = NULL;
  ImageData::image_data_t fData;
  void *imageData;
  short *outData, *outForRot, *rotBuf = NULL;
  short *sData;
  bool isInteger, isUnsignedInt, doingStack, isFloat, signedBytes, save4bit, needProc;
  double retval, procWall, saveWall, wallStart, wallNow;
  FILE *fp = NULL;
  MrcHeader hdata;
  ImodImageFile *iifile = NULL;
  int fileSlice, tmin, tmax, tsum, val, numSlices, fileMode, nxout, nyout, nxFile;
  float tmean, meanSum, scaleSave = m_fFloatScaling, scaling = 1.;
  int errorRet = 0;

	// Set these values to zero in case of error returns
	*width = 0;
	*height = 0;
	*arrSize = 0;
  m_iFramesSaved = 0;
  m_iErrorFromSave = 0;

	// Execute the command string as developed
  if (m_strCommand.length() > 0)
  	retval = ExecuteScript((char *)m_strCommand.c_str());
  else
    retval = m_iDSimageID;
  m_dLastReturnTime = GetTickCount();
	
	// If error, zero out the return values and return error code
	if (retval == SCRIPT_ERROR_RETURN)
		return (int)retval;

  // Get the image ID(s)
  ID = imageID = (long)(retval + 0.01);
  if (saveFrames) {
    stackID = (int)((retval + 0.1) / ID_MULTIPLIER);
    imageID = B3DNINT(retval - (double)stackID * ID_MULTIPLIER);
    if (stackID) {
      numLoop = 2;
      ID = stackID;
    } else {
      saveFrames = 0;
      m_iErrorFromSave = NO_STACK_ID;
    }
    sprintf(m_strTemp, "Image ID %d  stack ID %d\n", imageID, stackID);
    DebugToResult(m_strTemp);
  }

  // Loop on stack then image if there are both
  for (loop = 0; loop < numLoop; loop++) {
    doingStack = saveFrames && !loop;
    delete imageLp;
    imageLp = NULL;
    if (loop)
      ID = imageID;
    try {

      if (!DM::GetImageFromID(image, ID)) {
        sprintf(m_strTemp, "Image not found from ID %d\n", ID);
        ErrorToResult(m_strTemp);
        SET_ERROR(IMAGE_NOT_FOUND);
      }

      // Check the data type (may need to be fancier)
      byteSize = DM::ImageGetDataElementByteSize(image.get());
      isInteger = DM::ImageIsDataTypeInteger(image.get());
      isUnsignedInt = DM::ImageIsDataTypeUnsignedInteger(image.get());
      isFloat = DM::ImageIsDataTypeFloat(image.get());
      signedBytes = byteSize == 1 && !isUnsignedInt;
      if (byteSize != dataSize && !((dataSize == 2 && byteSize == 4 && 
        (isInteger || isFloat)) ||
        (dataSize == 2 && byteSize == 1 && doingStack))) {
          sprintf(m_strTemp, "Image data are not of the expected type (bs %d  ds %d  int"
             " %d  uint %d)\n", byteSize, dataSize, isInteger ? 1:0, isUnsignedInt?1:0);
          ErrorToResult(m_strTemp);
          SET_ERROR(WRONG_DATA_TYPE);
      }

      // Get the size and adjust if necessary to fit output array
      DM::GetSize( image.get(), width, height );
      if (*width * *height > outLimit) {
        ErrorToResult("Warning: image is larger than the supplied array\n",
          "\nA problem occurred acquiring an image for SerialEM:\n");
        *height = outLimit / *width;
      }

      // Get data pointer and transfer the data
      imageLp = new GatanPlugIn::ImageDataLocker( image );
      if (doingStack) {
        numDim = DM::ImageGetNumDimensions(image.get());
        if (numDim < 3) {
          m_iErrorFromSave = STACK_NOT_3D;
          continue;
        }
        numSlices = DM::ImageGetDimensionSize(image.get(), 2);

        // Float super-res image is from software processing and needs special scaling
        // into bytes, set divide -1 as flag for this
        if (sReadModes[m_iReadMode] == K2_SUPERRES_READ_MODE && isFloat)
          frameDivide = -1;

        // But if they are counting mode images in integer, they need to not be scaled
        // or divided by 2 if placed into shorts, and they may be packed to bytes
        if (sReadModes[m_iReadMode] == K2_COUNTING_READ_MODE && isInteger) {
          m_fFloatScaling = 1.;
          frameDivide = 0;
          if(m_iSaveFlags & K2_SAVE_RAW_PACKED)
            frameDivide = -2;
        }
        outByteSize = 2;
        if (byteSize == 1 || frameDivide < 0) {
          fileMode = MRC_MODE_BYTE;
          outByteSize = 1;
        } else if ((frameDivide > 0 && byteSize != 2) || 
          (dataSize == byteSize && !isUnsignedInt))
          fileMode = MRC_MODE_SHORT;
        else
          fileMode = MRC_MODE_USHORT;
        save4bit = sReadModes[m_iReadMode] == K2_SUPERRES_READ_MODE && byteSize == 1 &&
            (m_iSaveFlags & K2_SAVE_RAW_PACKED);
        needProc = byteSize > 2 || signedBytes || frameDivide < 0;

        // Allocate buffer for rotation/flip if processing needs to be done too
        if (needProc && m_iRotationFlip) {
          try {
            if (outByteSize == 1)
              rotBuf = (short *)(new unsigned char [*width * *height]);
            else
              rotBuf = new short [*width * *height];
          }
          catch (...) {
            m_iErrorFromSave = ROTBUF_MEMORY_ERROR;
            rotBuf = NULL;
            continue;
          }
        }

        if (((sReadModes[m_iReadMode] == K2_SUPERRES_READ_MODE && byteSize == 1) ||
          (sReadModes[m_iReadMode] == K2_COUNTING_READ_MODE && !isFloat)) &&
          (m_iSaveFlags & K2_COPY_GAIN_REF) && m_strGainRefToCopy)
          CopyK2ReferenceIfNeeded();

        // Set up Tiff output file structure
        if (m_bWriteTiff) {
          iifile = iiNew();
          iifile->nz = m_bFilePerImage ? 1 : numSlices;
          iifile->format = IIFORMAT_LUMINANCE;
          iifile->file = IIFILE_TIFF;
          iifile->type = IITYPE_UBYTE;
          if (fileMode == MRC_MODE_SHORT)
            iifile->type = IITYPE_SHORT;
          if (fileMode == MRC_MODE_USHORT)
            iifile->type = IITYPE_USHORT;
        }

        procWall = saveWall = 0.;
        wallStart = wallTime();
        for (int slice = 0; slice < numSlices; slice++) {
          imageLp->GetImageData(2, slice, fData);
          imageData = fData.get_data();
          outData = (short *)imageData;
          outForRot = (short *)array;

          // Process a float image (from software normalized frames)
          // It goes from the DM array into the passed array
          if (needProc) {
            ProcessImage(imageData, array, dataSize, *width, *height, frameDivide,
              transpose, byteSize, isInteger, isUnsignedInt);
            outData = (short *)array;
            outForRot = rotBuf;
            if (byteSize > 2) {
              scaling = m_fFloatScaling;
              if (frameDivide > 0)
                scaling /= 2.;
              else if (frameDivide == -1)
                scaling = SUPERRES_FRAME_SCALE;
            }
          }

          // Rotate and flip if desired and change the array pointer to save to use the 
          // passed array or the rotation array
          if (m_iRotationFlip) {
            RotateFlip(outData, fileMode, *width, *height, m_iRotationFlip, outForRot,
              &nxout, &nyout);
            outData = outForRot;
          } else {
            nxout = *width;
            nyout = *height;
          }
          wallNow = wallTime();
          procWall += wallNow - wallStart;
          wallStart = wallNow;
          nxFile = save4bit ? nxout / 2 : nxout;

          // open file if needed
          if (!slice || m_bFilePerImage) {
            if (m_bFilePerImage)
              sprintf(m_strTemp, "%s\\%s_%03d.%s", m_strSaveDir, m_strRootName, slice +1,
              m_bWriteTiff ? "tif" : "mrc");
            else
              sprintf(m_strTemp, "%s\\%s.%s", m_strSaveDir, m_strRootName, 
              m_bWriteTiff ? "tif" : "mrc");
            if (m_bWriteTiff) {
              iifile->nx = nxFile;
              iifile->ny = nyout;
              iifile->filename = _strdup(m_strTemp);
              i = tiffOpenNew(iifile);
            } else {
              fp = fopen(m_strTemp, "wb");
            }
            if ((m_bWriteTiff && i) || (!m_bWriteTiff && !fp)) {
              j = errno;
              i = (int)strlen(m_strTemp);
              m_strTemp[i] = '\n';
              m_strTemp[i+1] = 0x00;
              ErrorToResult(m_strTemp);
              sprintf(m_strTemp, "Failed to open above file: %s\n", strerror(j));
              ErrorToResult(m_strTemp);
              m_iErrorFromSave = FILE_OPEN_ERROR;
              break;
            }

            // Set up header for one slice
            if (!m_bWriteTiff) {
              mrc_head_new(&hdata, nxFile, nyout, 1, fileMode);
              sprintf(m_strTemp, "SerialEMCCD: Dose fractionation image, scaled by %.2f", 
                scaling);
              if (save4bit)
                sprintf(m_strTemp, "SerialEMCCD: Dose fractionation image, 4 bits packed"
                ); 
              mrc_head_label(&hdata, m_strTemp);
            }
            fileSlice = 0;
            tmin = 1000000;
            tmax = -tmin;
            meanSum = 0.;
          }

          // Loop on the lines to compute mean accurately
          tmean = 0.; 
          for (i = 0; i < nyout; i++) {

            // Get pointer to start of line
            if (outByteSize == 1) {
              bData = ((unsigned char *)outData) + i * nxout;
              sData = (short *)bData;
            } else
              sData = &outData[i * nxout];
            usData = (unsigned short *)sData;

            // Get min/max/sum and add to mean
            tsum = 0;
            if (fileMode == MRC_MODE_USHORT) {
              for (j = 0; j < nxout; j++) {
                val = usData[j];
                tmin = B3DMIN(tmin, val);
                tmax = B3DMAX(tmax, val);
                tsum += val;
              }
            } else if (fileMode == MRC_MODE_SHORT) {
              for (j = 0; j < nxout; j++) {
                val = sData[j];
                tmin = B3DMIN(tmin, val);
                tmax = B3DMAX(tmax, val);
                tsum += val;
              }
            } else {
              for (j = 0; j < nxout; j++) {
                val = bData[j];
                tmin = B3DMIN(tmin, val);
                tmax = B3DMAX(tmax, val);
                tsum += val;
              }
 
            }
            tmean += tsum;
          }

          // Pack 4-bit data into bytes; move into array if not there yet
          if (save4bit) {
            bData = (unsigned char *)outData;
            packed = (unsigned char *)array;
            outData = (short *)array;
            for (i = 0; i < nxFile * nyout; i++) {
              lowbyte = *bData++ & 15;
              *packed++ = lowbyte | ((*bData++ & 15) << 4);
            }
          }

          if (m_bWriteTiff) {
            iifile->amin = (float)tmin;
            iifile->amax = (float)tmax;
            iifile->amean = tmean / (float)(nxout * nyout);
            i = tiffWriteSection(iifile, outData, m_iTiffCompression, 0, 
              B3DNINT(2.54e8 / m_dPixelSize), m_iTiffQuality);
            if (i) {
              m_iErrorFromSave = WRITE_DATA_ERROR;
              sprintf(m_strTemp, "Error (%d) writing section %d to TIFF file\n", i, 
                slice);
              ErrorToResult(m_strTemp);
              break;
            }
          } else {

            // Seek to slice then write it
            i = mrc_big_seek(fp, hdata.headerSize, nxFile * outByteSize, 
              nyout * fileSlice, SEEK_SET);
            if (i) {
              sprintf(m_strTemp, "Error %d seeking for slice %d: %s\n", i, slice, 
                strerror(errno));
              ErrorToResult(m_strTemp);
              m_iErrorFromSave = SEEK_ERROR;
              break;
            }

            if ((i = (int)b3dFwrite(outData, outByteSize * nxFile, nyout, fp)) != nyout) {
              sprintf(m_strTemp, "Failed to write data past line %d of slice %d: %s\n", i, 
                slice, strerror(errno));
              ErrorToResult(m_strTemp);
              m_iErrorFromSave = WRITE_DATA_ERROR;
              break;
            }

            // This can't have any effect; all errors broke the loop above
            if (m_iErrorFromSave)
              continue;

            // Completely update and write the header regardless of whether one file/slice
            fileSlice++;
            hdata.nz = hdata.mz = fileSlice;
            hdata.amin = (float)tmin;
            hdata.amax = (float)tmax;
            meanSum += tmean / B3DMAX(1.f, (float)(nxout * nyout));
            hdata.amean = meanSum / hdata.nz;
            mrc_set_scale(&hdata, m_dPixelSize, m_dPixelSize, m_dPixelSize);
            if (mrc_head_write(fp, &hdata)) {
              ErrorToResult("Failed to write header\n");
              m_iErrorFromSave = HEADER_ERROR;
              break;
            }
          }

          // Finally, increment successful save count and close file if needed
          m_iFramesSaved++;
          if (m_bFilePerImage || slice == numSlices - 1) {
            if (m_bWriteTiff) {
              iiClose(iifile);
              free(iifile->filename);
              iifile->filename = NULL;
            } else {
              fclose(fp);
              fp = NULL;
            }
          }
          wallNow = wallTime();
          saveWall += wallNow - wallStart;
          wallStart = wallNow;
        }
        m_fFloatScaling = scaleSave;

        if (m_iErrorFromSave)
          continue;
        sprintf(m_strTemp, "Processing time %.3f   saving time %.3f sec\n", procWall,
          saveWall);
        DebugToResult(m_strTemp);
      } else {

        // Regular old image
        numDim = DM::ImageGetNumDimensions(image.get());
        if (numDim != 2) {
          sprintf(m_strTemp, "image with ID %d has %d dimensions!\n", ID, numDim);
          DebugToResult(m_strTemp);
        }
        imageData = imageLp->get();
        ProcessImage(imageData, array, dataSize, *width, *height, divideBy2, transpose, 
          byteSize, isInteger, isUnsignedInt);
      }

    }
    catch (exception exc) {
      ErrorToResult("Caught an exception from a call to a DM:: function\n");
      SET_ERROR(DM_CALL_EXCEPTION);
    }
  }
  delete imageLp;

  // Clean up files now in case an exception got us to here
  if (iifile)
    iiDelete(iifile);
  iifile = NULL;
  if (fp)
    fclose(fp);
  fp = NULL;
  delete [] rotBuf;

  // Delete image(s) before return if they can be found
  if (delImage) {
    if (DM::GetImageFromID(image, imageID))
      DM::DeleteImage(image.get());
    else 
      DebugToResult("Cannot find image for deleting it\n");
    if (saveFrames && DM::GetImageFromID(image, stackID))
      DM::DeleteImage(image.get());
    else if (saveFrames)
      DebugToResult("Cannot find stack for deleting it\n");

  }
  *arrSize = *width * *height;

	return errorRet;
}

/*
* ProcessImage copies data from DM to given array with conversion, truncation, scaling
* and division by 2 as needed.  There are way too many cases handled here, thanks to the
* K2 camera.  Here is list of the kind of data that it produces in various cases from
* actual camera (values in parentheses are from the simulator)
*                Linear              Counting           Super-res
* Unproc single  us int              us int             us short (int)
* Unproc df sum  us int              us int             us int
* Unproc frames  us short            us short           byte
* DS single      sign int            sign int           sign short (int)
* DS df sum      sign int            sign int           sign int
* DS frames      sign int (short)    sign int (short)   sign byte
* GN single/sum  float               float              float
* GN frames      float               float              float
*/
void  TemplatePlugIn::ProcessImage(void *imageData, void *array, int dataSize, 
											            long width, long height, long divideBy2, 
                                  long transpose, int byteSize, bool isInteger,
                                  bool isUnsignedInt)
{
  int i, j;
  unsigned int *uiData;
  int *iData;
  unsigned short *usData, *usData2;
  short *outData;
  short *sData;
  short sVal;
  unsigned char *bData;
  char *sbData;
  char sbVal;
  float *flIn, *flOut, flTmp;
  int operations[4] = {1, 5, 7, 3};

  // Do a simple copy if sizes match and not dividing by 2 and not transposing
  // or if bytes are passed in
  if ((dataSize == byteSize && !divideBy2 && m_fFloatScaling == 1.) || byteSize == 1 ||
    divideBy2 == -2) {
    
    // If they are signed bytes, need to copy with truncation
    if (byteSize == 1 && !isUnsignedInt) {
      sbData = (char *)imageData;
      bData = (unsigned char *)array;
      DebugToResult("Converting signed bytes with truncation at 0\n");
      for (i = 0; i < width * height; i++) {
        sbVal = *sbData++;
        *bData++ = (unsigned char)(sbVal < 0 ? 0 : sbVal);
      }

    // Packing unsigned or signed shorts from raw counting image to bytes
    } else if (byteSize == 2 && divideBy2 == -2 && isUnsignedInt) {
      usData = (unsigned short *)imageData;
      bData = (unsigned char *)array;
      DebugToResult("Truncating unsigned shorts into bytes\n");
      for (i = 0; i < width * height; i++)
        *bData++ = (unsigned char)*usData++;

    } else if (byteSize == 2 && divideBy2 == -2 && !isUnsignedInt) {
      sData = (short *)imageData;
      bData = (unsigned char *)array;
      DebugToResult("Truncating signed shorts into bytes\n");
      for (i = 0; i < width * height; i++) {
        sVal = *sData++;
        B3DCLAMP(sVal, 0, 255);
        *bData++ = (unsigned char)sVal;
      }

    // Packing signed ints from dark-subtracted counting image to bytes
    } else if (byteSize == 4 && divideBy2 == -2 && isInteger) {
      iData = (int *)imageData;
      bData = (unsigned char *)array;
      DebugToResult("Truncating signed integers into bytes\n");
      for (i = 0; i < width * height; i++) {
        j = *iData++;
        B3DCLAMP(j, 0, 255);
        *bData++ = (unsigned char)j;
      }

    // If a transpose changes the size, need to call the fancy routine
    } else if ((transpose & 256) && width != height) {

      // This routine builds in a final flip around X "for output" so the operations
      // are derived to specify such flipped output to give the desired one
      RotateFlip((short *)imageData, MRC_MODE_FLOAT, width, height, 
        operations[transpose & 3], (short *)array, &i, &j);
    } else {

      // Just copy data to array to start or end with if no transpose around Y
      if (!(transpose & 1)) {
        DebugToResult("Copying data\n");
        memcpy(array, imageData, width * height * byteSize);
      } else {

        // Otherwise transpose floats around Y axis
        DebugToResult("Copying float data with transposition around Y\n");
        for (j = 0; j < height; j++) {
          flIn = (float *)imageData + j * width;
          flOut = (float *)array + (j + 1) * width - 1;
          for (i = 0; i < width; i++)
            *flOut-- = *flIn++;
        }
      }

      // Do further transpositions in place for float data
      if (transpose & 2) {
        DebugToResult("Transposing float data around X in place\n");
        for (j = 0; j < height / 2; j++) {
          flIn = (float *)array + j * width;
          flOut = (float *)array + (height - j - 1) * width;
          for (i = 0; i < width; i++) {
            flTmp = *flIn;
            *flIn++ = *flOut;
            *flOut++ = flTmp;
          }
        }
      }
      if (transpose & 256) {
        DebugToResult("Transposing float data around diagonal in place\n");
        for (j = 0; j < height; j++) {
          flIn = (float *)array + j * width + j;
          flOut = flIn;
          for (i = j; i < width; i++) {
            flTmp = *flIn;
            *flIn++ = *flOut;
            *flOut = flTmp;
            flOut += width;
          }
        }
      } 
    }


  } else if (divideBy2 < 0) {

    // Scale super-res normalized floats into byte image with scaling by 16 because the
    // hardware limits the raw frames to 15 counts
      DebugToResult("Scaling super-res floats into bytes\n");
      flIn = (float *)imageData;
      bData = (unsigned char *)array;
      for (i = 0; i < width * height; i++)
        bData[i] = (unsigned char)(flIn[i] * SUPERRES_FRAME_SCALE + 0.5f);

  } else if (divideBy2 > 0) {

    // Divide by 2
    outData = (short *)array;
    if (isUnsignedInt) {
      if (byteSize == 1) {

        // unsigned byte to short with scaling  THIS WON'T HAPPEN WITH CURRENT LOGIC
        DebugToResult("Dividing unsigned bytes by 2 with scaling\n");
        bData = (unsigned char *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)((float)bData[i] * m_fFloatScaling / 2.f + 0.5f);
      } else if (byteSize == 2 && m_fFloatScaling == 1.) {

        // unsigned short to short
        DebugToResult("Dividing unsigned shorts by 2\n");
        usData = (unsigned short *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)(usData[i] / 2);
      } else if (byteSize == 2) {

        // unsigned short to short with scaling
        DebugToResult("Dividing unsigned shorts by 2 with scaling\n");
        usData = (unsigned short *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)((float)usData[i] * m_fFloatScaling / 2.f + 0.5f);
      } else if (m_fFloatScaling == 1.) {

        // unsigned long to short
        DebugToResult("Dividing unsigned integers by 2\n");
        uiData = (unsigned int *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)(uiData[i] / 2);
      } else {

        // unsigned long to short with scaling
        DebugToResult("Dividing unsigned integers by 2 with scaling\n");
        uiData = (unsigned int *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)((float)uiData[i] * m_fFloatScaling / 2.f + 0.5f);
      }
    } else if (isInteger) {
      if (byteSize == 2 && m_fFloatScaling == 1.) {

        // signed short to short
        DebugToResult("Dividing signed shorts by 2\n");
        sData = (short *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = sData[i] / 2;
      } else if (byteSize == 2) {

        // signed short to short with scaling
        DebugToResult("Dividing signed shorts by 2 with scaling\n");
        sData = (short *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)((float)sData[i] * m_fFloatScaling / 2.f + 0.5f);
      } else if (m_fFloatScaling == 1.) {

        // signed long to short
        DebugToResult("Dividing signed integers by 2\n");
        iData = (int *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)(iData[i] / 2);
      } else {

        // signed long to short with scaling
        DebugToResult("Dividing signed integers by 2 with scaling\n");
        iData = (int *)imageData;
        for (i = 0; i < width * height; i++)
          outData[i] = (short)((float)iData[i] * m_fFloatScaling / 2.f + 0.5f);
      }
    } else {

      // Float to short
      DebugToResult("Dividing floats by 2 with scaling\n");
      flIn = (float *)imageData;
      for (i = 0; i < width * height; i++)
        outData[i] = (short)(flIn[i] * m_fFloatScaling / 2.f + 0.5f);
    }

  } else {

    // No division by 2: Convert long integers to unsigned shorts
    usData = (unsigned short *)array;
    if (isUnsignedInt) {
      if (byteSize == 1) {

        // unsigned byte to ushort with scaling  THIS WON'T HAPPEN WITH CURRENT LOGIC
        DebugToResult("Converting unsigned bytes to unsigned shorts with scaling\n");
        bData = (unsigned char *)imageData;
        for (i = 0; i < width * height; i++)
          usData[i] = (unsigned short)((float)bData[i] * m_fFloatScaling + 0.5f);
      } else if (byteSize == 2) {

        DebugToResult("Scaling unsigned shorts\n");
        usData2 = (unsigned short *)imageData;
        for (i = 0; i < width * height; i++)
          usData[i] = (unsigned short)((float)usData2[i] * m_fFloatScaling + 0.5f);

        // Unsigned short to ushort with scaling

      } else if (m_fFloatScaling == 1.) {

        // If these are long integers and they are unsigned, just transfer
        DebugToResult("Converting unsigned integers to unsigned shorts\n");
        uiData = (unsigned int *)imageData;
        for (i = 0; i < width * height; i++)
          usData[i] = (unsigned short)uiData[i];
      } else {

        // Or scale them
        DebugToResult("Converting unsigned integers to unsigned shorts with scaling\n");
        uiData = (unsigned int *)imageData;
        for (i = 0; i < width * height; i++)
          usData[i] = (unsigned short)((float)uiData[i] * m_fFloatScaling + 0.5f);
      }
    } else if (isInteger) {

      if (byteSize == 2) {

        // Scaling signed shorts: convert to unsigned and truncate at 0
        DebugToResult("Converting signed shorts to unsigned shorts with scaling "
          "and truncation\n");
        sData = (short *)imageData;
        for (i = 0; i < width * height; i++) {
          if (sData[i] >= 0)
            usData[i] = (unsigned short)((float)sData[i] * m_fFloatScaling + 0.5f);
          else
            usData[i] = 0;
        }
      } else if (m_fFloatScaling == 1.) {

        // Otherwise there are ints: need to truncate at zero to copy signed to unsigned
        DebugToResult("Converting signed integers to unsigned shorts with "
          "truncation\n");
        iData = (int *)imageData;
        for (i = 0; i < width * height; i++) {
          if (iData[i] >= 0)
            usData[i] = (unsigned short)iData[i];
          else
            usData[i] = 0;
        }
      } else {

        // Scaling, convert to usigned and truncate at 0
        DebugToResult("Converting signed integers to unsigned shorts with scaling "
          "and truncation\n");
        iData = (int *)imageData;
        for (i = 0; i < width * height; i++) {
          if (iData[i] >= 0)
            usData[i] = (unsigned short)((float)iData[i] * m_fFloatScaling + 0.5f);
          else
            usData[i] = 0;
        }
      }
    } else {

      //Float to unsigned with truncation
      DebugToResult("Converting floats to unsigned shorts with truncation and "
        "scaling\n");
      flIn = (float *)imageData;
      for (i = 0; i < width * height; i++) {
        if (flIn[i] >= 0)
          usData[i] = (unsigned short)(flIn[i] * m_fFloatScaling + 0.5f);
        else
          usData[i] = 0;
      }
    }
  }
}

// Perform any combination of rotation and Y flipping for a short array: 
// operation = 0-3 for rotation by 90 * operation, plus 4 for flipping around Y axis 
// before rotation or 8 for flipping around Y after.  THIS IS COPY OF ProcRotateFlip
// with the type and invertCon arguments removed and invert contrast code also
void TemplatePlugIn::RotateFlip(short int *array, int mode, int nx, int ny, int operation, 
                                short int *brray, int *nxout, int *nyout)
{
  int xalong[4] = {1, 0, -1, 0};
  int yalong[4] = {0, -1, 0, 1};
  int xinter[4] = {0, 1, 0, -1};
  int yinter[4] = {1, 0, -1, 0};
  int mapping[8] = {6, 5, 4, 7, 2, 1, 0, 3};
  int xstart, ystart, dinter, dalong, ix, iy, strip, numStrips, rotation;
  short int *bline, *blineStart, *alineStart;
  short int *aline1, *aline2, *aline3, *aline4, *aline5, *aline6, *aline7, *aline8;
  short int *bline1, *bline2, *bline3, *bline4, *bline5, *bline6, *bline7, *bline8;

  unsigned char *bubln, *bublnStart, *aublnStart;
  unsigned char *aubln1, *aubln2, *aubln3, *aubln4, *aubln5, *aubln6, *aubln7, *aubln8;
  unsigned char *bubln1, *bubln2, *bubln3, *bubln4, *bubln5, *bubln6, *bubln7, *bubln8;
  unsigned char *ubarray = (unsigned char *)array;
  unsigned char *ubbrray = (unsigned char *)brray;

  float *bfln, *bflnStart, *aflnStart;
  float *afln1, *afln2, *afln3, *afln4, *afln5, *afln6, *afln7, *afln8;
  float *bfln1, *bfln2, *bfln3, *bfln4, *bfln5, *bfln6, *bfln7, *bfln8;
  float *farray = (float *)array;
  float *fbrray = (float *)brray;

  // Map the operation to produce a final flipping around X axis for output
  if (operation < 8)
    operation = mapping[operation];
  rotation = operation % 4;

  // Flip X coordinates for a flip; transpose X and Y for odd rotations
  int flip = (operation / 4) ? -1 : 1;
  *nxout = nx;
  *nyout = ny;
  if (rotation % 2) {
    *nxout = ny;
    *nyout = nx;
    
    // For flipping before rotation, exchange the 1 and 3 rotations
    if (flip < 0 && (operation & 4) != 0)
      rotation = 4 - rotation;
  }

  // It is important to realize that these coordinates all start at upper left of image
  xstart = 0;
  ystart = 0;
  if (rotation > 1)
    xstart = *nxout - 1;
  if (rotation == 1 || rotation == 2)
    ystart = *nyout - 1;
  if (flip < 0)
    xstart = *nxout - 1 - xstart;

  // From X and Y increments along and between lines, get index increments
  dalong = flip * xalong[rotation] + *nxout * yalong[rotation];
  dinter = flip * xinter[rotation] + *nxout * yinter[rotation];

  // Do the copy
  numStrips = ny / 8;
  if (mode == MRC_MODE_FLOAT) {

    // FLOATS
    bflnStart = fbrray + xstart + *nxout * ystart;
    aflnStart = farray;
    for (strip = 0, iy = 0; strip < numStrips; strip++, iy += 8) {
      afln1 = aflnStart;
      afln2 = afln1 + nx;
      afln3 = afln2 + nx;
      afln4 = afln3 + nx;
      afln5 = afln4 + nx;
      afln6 = afln5 + nx;
      afln7 = afln6 + nx;
      afln8 = afln7 + nx;
      bfln1 = bflnStart;
      bfln2 = bfln1 + dinter;
      bfln3 = bfln2 + dinter;
      bfln4 = bfln3 + dinter;
      bfln5 = bfln4 + dinter;
      bfln6 = bfln5 + dinter;
      bfln7 = bfln6 + dinter;
      bfln8 = bfln7 + dinter;
      bflnStart = bfln8 + dinter;
      for (ix = 0; ix < nx; ix++) {
        *bfln1 = *afln1++;
        bfln1 += dalong;
        *bfln2 = *afln2++;
        bfln2 += dalong;
        *bfln3 = *afln3++;
        bfln3 += dalong;
        *bfln4 = *afln4++;
        bfln4 += dalong;
        *bfln5 = *afln5++;
        bfln5 += dalong;
        *bfln6 = *afln6++;
        bfln6 += dalong;
        *bfln7 = *afln7++;
        bfln7 += dalong;
        *bfln8 = *afln8++;
        bfln8 += dalong;
      }
      aflnStart = afln8;
    }
    for (; iy < ny; iy++) {
      bfln = bflnStart;
      for (ix = 0; ix < nx; ix++) {
        *bfln = *aflnStart++;
        bfln += dalong;
      }
      bflnStart += dinter;
    }

  } else if (mode != MRC_MODE_BYTE) {

    // INTEGERS
    blineStart = brray + xstart + *nxout * ystart;
    alineStart = array;
    for (strip = 0, iy = 0; strip < numStrips; strip++, iy += 8) {
      aline1 = alineStart;
      aline2 = aline1 + nx;
      aline3 = aline2 + nx;
      aline4 = aline3 + nx;
      aline5 = aline4 + nx;
      aline6 = aline5 + nx;
      aline7 = aline6 + nx;
      aline8 = aline7 + nx;
      bline1 = blineStart;
      bline2 = bline1 + dinter;
      bline3 = bline2 + dinter;
      bline4 = bline3 + dinter;
      bline5 = bline4 + dinter;
      bline6 = bline5 + dinter;
      bline7 = bline6 + dinter;
      bline8 = bline7 + dinter;
      blineStart = bline8 + dinter;
      for (ix = 0; ix < nx; ix++) {
        *bline1 = *aline1++;
        bline1 += dalong;
        *bline2 = *aline2++;
        bline2 += dalong;
        *bline3 = *aline3++;
        bline3 += dalong;
        *bline4 = *aline4++;
        bline4 += dalong;
        *bline5 = *aline5++;
        bline5 += dalong;
        *bline6 = *aline6++;
        bline6 += dalong;
        *bline7 = *aline7++;
        bline7 += dalong;
        *bline8 = *aline8++;
        bline8 += dalong;
      }
      alineStart = aline8;
    }
    for (; iy < ny; iy++) {
      bline = blineStart;
      for (ix = 0; ix < nx; ix++) {
        *bline = *alineStart++;
        bline += dalong;
      }
      blineStart += dinter;
    }

  } else {

    // BYTES
    bublnStart = ubbrray + xstart + *nxout * ystart;
    aublnStart = ubarray;
    for (strip = 0, iy = 0; strip < numStrips; strip++, iy += 8) {
      aubln1 = aublnStart;
      aubln2 = aubln1 + nx;
      aubln3 = aubln2 + nx;
      aubln4 = aubln3 + nx;
      aubln5 = aubln4 + nx;
      aubln6 = aubln5 + nx;
      aubln7 = aubln6 + nx;
      aubln8 = aubln7 + nx;
      bubln1 = bublnStart;
      bubln2 = bubln1 + dinter;
      bubln3 = bubln2 + dinter;
      bubln4 = bubln3 + dinter;
      bubln5 = bubln4 + dinter;
      bubln6 = bubln5 + dinter;
      bubln7 = bubln6 + dinter;
      bubln8 = bubln7 + dinter;
      bublnStart = bubln8 + dinter;
      for (ix = 0; ix < nx; ix++) {
        *bubln1 = *aubln1++;
        bubln1 += dalong;
        *bubln2 = *aubln2++;
        bubln2 += dalong;
        *bubln3 = *aubln3++;
        bubln3 += dalong;
        *bubln4 = *aubln4++;
        bubln4 += dalong;
        *bubln5 = *aubln5++;
        bubln5 += dalong;
        *bubln6 = *aubln6++;
        bubln6 += dalong;
        *bubln7 = *aubln7++;
        bubln7 += dalong;
        *bubln8 = *aubln8++;
        bubln8 += dalong;
      }
      aublnStart = aubln8;
    }
    for (; iy < ny; iy++) {
      bubln = bublnStart;
      for (ix = 0; ix < nx; ix++) {
        *bubln = *aublnStart++;
        bubln += dalong;
      }
      bublnStart += dinter;
    }
  }
}

// Copy a gain reference file to the directory if there is not a newer one
int TemplatePlugIn::CopyK2ReferenceIfNeeded()
{
  WIN32_FIND_DATA findRefData, findCopyData;
  HANDLE hFindRef, hFindCopy;
  string saveDir = m_strSaveDir;
  string rootName = m_strRootName;
  char *prefix[2] = {"Count", "Super"};
  int ind, retVal = 0;
  bool needCopy = true, namesOK = false;
  int prefInd = sReadModes[m_iReadMode] == K2_SUPERRES_READ_MODE ? 1 : 0;

  // For single image files, find the date-time root and split up the name
  if (m_bFilePerImage) {
    ind = (int)saveDir.find_last_of("\\");
    if (ind < 2 || ind >= (int)saveDir.length() - 1)
      return 1;
    rootName = saveDir.data() + ind + 1;
    saveDir.erase(ind, string::npos);
  }


  // Get the reference file first
  hFindRef = FindFirstFile(m_strGainRefToCopy, &findRefData);
  if (hFindRef == INVALID_HANDLE_VALUE) {
    sprintf(m_strTemp, "Cannot find K2 gain reference file: %s\n", 
      m_strGainRefToCopy);
    ErrorToResult(m_strTemp);
    return 1;
  } 
  FindClose(hFindRef);

  // If there is an existing copy and the mode and the directory match, find the copy
  // Otherwise look for all matching files in directory
  if (m_strLastRefName && strstr(m_strLastRefName, prefix[prefInd]) && 
    !strcmp(m_strLastRefDir, saveDir.data())) {
    //sprintf(m_strTemp, "Finding %s\n", m_strLastRefName);
    hFindCopy = FindFirstFile(m_strLastRefName, &findCopyData);
    namesOK = true;
  } else {
    sprintf(m_strTemp, "%s\\%sRef_*.dm4", saveDir.data(), prefix[prefInd]);
    hFindCopy = FindFirstFile(m_strTemp, &findCopyData);
    //sprintf(m_strTemp, "finding %s\\%sRef_*.dm4\n", saveDir.data(), prefix[prefInd]);
  }
  // DebugToResult(m_strTemp);

  // Test that file or all candidate files in the directory and stop if find one
  // is sufficiently newer then the ref
  if (hFindCopy != INVALID_HANDLE_VALUE) {
    while (needCopy) {

      // The DM reference access and creation times match and the last write time is a bit
      // later.  Copies on a samba drive are given the original creation and write times,
      // but can lose precision but can come out earlier when comparing last write time,
      // although they are given current access times.  Copies on the SSD RAID are given 
      // the original write time but a current creation and access time
      double refSec = 429.4967296 * findRefData.ftCreationTime.dwHighDateTime + 
        1.e-7 * findRefData.ftCreationTime.dwLowDateTime;
      double copySec = 429.4967296 * findCopyData.ftCreationTime.dwHighDateTime + 
        1.e-7 * findCopyData.ftCreationTime.dwLowDateTime;
      //sprintf(m_strTemp, "refSec  %f  copySec %f\n", refSec, copySec);
      //DebugToResult(m_strTemp);
      needCopy = refSec > copySec + 10.;
      if (!needCopy || !FindNextFile(hFindCopy, &findCopyData))
        break;
    }
    FindClose(hFindCopy);
  }
  if (!needCopy && namesOK)
    return 0;

  // Fix up the directory and reference name if they are changed, whether it is an old
  // reference or a new copy
  B3DFREE(m_strLastRefDir);
  B3DFREE(m_strLastRefName);
  m_strLastRefDir = strdup(saveDir.data());
  if (!needCopy) {
    sprintf(m_strTemp, "%s\\%s", saveDir.data(), findCopyData.cFileName);
    m_strLastRefName = strdup(m_strTemp);
    return 0;
  }
  sprintf(m_strTemp, "%s\\%sRef_%s.dm4", saveDir.data(), prefix[prefInd],
    rootName.data());
  m_strLastRefName = strdup(m_strTemp);

  // Copy the reference
  DebugToResult("Making new copy of gain reference\n");
  if (!CopyFile(m_strGainRefToCopy, m_strTemp, false)) {
    sprintf(m_strTemp, "An error occurred copying %s to %s\n", m_strGainRefToCopy,
      m_strTemp);
    ErrorToResult(m_strTemp);
    B3DFREE(m_strLastRefName);
    return 1;
  }

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

// Set the read mode and the scaling factor
void TemplatePlugIn::SetReadMode(long mode, double scaling)
{
  if (mode > 2)
    mode = 2;

  //  Constrain the scaling to a maximum of 1 in linear mode; old SEM always sends 
  // counting mode scaling, newer SEM sends 1 for Summit linear or 0.25 for Base mode
  if (mode <= 0 && scaling > 1.)
    scaling = 1.;
  m_iReadMode = mode;
  m_fFloatScaling = (float)scaling;
}

// Set the parameters for the next K2 acquisition
void TemplatePlugIn::SetK2Parameters(long mode, double scaling, long hardwareProc, 
                                     BOOL doseFrac, double frameTime, BOOL alignFrames, 
                                     BOOL saveFrames, char *filter)
{
  SetReadMode(mode, scaling);
  m_iHardwareProc = hardwareProc;
  m_bDoseFrac = doseFrac;
  m_dFrameTime = frameTime;
  m_bAlignFrames = alignFrames;
  if (alignFrames) {
    strncpy(m_strFilterName, filter, MAX_FILTER_NAME - 1);
    m_strFilterName[MAX_FILTER_NAME - 1] = 0x00;
  }
  m_bSaveFrames = saveFrames;
  sprintf(m_strTemp, "SetK2Parameters called with save %s\n", m_bSaveFrames ? "Y":"N");
  DebugToResult(m_strTemp);

}

// Setup properties for saving frames in files
void TemplatePlugIn::SetupFileSaving(long rotationFlip, BOOL filePerImage, 
                                     double pixelSize, long flags, double dummy1, 
                                     double dummy2, double dummy3, double dummy4, 
                                     char *dirName, char *rootName, char *refName,
                                     char *command, long *error)
{
  struct _stat statbuf;
  FILE *fp;
  int newDir, dummy;
  m_iRotationFlip = rotationFlip;
  m_bFilePerImage = filePerImage;
  m_dPixelSize = pixelSize;
  m_iSaveFlags = flags;
  m_bWriteTiff = flags & (K2_SAVE_LZW_TIFF | K2_SAVE_ZIP_TIFF);
  m_iTiffCompression = (flags & K2_SAVE_ZIP_TIFF) ? 8 : 5;

  // Copy all the strings if they are changed
  if (CopyStringIfChanged(dirName, &m_strSaveDir, newDir, error))
    return;
  if (CopyStringIfChanged(rootName, &m_strRootName, dummy, error))
    return;
  if (refName && (flags & K2_COPY_GAIN_REF)) {
    if (CopyStringIfChanged(refName, &m_strGainRefToCopy, dummy, error))
      return;
  }
  if (command && (flags & K2_RUN_COMMAND)) {
    if (CopyStringIfChanged(command, &m_strPostSaveCom, dummy, error))
      return;
  }

  sprintf(m_strTemp, "SetupFileSaving called with rf %d %s fpi %s pix %f dir %s root %s\n" 
    , rotationFlip, m_bWriteTiff ? "TIFF" : "MRC", filePerImage ? "Y":"N", pixelSize, 
    m_strSaveDir, m_strRootName);
  DebugToResult(m_strTemp);

  // For one file per image, create the directory, which must not exist
  *error = 0;
  if (filePerImage) {
    if (_mkdir(m_strSaveDir)) {
      if (errno == EEXIST)
        *error = DIR_ALREADY_EXISTS;
      else
        *error = DIR_CREATE_ERROR;
    }
  } else {

    // Otherwise, make sure directory exists and is a directory
    if (_stat(m_strSaveDir, &statbuf))
      *error = DIR_NOT_EXIST;
    if (! *error && !(statbuf.st_mode & _S_IFDIR))
      *error = SAVEDIR_IS_FILE;

    // Check whether the file exists
    sprintf(m_strTemp, "%s\\%s.mrc", m_strSaveDir, m_strRootName);
    if (! *error && !_stat(m_strTemp, &statbuf))
      *error = FILE_ALREADY_EXISTS;

    // For a new directory, check writability by opening file
    if (! *error && newDir) {
      fp = fopen(m_strTemp, "wb");
      if (!fp)
        *error = DIR_NOT_WRITABLE;
      else
        fclose(fp);
    }
  }
  if (*error) {
    sprintf(m_strTemp, "SetupFileSaving error is %d\n", *error);
    DebugToResult(m_strTemp);
  }
}

// Duplicate a string into the given member variable if it has changed
int TemplatePlugIn::CopyStringIfChanged(char *newStr, char **memberStr, int &changed,
                                        long *error)
{
  changed = 1;
  if (*memberStr)
    changed = strcmp(*memberStr, newStr);
  if (changed) {
    B3DFREE(*memberStr);
    *memberStr = strdup(newStr);
    if (!*memberStr) {
      *error = ROTBUF_MEMORY_ERROR;
      return 1;
    }
  }
  return 0;
}

// Get results of last frame saving operation
void TemplatePlugIn::GetFileSaveResult(long *numSaved, long *error)
{
  *numSaved = m_iFramesSaved;
  *error = m_iErrorFromSave;
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

// Set the insertion state of the given camera
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
  if ((code >> 24) < 4 && ((code >> 16) & 0xff) < 11)
  	m_iDMVersion = 100 * (code >> 24) + 10 * ((code >> 16) & 0xff) + 
	  	((code >> 8) & 0xff);
  else
   	m_iDMVersion = 10000 * (code >> 24) + 100 * ((code >> 16) & 0xff) + 
	  	((code >> 8) & 0xff);

  sprintf(m_strTemp, "retval = %g, code = %x, version = %d\n", retval, code, m_iDMVersion);
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

/*
 * DIGISCAN: return flyback time and line frequency
 */
int TemplatePlugIn::GetDSProperties(long extraDelay, double addedFlyback, double margin,
                                    double *flyback, double *lineFreq, double *rotOffset,
                                    long *doFlip)
{
  DebugToResult("In GetDSProperties\n");
  m_iExtraDSdelay = extraDelay;
  m_dSyncMargin = margin;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetFlyBackTime()\n"
    "Exit(retval)\n");
  m_strCommand += m_strTemp;
	double retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
  *flyback = retval;
  m_dFlyback = retval + addedFlyback;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetLineFrequency()\n"
    "Exit(retval)\n");
  m_strCommand += m_strTemp;
  retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
  *lineFreq = retval;
  m_dLineFreq = retval;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetRotationOffset()\n"
    "Exit(retval)\n");
  m_strCommand += m_strTemp;
  retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
  *rotOffset = retval;
	m_strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetDoFlip()\n"
    "Exit(retval)\n");
  m_strCommand += m_strTemp;
  retval = ExecuteScript((char *)m_strCommand.c_str());
	if (retval == SCRIPT_ERROR_RETURN)
		return 1;
  *doFlip = retval != 0. ? 1 : 0;
  return 0;
}

/*
 * Acquire DigiScan image from one or more channels, return first channel
 * If continuous = 1, start continuous acquire then return image
 * If continuous = -1, return an image from continuous acquire
 */
int TemplatePlugIn::AcquireDSImage(short array[], long *arrSize, long *width, 
                                  long *height, double rotation, double pixelTime, 
                                  long lineSync, long continuous, long numChan, 
                                  long channels[], long divideBy2)
{
  int chan, j, again, dataSize = 2;
  // 7/25/13: Set based on fact that 20 second exposures were not safe even with 800
  // msec extra shot delay.  This is extra-generous, 1.04 should do it.
  float delayErrorFactor = 1.05f;
  double fullExpTime = *height * (*width * pixelTime + m_dFlyback + 
    (lineSync ? m_dSyncMargin : 0.)) / 1000.;

  // If continuing with continuous, 
  if (continuous < 0) {

    // Return with error if continuous is supposed to be started and isn't
    if (!m_bContinuousDS)
      return 1;

    // Timeout since last return of data
    double elapsed = GetTickCount() - m_dLastReturnTime;
    if (elapsed < 0)
      elapsed += 4294967296.;
    again = (int)(fullExpTime + 180 - elapsed);
    if (again > 0)
      Sleep(again); 

    // Continuing acquire: just get the image
    return ReturnDSChannel(array, arrSize, width, height, channels[0], divideBy2);
  }

  // Stop continuous acquire if it was started
  if (m_bContinuousDS)
    StopDSAcquisition();

	m_strCommand.resize(0);
  m_strCommand += "Number exists, xsize, ysize, oldx, oldy, nbytes, idchan, idfirst\n";
  m_strCommand += "image imdel, imchan\n";
  m_strCommand += "String channame\n";

  // First see if there are channels that haven't been returned and need to be deleted
  for (chan = 0; chan < MAX_DS_CHANNELS; chan++) {
    if (m_iDSAcquired[chan] == CHAN_ACQUIRED || 
      (!m_bGMS2 && m_iDSAcquired[chan] != CHAN_UNUSED)) {
      again = 0;
      for (j = 0; j < numChan; j++)
        if (channels[j] == chan)
          again = 1;
      if (!again) {
        sprintf(m_strTemp, "exists = GetNamedImage(imdel, \"SEMchan%d\")\n"
          "if (exists)\n"
          "  DeleteImage(imdel)\n", chan);
        m_strCommand += m_strTemp;
      }
    }
    m_iDSAcquired[chan] = CHAN_UNUSED;
  }

  // Set the parameters for acquisition
  sprintf(m_strTemp, "xsize = %d\n"
    "ysize = %d\n"
    "Number paramID = DSCreateParameters(xsize, ysize, %f, %f, %d)\n", 
    *width, *height, rotation, pixelTime, lineSync ? 1: 0);
  m_strCommand += m_strTemp;

  // Add commands for each channel
  for (chan = 0; chan < numChan; chan++) {
    sprintf(m_strTemp, "channame = \"SEMchan%d\"\n"
      "nbytes = %d\n"
      "exists = GetNamedImage(imchan, channame)\n"
      "if (exists) {\n"
    	"  Get2DSize(imchan, oldx, oldy)\n"
	    "  if (! IsIntegerDataType(imchan, nbytes, 0) || oldx != xsize || oldy != ysize) {\n"
		  "    exists = 0\n"
      "    DeleteImage(imchan)\n"
      "  }\n"
      "}\n"
      "if (! exists) {\n"
      "  imchan := IntegerImage(channame, nbytes, 0, xsize, ysize)\n",
      channels[chan], dataSize);
    m_strCommand += m_strTemp;
    if (!m_bGMS2)
      m_strCommand += "  ShowImage(imchan)\n";
    else
      m_strCommand += "  KeepImage(imchan)\n";
    m_strCommand += "}\n"
      "idchan = GetImageID(imchan)\n";
    if (!chan)
      m_strCommand += "idfirst = idchan\n";
    sprintf(m_strTemp, "DSSetParametersSignal(paramID, %d, nbytes, 1, idchan)\n", 
      channels[chan]);
    m_strCommand += m_strTemp;
    m_iDSAcquired[channels[chan]] = CHAN_ACQUIRED;
  }

  // Acquisition and return commands
  if (!continuous) {
    //j = (int)((fullExpTime + m_iExtraDSdelay) * delayErrorFactor * 0.06 + 0.5);
    if (m_iExtraDSdelay > 0) {

      // With this loop, it doesn't seem to need any delay at the end
      m_strCommand += "DSStartAcquisition(paramID, 0, 0)\n"
        "while (DSIsViewActive()) {\n"
        "  Delay(2)\n"
        "}\n";
      //sprintf(m_strTemp, "Delay(%d)\n", j);
      //m_strCommand += m_strTemp;
    } else {
      m_strCommand += "DSStartAcquisition(paramID, 0, 1)\n";
    }
    m_strCommand += "DSDeleteParameters(paramID)\n"
    "Exit(idfirst)\n";

    again = AcquireAndTransferImage((void *)array, dataSize, arrSize, width, height,
      divideBy2, 0, m_bGMS2 ? DEL_IMAGE : NO_DEL_IM, NO_SAVE);
    if (again != DM_CALL_EXCEPTION)
      m_iDSAcquired[channels[0]] = CHAN_RETURNED;
    return again;
  } else {

    // For continuous acquire, start it, get the parameter ID, 
    m_strCommand += "DSStartAcquisition(paramID, 1, 0)\n"
      "Exit(paramID + 1000000. * idfirst)\n";
    double retval = ExecuteScript((char *)m_strCommand.c_str());
	  if (retval == SCRIPT_ERROR_RETURN)
		  return 1;
    m_iDSimageID = (int)(retval / 1000000. + 0.5);
    m_iDSparamID = (int)(retval + 0.5 - 1000000. * m_iDSimageID);
    m_bContinuousDS = true;
    m_dContExpTime = *width * *height * pixelTime / 1000.;
    for (j = 0; j < *width * *height; j++)
      array[j] = 0;
    m_dLastReturnTime = GetTickCount();
    return 0;
  }
}

/*
 * Return image from another channel of already acquired image
 */
int TemplatePlugIn::ReturnDSChannel(short array[], long *arrSize, long *width, 
                                   long *height, long channel, long divideBy2)
{
  if (m_iDSAcquired[channel] != CHAN_ACQUIRED)
    return 1;
  m_strCommand.resize(0);
  if (!m_bContinuousDS) {
    sprintf(m_strTemp, "image imzero\n"
      "Number idzero = -1.\n"
      "Number exists = GetNamedImage(imzero, \"SEMchan%d\")\n"
      "if (exists)\n" 
      "  idzero = GetImageID(imzero)\n"
      "Exit(idzero)\n", channel);
    m_strCommand += m_strTemp;
  }
  int retval = AcquireAndTransferImage((void *)array, 2, arrSize, width, height, 
    divideBy2, 0, (!m_bContinuousDS && m_bGMS2) ? DEL_IMAGE : NO_DEL_IM, NO_SAVE);
  if (retval != DM_CALL_EXCEPTION && !m_bContinuousDS)
    m_iDSAcquired[channel] = CHAN_RETURNED;
  return retval;
}

int TemplatePlugIn::StopDSAcquisition()
{
  if (!m_bContinuousDS)
    return 1;
  m_strCommand.resize(0);
  sprintf(m_strTemp, "DSStopAcquisition(%d)\n"
      "Delay(%d)\n"
      "DSDeleteParameters(%d)\n"
      "Exit(0)\n", m_iDSparamID, (int)(0.06 * m_iExtraDSdelay + 0.5), m_iDSparamID);
  m_strCommand += m_strTemp;
  double retval = ExecuteScript((char *)m_strCommand.c_str());
  m_iDSparamID = 0;
  m_iDSimageID = 0;
  m_bContinuousDS = false;
  //m_dLastReturnTime = GetTickCount();
  return (retval == SCRIPT_ERROR_RETURN) ? 1 : 0;
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

void PlugInWrapper::SetReadMode(long mode, double scaling)
{
	gTemplatePlugIn.SetReadMode(mode, scaling);
}

void PlugInWrapper::SetK2Parameters(long readMode, double scaling, long hardwareProc, 
                                    BOOL doseFrac, double frameTime, BOOL alignFrames, 
                                    BOOL saveFrames, char *filter)
{
  gTemplatePlugIn.SetK2Parameters(readMode, scaling, hardwareProc, doseFrac, frameTime, 
    alignFrames, saveFrames, filter);
}
void PlugInWrapper::SetupFileSaving(long rotationFlip, BOOL filePerImage, 
                                    double pixelSize, long flags, double dummy1, 
                                    double dummy2, double dummy3, double dummy4, 
                                    char *dirName, char *rootName, char *refName, 
                                    char *command, long *error)
{
  gTemplatePlugIn.SetupFileSaving(rotationFlip, filePerImage, pixelSize, flags, dummy1,
    dummy2, dummy3, dummy4, dirName, rootName, refName, command, error);
}

void PlugInWrapper::GetFileSaveResult(long *numSaved, long *error)
{
  gTemplatePlugIn.GetFileSaveResult(numSaved, error);
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

int PlugInWrapper::GetDSProperties(long timeout, double addedFlyback, double margin,
                                   double *flyback, 
                                   double *lineFreq, double *rotOffset, long *doFlip)
{
  return gTemplatePlugIn.GetDSProperties(timeout, addedFlyback, margin, flyback, lineFreq, 
    rotOffset, doFlip);
}

int PlugInWrapper::AcquireDSImage(short array[], long *arrSize, long *width, 
                                  long *height, double rotation, double pixelTime, 
                                  long lineSync, long continuous, long numChan, 
                                  long channels[], long divideBy2)
{
  return gTemplatePlugIn.AcquireDSImage(array, arrSize, width, height, rotation, 
    pixelTime, lineSync, continuous, numChan, channels, divideBy2);
}

int PlugInWrapper::ReturnDSChannel(short array[], long *arrSize, long *width, 
                                   long *height, long channel, long divideBy2)
{
  return gTemplatePlugIn.ReturnDSChannel(array, arrSize, width, height, channel,
    divideBy2);
}

int PlugInWrapper::StopDSAcquisition()
{
  return gTemplatePlugIn.StopDSAcquisition();
}

void PlugInWrapper::ErrorToResult(const char *strMessage, const char *strPrefix)
{
  gTemplatePlugIn.ErrorToResult(strMessage, strPrefix);
}
void PlugInWrapper::DebugToResult(const char *strMessage, const char *strPrefix)
{
  gTemplatePlugIn.DebugToResult(strMessage, strPrefix);
}
int PlugInWrapper::GetDebugVal()
{
  return gTemplatePlugIn.m_iDebugVal;
}

// Dummy functions for 32-bit.  Ultimately we would like code needed library functions
// to be in a different module
#ifndef _WIN64
void mrc_set_scale(MrcHeader *h, double x, double y, double z){}
int mrc_head_new(MrcHeader *hdata, int x, int y, int z, int mode){ return 0;}
int mrc_head_label(MrcHeader *hdata, const char *label) {  return 0;}
int mrc_head_write(FILE *fout, MrcHeader *hdata) { return 0;}
int mrc_big_seek(FILE *fp, int base, int size1, int size2, int flag) { return 0;}
size_t b3dFwrite(void *buf, size_t size, size_t count, FILE *fp) { return 0;}
double wallTime(void) { return 0.;}
ImodImageFile *iiNew(void) {
  ImodImageFile *dummy = NULL;
  return dummy;
}
void iiClose(ImodImageFile *inFile) {}
void iiDelete(ImodImageFile *inFile) {}
int tiffOpenNew(ImodImageFile *inFile) {return 0;}
int tiffWriteSection(ImodImageFile *inFile, void *buf, int compression, 
                     int inverted, int resolution, int quality) {return 0;}

#endif
