#include "stdafx.h"

// For GMS2, GMS_MINOR_VERSION must be defined to a single digit below 3 (0, 1, 2) or to 
// double digits for 3 onwards (30, 31, etc).  GMS_SDK_VERSION goes to 3 digits for GMS3
// To build all versions starting with 31 - x64:
// Define to 30, build 30 - x64 then switch to GMS2-32bit - Win32, build 30 - Win32
// Define to 0, build 0 - Win32
// Define to 2, switch to GMS2-64bit - x64, build 2 - x64
// Return to 31
#ifndef GMS_MINOR_VERSION
#define GMS_MINOR_VERSION -1
#endif

#ifndef GMS_MAJOR_VERSION
#define GMS_MAJOR_VERSION 1
#endif
#if GMS_MAJOR_VERSION > 2
#define GMS_SDK_VERSION (100 * GMS_MAJOR_VERSION + GMS_MINOR_VERSION)
#else
#define GMS_SDK_VERSION GMS_MINOR_VERSION
#endif

#define _GATANPLUGIN_WIN32_DONT_DEFINE_DLLMAIN
#define _GATANPLUGIN_USES_LIBRARY_VERSION 2

#if GMS_SDK_VERSION >= 300
#include "GMSFoundation.h"
#endif

#include "DMPlugInbasic.h"

#define _GATANPLUGIN_USE_CLASS_PLUGINMAIN
#include "DMPlugInMain.h"
#include "DMPluginCamera.h"

using namespace Gatan;

#include "TemplatePlugIn.h"
#if defined(_WIN64) && GMS_SDK_VERSION < 31
#include "K2DoseFractionation.h"
#endif
#include <string>
#include <vector>
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
#define DATA_MUTEX_WAIT  100
#define FRAME_EVENT_WAIT  200
#define IMAGE_MUTEX_WAIT  1000
#define IMAGE_MUTEX_CLEAN_WAIT  5000
enum {CHAN_UNUSED = 0, CHAN_ACQUIRED, CHAN_RETURNED};
enum {NO_SAVE = 0, SAVE_FRAMES};
enum {NO_DEL_IM = 0, DEL_IMAGE};
enum {WAIT_FOR_THREAD = 0, WAIT_FOR_RETURN, WAIT_FOR_NEW_SHOT, WAIT_FOR_CONTINUOUS};

// Mapping from program read modes (0-2) to values for K2.  The read mode index >= 0 is
// the marker for a K2, so OveView diffraction mode is sent as -2 and not included here
static int sReadModes[3] = {K2_LINEAR_READ_MODE, K2_COUNTING_READ_MODE, 
K2_SUPERRES_READ_MODE};

// Values to send to CM_SetHardwareCorrections or K2_SetHardwareProcessing given hardware
// processing value of 0, 2, 4, 6 divided by 2
static int sCMHardCorrs[4] = {0x0, 0x100, 0x200, 0x300};
static int sK2HardProcs[4] = {0, 2, 4, 6};

// Transpose values to use to cancel default transpose for GMS 2.3.1 with dose frac
static int sInverseTranspose[8] = {0, 258, 3, 257, 1, 256, 2, 259};

// THE debug mode flag
static BOOL sDebug;

// Handle for mutexes for writing critical components of thread data and continuous image
static HANDLE sDataMutexHandle;
static HANDLE sImageMutexHandle;
static HANDLE sFrameReadyEvent = NULL;
static int sJ;

// Array for storing continuously acquired images to pass from thread to caller
static short *sContinuousArray = NULL;

#define DELETE_CONTINUOUS {delete [] sContinuousArray; sContinuousArray = NULL;}

// Structure to hold data to pass to acquire proc/thread
struct ThreadData 
{
  // Arguments to AcquireAndTransferImage
  void *array;
  int dataSize;
  long arrSize, width, height, divideBy2, transpose, delImage, saveFrames;

  // former members of the class, used to start with m_
  float fFloatScaling;
  BOOL bFilePerImage;
  int iFramesSaved;
  int iErrorFromSave;
  double dPixelSize;
  int iRotationFlip;
  int iSaveFlags;
  BOOL bWriteTiff;
  int iTiffCompression;
  int iTiffQuality;
  BOOL bAsyncSave;
  double dK2Exposure;
  double dK2Settling;
  int iK2Binning;
  int iK2Shutter;
  int iK2Processing;
  int iLeft;
  int iRight;
  int iTop;
  int iBottom;
  string strCommand;
  char strTemp[MAX_TEMP_STRING];   // Give it its own temp string separate from class
  int iDSimageID;
  double dLastReturnTime;
  int iReadMode;
  double dFrameTime;
  BOOL bAlignFrames;
  char strFilterName[MAX_FILTER_NAME];
  int iHardwareProc;
  string strRootName;
  string strSaveDir;
  string strGainRefToCopy;
  string strLastRefName;
  string strLastRefDir;

  // New non-internal items
  int iNumFramesToSum;
  int iNumSummed;
  int iNumGrabAndStack;
  bool bEarlyReturn;
  bool bAsyncToRAM;
  int iReadyToAcquire;
  int iReadyToReturn;
  int iDMquitting;
  int iFrameRotFlip;
  bool bDoContinuous;
  bool bSetContinuousMode;
  bool bUseAcquisitionObj;
  int iContinuousQuality;
  int iCorrections;
  int iEndContinuous;
  int iContWidth, iContHeight;
  int iWaitingForFrame;
  bool bUseOldAPI;
  int iAntialias;
  int iFinalBinning;
  int iFinalWidth;
  int iFinalHeight;
  bool bSaveSummedFrames;
  vector<short> outSumFrameList;
  vector<short> numFramesInOutSum;
  int outSumFrameIndex;
  int numAddedToOutSum;
  int numOutSumsDoneAtIndex;

  // Items needed internally in save routine and its functions
  FILE *fp;
  ImodImageFile *iifile;
  short *outData, *rotBuf, *tempBuf, *outSumBuf;
  int *sumBuf;
  void **grabStack;
  MrcHeader hdata;
  int fileMode, outByteSize, byteSize;
  bool isInteger, isUnsignedInt, isFloat, signedBytes, save4bit;
  float writeScaling;
};

// Local functions callable from thread
static DWORD WINAPI AcquireProc(LPVOID pParam);
static void  ProcessImage(void *imageData, void *array, int dataSize, long width, 
                          long height, long divideBy2, long transpose, int byteSize, 
                          bool isInteger, bool isUnsignedInt, float floatScaling);
static int PackAndSaveImage(ThreadData *td, void *array, int nxout, int nyout, int slice, 
                            bool finalFrame, int &fileSlice, int &tmin,
                            int &tmax, float &meanSum);
static int GetTypeAndSizeInfo(ThreadData *td, DM::Image &image, int loop, int outLimit,
                              bool doingStack);
static int InitOnFirstFrame(ThreadData *td, bool needTemp, bool needSum, 
                            long &frameDivide, bool &needProc);
static void RotateFlip(short int *array, int mode, int nx, int ny, int operation, 
                       bool invert, short int *brray, int *nxout, int *nyout);
static void AddToSum(ThreadData *td, void *data, void *sumArray);
static void AccumulateWallTime(double &cumTime, double &wallStart);
static void DeleteImageIfNeeded(ThreadData *td, DM::Image &image, bool *needsDelete);
static void SetWatchedDataValue(int &member, int value);
static int GetWatchedDataValue(int &member);
static double TickInterval(double start);
static int CopyK2ReferenceIfNeeded(ThreadData *td);
static void DebugToResult(const char *strMessage, const char *strPrefix = NULL);
static void ErrorToResult(const char *strMessage, const char *strPrefix = NULL);
static BOOL SleepMsg(DWORD dwTime_ms);
static double ExecuteScript(char *strScript);
static int RunContinuousAcquire(ThreadData *td);
static void AntialiasReduction(ThreadData *td, void *array);

// Declarations of global functions called from here
void TerminateModuleUninitializeCOM();
BOOL WasCOMInitialized();
int GetSocketInitialization(int &wsaError);
int StartSocket(int &wsaError);
void ShutdownSocket(void);
bool CallIsFromSocket(void);
#ifdef _WIN64
void K2_SetHardwareProcessing(const CM::CameraPtr &camera, long processing);
#endif

// The plugin class
class TemplatePlugIn :  public Gatan::PlugIn::PlugInMain
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
    double frameTime, BOOL alignFrames, BOOL saveFrames, long rotationFlip, long flags, 
    double dummy1, double dummy2, double dummy3, double dummy4, char *filter);
  void SetupFileSaving(long rotationFlip, BOOL filePerImage, double pixelSize, long flags,
    double dummy1, double dummy2, double dummy3, double dummy4, char *dirName, 
    char *rootName, char *refName, char *defects, char *command, char *sumList, 
    long *error);
  void GetFileSaveResult(long *numSaved, long *error);
  int GetDefectList(short xyPairs[], long *arrSize, long *numPoints, 
    long *numTotal);
  double ExecuteClientScript(char *strScript, BOOL selectCamera);
  int AcquireAndTransferImage(void *array, int dataSize, long *arrSize, long *width,
    long *height, long divideBy2, long transpose, long delImage, long saveFrames);
  DWORD WaitForAcquireThread(int waitType);
  int CopyStringIfChanged(char *newStr, string &memberStr, int &changed, long *error);
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
  int GetContinuousFrame(short array[], long *arrSize, long *width, long *height, 
    bool firstCall);
  int StopDSAcquisition();
  int StopContinuousCamera();
  void SetDebugMode(BOOL inVal) {sDebug = inVal;};
  virtual void Start();
  virtual void Run();
  virtual void Cleanup();
  virtual void End();
  TemplatePlugIn();

  int m_iDebugVal;
  
private:
  ThreadData mTD;
  ThreadData mTDcopy;
  HANDLE m_HAcquireThread;
  BOOL m_bGMS2;
  int m_iDMVersion;
  int m_iCurrentCamera;
  int m_iDMSettlingOK[MAX_CAMERAS];
  string m_strQueue;
  char m_strTemp[MAX_TEMP_STRING];
  int m_iDSAcquired[MAX_DS_CHANNELS];
  BOOL m_bContinuousDS;
  int m_iDSparamID;
  double m_dContExpTime;
  int m_iExtraDSdelay;
  double m_dFlyback;
  double m_dLineFreq;
  double m_dSyncMargin;
  BOOL m_bDoseFrac;
  BOOL m_bSaveFrames;
  string m_strPostSaveCom;
  string m_strDefectsToSave;
};

TemplatePlugIn::TemplatePlugIn()
{
  sDebug = getenv("SERIALEMCCD_DEBUG") != NULL;
  m_iDebugVal = 0;
  if (sDebug)
    m_iDebugVal = atoi(getenv("SERIALEMCCD_DEBUG"));

  sDataMutexHandle = CreateMutex(0, 0, 0);
  sImageMutexHandle = CreateMutex(0, 0, 0);
  m_HAcquireThread = NULL;
  m_iDMVersion = 340;
  m_iCurrentCamera = 0;
  m_strQueue.resize(0);
  for (int i = 0; i < MAX_CAMERAS; i++)
    m_iDMSettlingOK[i] = 1;
  for (int j = 0; j < MAX_DS_CHANNELS; j++)
    m_iDSAcquired[j] = CHAN_UNUSED;
  sFrameReadyEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("SEMCCDFrameEvent"));
#ifdef GMS2
  m_bGMS2 = true;
#else
  m_bGMS2 = false;
#endif
  m_bContinuousDS = false;
  m_iDSparamID = 0;
  mTD.iDSimageID = 0;
  m_iExtraDSdelay = 10;
  m_dFlyback = 400.;
  m_dLineFreq = 60.;
  m_dSyncMargin = 10.;
  mTD.iReadMode = -1;
  mTD.fFloatScaling = 1.;
  mTD.iHardwareProc = 6;
  m_bDoseFrac = false;
  m_bSaveFrames = false;
  mTD.iSaveFlags = 0;
  mTD.bFilePerImage = true;
  mTD.bWriteTiff = false;
  mTD.iTiffCompression = 5;  // 5 is LZW, 8 is ZIP
  mTD.iTiffQuality = -1;
  mTD.bAsyncSave = true;
  mTD.iRotationFlip = 0;
  mTD.iFrameRotFlip = 0;
  mTD.dPixelSize = 5.;
  const char *temp = getenv("SERIALEMCCD_ROOTNAME");
  if (temp)
    mTD.strRootName = temp;
  temp = getenv("SERIALEMCCD_SAVEDIR");
  if (temp)
    mTD.strSaveDir = temp;
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
    sDebug = true;
    DebugToResult("DllMain was never called when SerialEMCCD was loaded - trouble!\n");
    //PlugIn::gResultOut << "DllMain was never called - trouble!" << std::endl;
  }
  if (GetDMVersion() < 0) {
    sDebug = true;
    DebugToResult("SerialEMCCD: Error getting Digital Micrograph version\n");
  }
  HANDLE hMutex = CreateMutex(NULL, FALSE, "SEMCCD-SingleInstance");
  if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    std::string str = "WARNING: THERE ARE TWO COPIES OF THE SERIALEMCCD PLUGIN LOADED!!!"
      "\n\n"
    "  Look in both C:\\ProgramData\\Gatan\\Plugins and C:\\Program Files\\Gatan\\Plugins"
    "\n  (or in C:\\Program Files\\Gatan\\DigitalMicrograph\\Plugins on Windows XP) for\n"
    "  copies of SEMCCD-GMS2...dll.\n  Shut down DigitalMicrograph and remove the extra "
    "copy.\n  Rename the remaining file, if necessary, to remove minor version numbers\n"
    "  (i.e., it should be named SEMCCD-GMS2-32.dll or SEMCCD-GMS2-64.dll";
    MessageBox(NULL, str.c_str(), "Two Copies of SerialEM Plugin", 
      MB_OK | MB_ICONQUESTION);
    str = "\n" + str + 
      "\n\nWARNING: THERE ARE TWO COPIES OF THE SERIALEMCCD PLUGIN LOADED!!!\n\n";
    ErrorToResult(str.c_str(), "");
  }
  DebugToResult("Going to start socket\n");
  socketRet = StartSocket(wsaError);
  if (socketRet) {
    sprintf(buff, "Socket initialization failed, return value %d, WSA error %d\n", 
      socketRet, wsaError);
    ErrorToResult(buff, "SerialEMCCD: ");
  }

  overrideWriteBytes(0);
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


/*
 * Outputs messages to results window when debig flag is set
 */
static void DebugToResult(const char *strMessage, const char *strPrefix)
{
  if (!sDebug) 
    return;
  double time = (::GetTickCount() % (DWORD)3600000) / 1000.;
  char timestr[20];
  const char *findret;
  sprintf(timestr, "%.3f ", time);
  DM::OpenResultsWindow();
  DM::Result(timestr);
  if (strPrefix)
    DM::Result(strPrefix);
  else {
    findret = strchr(strMessage, '\n');
    if (findret && findret[1] != 0x00)
      DM::Result("DMCamera Debug :\n");
    else
      DM::Result("DMCamera Debug :  ");
  }
  DM::Result(strMessage );
}

/*
 * Outputs messages to the results window upon error; just the message itself if in 
 * debug mode, or a supplied or defulat prefix first if not in debug mode
 */
static void ErrorToResult(const char *strMessage, const char *strPrefix)
{
  if (sDebug) {
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
static double ExecuteScript(char *strScript)
{
  double retval;
  char last, retstr[128];
  DebugToResult(strScript, "DMCamera executing script :\n\n");
  if (sDebug) {

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
    if (!sDebug) {
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

/*
 * The external call to execute a script, optionally placing commands to select the
 * camera first
 */
double TemplatePlugIn::ExecuteClientScript(char *strScript, BOOL selectCamera)
{
  mTD.strCommand.resize(0);
  if (selectCamera)
    AddCameraSelection();
  mTD.strCommand += strScript;
  char last = mTD.strCommand[mTD.strCommand.length() - 1];
  if (last != '\n' && last != '\r')
    mTD.strCommand += "\n";
  return ExecuteScript((char *)mTD.strCommand.c_str());
}

/*
 * Add a command to a script to be executed in the future
 */
void TemplatePlugIn::QueueScript(char *strScript)
{
  m_strQueue += strScript;
  char last = m_strQueue[m_strQueue.length() - 1];
  if (last != '\n' && last != '\r')
    m_strQueue += "\n";
  DebugToResult("QueueScript called, queue is now:\n");
  if (sDebug)
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
  int newProc, err, procIn, binAdj, heightAdj, widthAdj;
  bool swapXY = mTD.iRotationFlip > 0 && mTD.iRotationFlip % 2 && m_bDoseFrac;
  if (m_bSaveFrames && mTD.iReadMode >= 0 && m_bDoseFrac && mTD.strSaveDir.length() && 
    mTD.strRootName.length())
      saveFrames = SAVE_FRAMES;

  // Strip continuous mode information from processing, set flag to do it if no conflict
  mTD.bDoContinuous = false;
  if (processing > 7) {
    procIn = processing;
    processing &= 7;
    if (saveFrames == NO_SAVE) {
      mTD.bDoContinuous = true;
      if (sContinuousArray) {
        return GetContinuousFrame(array, arrSize, width, height, false);
      }
      mTD.iContinuousQuality = (procIn >> QUALITY_BITS_SHIFT) & QUALITY_BITS_MASK;
      mTD.bUseAcquisitionObj = (procIn & CONTINUOUS_ACQUIS_OBJ) != 0;
      mTD.bSetContinuousMode = (procIn & CONTINUOUS_SET_MODE) != 0;
    }
  }
  if (!mTD.bDoContinuous && sContinuousArray)
    StopContinuousCamera();

  if (m_iDMVersion >= NEW_CAMERA_MANAGER) {
    
    // Convert the processing argument to the new format
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
  }

  //sprintf(m_strTemp, "Entering GetImage with divideBy2 %d\n", divideBy2);
  //DebugToResult(m_strTemp);
  // Workaround for Downing camera problem, inexplicable wild values coming through
  if (divideBy2 > 1 || divideBy2 < -1)
    divideBy2 = 0;
  mTD.strCommand.resize(0);
  AddCameraSelection();

  // Add and clear the queue
  if (!m_strQueue.empty()) {
    mTD.strCommand += m_strQueue;
    m_strQueue.resize(0);
  }

  // single frame doesn't work in older GMS for async saving; but don't know about newer
  mTD.iK2Processing = newProc;
  if (saveFrames == SAVE_FRAMES && B3DNINT(exposure / mTD.dFrameTime) == 1 && 
    GMS_SDK_VERSION < 31)
    mTD.bAsyncSave = false;

  // Also cancel asynchronous save if aligning and saving frames since we need to use
  // the old API, and that is set up to happen only through script calls
  mTD.bUseOldAPI = saveFrames == SAVE_FRAMES && mTD.bAlignFrames && 
    GMS_SDK_VERSION >= 31;
  if (mTD.bUseOldAPI)
    mTD.bAsyncSave = false;

  // For antialias reduction here, make sure it is allowed and set binning to 1
  if (mTD.iReadMode >= 0 && mTD.iAntialias && !(mTD.bEarlyReturn && !mTD.iNumFramesToSum))
  {
    binAdj = binning / (sReadModes[mTD.iReadMode] == K2_SUPERRES_READ_MODE ? 2 : 1);
    heightAdj = swapXY ? right - left : bottom - top;
    widthAdj = swapXY ? bottom - top : right - left;
    if (binning == 1 || mTD.iFinalWidth * binAdj > widthAdj || 
      mTD.iFinalHeight * binAdj > heightAdj || mTD.bDoContinuous) {
        if (mTD.bDoContinuous)
          sprintf(m_strTemp,"Attempting to use antialias reduction in continuous mode\n");
        else
          sprintf(m_strTemp, "Bad parameters for antialiasing: from %d x %d to %d x %d "
          "binning by %d\n", widthAdj, heightAdj, mTD.iFinalWidth, mTD.iFinalHeight, 
          binAdj);
        ErrorToResult(m_strTemp,
          "\nA problem occurred acquiring an image for SerialEM:\n");
        return BAD_ANTIALIAS_PARAM;
    }
    mTD.iFinalBinning = binning;
    binning = 1;
  } else
    mTD.iAntialias = 0;

  // Intercept K2 asynchronous saving and continuous mode here
  mTD.iTop = top;
  mTD.iBottom = bottom;
  mTD.iLeft = left;
  mTD.iRight = right;
  if ((saveFrames == SAVE_FRAMES && mTD.bAsyncSave) || mTD.bDoContinuous) {
      sprintf(m_strTemp, "Exit(0)");
      mTD.strCommand += m_strTemp;
      mTD.dK2Exposure = exposure;
      mTD.dK2Settling = settling;
      mTD.iK2Shutter = shutter;
      mTD.iK2Binning = binning;
      mTD.iCorrections = corrections;

      // Make call to acquire image or start continuous acquires
      err = AcquireAndTransferImage((void *)array, 2, arrSize, width, height,
        divideBy2, 0, DEL_IMAGE, SAVE_FRAMES);
      mTD.iAntialias = 0;

      // Then fetch the first continuous frame if that was OK
      if (!err && mTD.bDoContinuous) {
        DebugToResult("Making first call to GetContinuousFrame\n");
        return GetContinuousFrame(array, arrSize, width, height, true);
      }
      return err;
  }

  // Set up acquisition parameters
  if (m_iDMVersion >= NEW_CAMERA_MANAGER) {
    sprintf(m_strTemp, "Object acqParams = CM_CreateAcquisitionParameters_FullCCD"
      "(camera, %d, %g, %d, %d)\n", newProc, exposure, binning, binning);
    mTD.strCommand += m_strTemp;
    if (!m_bDoseFrac) {
      sprintf(m_strTemp, "CM_SetBinnedReadArea(camera, acqParams, %d, %d, %d, %d)\n",
       top, left, bottom, right);
      mTD.strCommand += m_strTemp;
    }

    // Specify corrections if incoming value is >= 0
    // As of DM 3.9.3 (3.9?) need to modify only the allowed coorections to avoid an
    // overscan image in simulator, so change 255 to 49
    if (corrections >= 0) {
      sprintf(m_strTemp, "CM_SetCorrections(acqParams, %d, %d)\n", 0x1000 + 49,
        corrections);
      mTD.strCommand += m_strTemp;
    }
    // Turn off defect correction for raw images and dark references
    //if (newProc == NEWCM_UNPROCESSED)
    //  mTD.strCommand += "CM_SetCorrections(acqParams, 1, 0)\n";
  }

  // Old version, set the settling and the alternate shutter through notes
  if (m_iDMVersion < OLD_SETTLING_BROKEN) {
    sprintf(m_strTemp, 
      "SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", %g)\n", settling);
    mTD.strCommand += m_strTemp;
  }

  // 5/23/06: drift settling in Record set from DM was being applied, so set settling
  // unconditionally as long as settling is OK (not in Faux camera)
  if (m_iDMVersion >= NEW_SETTLING_OK && m_iDMSettlingOK[m_iCurrentCamera]) {
    sprintf(m_strTemp, "CM_SetSettling(acqParams, %g)\n", settling);
    mTD.strCommand += m_strTemp;
  }
  
  if (m_iDMVersion < OLD_SELECT_SHUTTER_BROKEN && shutter >= 0) {
    sprintf(m_strTemp, "SetPersistentNumberNote"
      "(\"MSC:Parameters:2:Alternate Shutter\", %d)\n", shutter);
    mTD.strCommand += m_strTemp;
  }

  if (m_iDMVersion >= NEW_SELECT_SHUTTER_OK && shutter >= 0) {
    sprintf(m_strTemp, "CM_SetShutterIndex(acqParams, %d)\n", shutter);
    mTD.strCommand += m_strTemp;
  }

  // Commands for K2 camera
  if (mTD.iReadMode >= 0) {
    if (GMS_SDK_VERSION < 31) {
      sprintf(m_strTemp, "K2_SetHardwareProcessing(camera, %d)\n", 
        mTD.iReadMode ? sK2HardProcs[mTD.iHardwareProc / 2] : 0);
    } else {
      sprintf(m_strTemp, "CM_SetHardwareCorrections(acqParams, %d)\n"
          "CM_SetDoAcquireStack(acqParams, %d)\n",
          mTD.iReadMode ? sCMHardCorrs[mTD.iHardwareProc / 2] : 0, m_bDoseFrac ? 1 : 0);
    }
    mTD.strCommand += m_strTemp;

    if (m_bDoseFrac) {

      if (GMS_SDK_VERSION < 31 || mTD.bUseOldAPI) {

        // WORKAROUND to bug in frame time, save & set the global frame time, restore after
        sprintf(m_strTemp, "Object k2dfa = alloc(K2_DoseFracAcquisition)\n"
          "k2dfa.DoseFrac_SetHardwareProcessing(%d)\n"
          "k2dfa.DoseFrac_SetAlignOption(%d)\n"
          "k2dfa.DoseFrac_SetFrameExposure(%f)\n"
          "Number savedFrameTime = K2_DoseFrac_GetFrameExposure(camera)\n"
          "K2_DoseFrac_SetFrameExposure(camera, %f)\n", 
          mTD.iReadMode ? sK2HardProcs[mTD.iHardwareProc / 2] : 0,
          mTD.bAlignFrames ? 1 : 0, mTD.dFrameTime, mTD.dFrameTime);
        mTD.strCommand += m_strTemp;
        if (mTD.bAlignFrames) {
          sprintf(m_strTemp, "k2dfa.DoseFrac_SetFilter(\"%s\")\n", mTD.strFilterName);
          mTD.strCommand += m_strTemp;
        }
      } else {

        // NEW API
        sprintf(m_strTemp, "CM_SetAlignmentFilter(acqParams, \"%s\")\n"
          "CM_SetFrameExposure(acqParams, %f)\n"
          "CM_SetStackFormat(acqParams, %d)\n",
          mTD.bAlignFrames ? mTD.strFilterName : "", mTD.dFrameTime,
          saveFrames == SAVE_FRAMES ? 0 : 1);
        mTD.strCommand += m_strTemp;
      }

      // Cancel the rotation/flip done by DM in GMS 2.3.1 rgardless of API
      if (mTD.iRotationFlip && GMS_SDK_VERSION >= 31) {
        sprintf(m_strTemp, "CM_SetAcqTranspose(acqParams, %d)\n", 
          sInverseTranspose[mTD.iRotationFlip]);
        mTD.strCommand += m_strTemp;
      }
    }

    sprintf(m_strTemp, "CM_SetReadMode(acqParams, %d)\n"
      "Number wait_time_s\n"
      "CM_PrepareCameraForAcquire(manager, camera, acqParams, NULL, wait_time_s)\n"
      "Sleep(wait_time_s)\n", sReadModes[mTD.iReadMode]);
    mTD.strCommand += m_strTemp;
  }

  // A read mode of -2 means set mode 1 for diffraction
  if (mTD.iReadMode == -2)
    mTD.strCommand += "CM_SetReadMode(acqParams, 1)\n";
  
  // Open shutter if a delay is set
  if (shutterDelay) {
    if (m_iDMVersion < NEW_OPEN_SHUTTER_OK)
      mTD.strCommand += "SSCOpenShutter()\n";
    else {
      // Specify the other shutter as being closed first; 1 means closed
      sprintf(m_strTemp, "CM_SetCurrentShutterState(camera, %d, 1)\n", 
        shutter > 0 ? 1 : 0);
      mTD.strCommand += m_strTemp;
      sprintf(m_strTemp, "CM_SetCurrentShutterState(camera, %d, 0)\n", 
        shutter > 0 ? 0 : 1);
      mTD.strCommand += m_strTemp;
    }
    sprintf(m_strTemp, "Delay(%d)\n", shutterDelay);
    mTD.strCommand += m_strTemp;
    // Probably unneeded
    /*
    if (m_iDMVersion < NEW_OPEN_SHUTTER_OK)
      mTD.strCommand += "SSCCloseShutter()\n";
    else
      mTD.strCommand += "CM_SetCurrentShutterState(camera, 1, 1)\n";
    */
  }

  // Get the image acquisition command
  if (m_iDMVersion < NEW_CAMERA_MANAGER) {
    switch (processing) {
    case UNPROCESSED:
      mTD.strCommand += "Image img := SSCUnprocessedBinnedAcquire";
      break;
    case DARK_SUBTRACTED:
      mTD.strCommand += "Image img := SSCDarkSubtractedBinnedAcquire";
      break;
    case GAIN_NORMALIZED:
      mTD.strCommand += "Image img := SSCGainNormalizedBinnedAcquire";
      break;
    case DARK_REFERENCE:
      mTD.strCommand += "Image img := SSCGetDarkReference";
      break;
    }

    sprintf(m_strTemp, "(%f, %d, %d, %d, %d, %d)\n", exposure, binning, top, left, 
      bottom, right);
    mTD.strCommand += m_strTemp;
  } else {
    if (processing == DARK_REFERENCE) {
      // This command has an inverted sense, 1 means keep shutter closed
      // Need to use this to get defect correction
      mTD.strCommand += "CM_SetShutterExposure(acqParams, 1)\n"
          "Image img := CM_AcquireImage(camera, acqParams)\n";
//      "Image img := CM_CreateImageForAcquire(camera, acqParams, \"temp\")\n"
//            "CM_AcquireDarkReference(camera, acqParams, img, NULL)\n";
    } else if (mTD.iReadMode >= 0 && m_bDoseFrac && 
      (GMS_SDK_VERSION < 31 || mTD.bUseOldAPI)) {
      mTD.strCommand += "Image stack\n"
        "Image img := k2dfa.DoseFrac_AcquireImage(camera, acqParams, stack)\n"
        "K2_DoseFrac_SetFrameExposure(camera, savedFrameTime)\n";
    } else {
      mTD.strCommand += "Image img := CM_AcquireImage(camera, acqParams)\n";
    }
  }
  
  // Restore drift settling to zero if it was set
  if (m_iDMVersion < OLD_SETTLING_BROKEN && settling > 0.)
    mTD.strCommand += "SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", 0.)\n";
  
  // Final calls to retain image and return its ID
  sprintf(m_strTemp, "KeepImage(img)\n"
    "number retval = GetImageID(img)\n");
  mTD.strCommand += m_strTemp;
  if (saveFrames == SAVE_FRAMES && (GMS_SDK_VERSION < 31 || mTD.bUseOldAPI)) {
    sprintf(m_strTemp, "KeepImage(stack)\n"
      "number stackID = GetImageID(stack)\n"
      //"Result(retval + \"  \" + stackID + \"\\n\")\n"
      "retval = retval + %d * stackID\n", ID_MULTIPLIER);
    mTD.strCommand += m_strTemp;
  } else if (m_bSaveFrames) {
    sprintf(m_strTemp, "Save set but %d %d   %s   %s\n", mTD.iReadMode, 
      m_bDoseFrac ? 1 : 0, mTD.strSaveDir.length() ? mTD.strSaveDir.c_str() : "NO DIR",
      mTD.strRootName.length() ? mTD.strRootName.c_str() : "NO ROOT");
    DebugToResult(m_strTemp);
  }
  sprintf(m_strTemp, "Exit(retval)");
  mTD.strCommand += m_strTemp;

  //sprintf(m_strTemp, "Calling AcquireAndTransferImage with divideBy2 %d\n", divideBy2);
  //DebugToResult(m_strTemp);
  int retval = AcquireAndTransferImage((void *)array, 2, arrSize, width, height,
    divideBy2, 0, DEL_IMAGE, saveFrames); 
  mTD.iAntialias = 0;

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
    mTD.strCommand.resize(0);
    AddCameraSelection();
    mTD.strCommand += "Number transpose = CM_Config_GetDefaultTranspose(camera)\n"
      "Exit(transpose)";
    double retval = ExecuteScript((char *)mTD.strCommand.c_str());
    if (retval != SCRIPT_ERROR_RETURN)
      transpose = (int)retval;
  }

  mTD.strCommand.resize(0);
  AddCameraSelection();
  sprintf(m_strTemp, "Image img := SSCGetGainReference(%d)\n"
    "KeepImage(img)\n"
    "number retval = GetImageID(img)\n"
    "Exit(retval)", binning);
  mTD.strCommand += m_strTemp;
  mTD.bDoContinuous = false;
  mTD.iAntialias = 0;
  retval = AcquireAndTransferImage((void *)array, 4, arrSize, width, height, 0, transpose, 
    DEL_IMAGE, NO_SAVE);
  if (transpose & 256) {
    tmp = *width;
    *width = *height;
    *height = tmp;
  }
  return retval;
}

#define SET_ERROR(a) {if (doingStack) \
        td->iErrorFromSave = a; \
      if (!doingStack || numLoop == 1) \
        errorRet = a; \
        continue;}

/*
 * Common routine for executing the current command script, getting an image ID back from
 * it, and copying the image into the supplied array with various transformations
 */
int TemplatePlugIn::AcquireAndTransferImage(void *array, int dataSize, long *arrSize, 
                      long *width, long *height, long divideBy2, long transpose, 
                      long delImage, long saveFrames)
{
  DWORD retval = 0, resret, threadID;
  ThreadData *retTD = &mTD;
  int retWidth = retTD->width, retHeight = retTD->height;

  // If there was an acquire thread started, wait until it is done for any dose frac
  // shot, or until ready for acquisition for other shots
  if (m_HAcquireThread) {
    retval = WaitForAcquireThread(sContinuousArray ? WAIT_FOR_CONTINUOUS : (
      m_bDoseFrac ? WAIT_FOR_THREAD : WAIT_FOR_NEW_SHOT));
    if (!SleepMsg(10))
      retval = 1;
  }

  int sizeOrig = *arrSize;
  mTD.array = array;
  mTD.dataSize = dataSize;
  mTD.arrSize = *arrSize;
  mTD.width = *width;
  mTD.height = *height;
  mTD.divideBy2 = divideBy2;
  mTD.transpose = transpose;
  mTD.delImage = delImage;
  mTD.saveFrames = saveFrames;
  mTD.iReadyToAcquire = 0;
  mTD.iReadyToReturn = 0;
  mTD.iDMquitting = 0;
  mTD.iErrorFromSave = 0;
  mTD.iEndContinuous = 0;
  mTD.iWaitingForFrame = 0;

  // Get the array for storing continuous images 
  if (!retval && mTD.bDoContinuous) {
    try {
      sContinuousArray = new short [sizeOrig];
      memset(sContinuousArray, 0, 2 * sizeOrig);
    }
    catch (...) {
      sContinuousArray = NULL;
      DebugToResult("AcquireAndTransferImage: error making sContinuousArray\n");
      return ROTBUF_MEMORY_ERROR;
    }
  }

  // A thread is needed if doing asynchronous saving, or even for synchronous save with
  // an early return, but only for old version
  if (!retval && (mTD.bDoContinuous || 
    (saveFrames && (mTD.bAsyncSave || (mTD.bEarlyReturn && GMS_SDK_VERSION < 31))))) {
      *arrSize = *width = *height = 0;
      mTDcopy = mTD;
      retTD = &mTDcopy;
      m_HAcquireThread = CreateThread(NULL, 0, AcquireProc, &mTDcopy, CREATE_SUSPENDED, 
        &threadID);
      if (!m_HAcquireThread) {
        retval = THREAD_ERROR;
        DebugToResult("AcquireAndTransferImage: error starting thread\n");
      }
      if (!retval) {
        if (mTD.bDoContinuous)
          SetThreadPriority(m_HAcquireThread, THREAD_PRIORITY_ABOVE_NORMAL);
        resret = ResumeThread(m_HAcquireThread);
        if (resret == (DWORD)(-1) || resret > 1)
          retval = THREAD_ERROR;
      }
      if (retval) {
        DELETE_CONTINUOUS;
        return retval;
      }
      DebugToResult(mTD.bDoContinuous ? "Started continuous thread, returning\n" : 
        "Started thread, going into wait loop\n");
      if (mTD.bDoContinuous)
        return 0;
      retval = WaitForAcquireThread(mTD.bEarlyReturn ? WAIT_FOR_RETURN : WAIT_FOR_THREAD);
      mTD.iErrorFromSave = mTDcopy.iErrorFromSave;
      mTD.iFramesSaved = mTDcopy.iFramesSaved;
      retWidth = mTDcopy.iAntialias ? retTD->iFinalWidth : retTD->width;
      retHeight = mTDcopy.iAntialias ? retTD->iFinalHeight : retTD->height;
      sprintf(m_strTemp, "Back from thread, retval %d errfs %d  #saved %d w %d h %d\n",
        retval, mTD.iErrorFromSave, mTD.iFramesSaved, retWidth, retHeight);
      DebugToResult(m_strTemp);
      if (!retval && !retTD->arrSize && retTD->iNumFramesToSum)
        retTD->arrSize = retWidth * retHeight;
  } else if (!retval) {
    retval = AcquireProc(&mTD);
    retWidth = mTD.iAntialias ? mTD.iFinalWidth : mTD.width;
    retHeight = mTD.iAntialias ? mTD.iFinalHeight : mTD.height;
    /*sprintf(m_strTemp, "Back from call, retval %d w %d h %d  arrsize %d\n",
        retval, retWidth, retHeight, mTD.arrSize);
    DebugToResult(m_strTemp);*/
  }
  *width = retWidth;
  *height = retHeight;
  if (!retval && !retTD->arrSize)
    *arrSize = B3DMIN(1024, sizeOrig);
  else if (!retval && retTD->iAntialias)
    *arrSize = retWidth * retHeight;
  else
    *arrSize = retTD->arrSize;
  return (int)retval;
}

/*
 * Wait for either an acquire thread to be done, a sum to be done, or for DM to be ready
 * for a non-dose-frac shot
 */
DWORD TemplatePlugIn::WaitForAcquireThread(int waitType)
{
  const char *messages[] = {"thread to end", "exposure and frame sum to complete",
    "ready for single-shot", "continuous exposure to end"};
  double quitStart = -1.;
  double waitStart = GetTickCount();
  DWORD retval;
  sprintf(m_strTemp, "Waiting for %s  %d\n", messages[waitType], 
    waitType == WAIT_FOR_CONTINUOUS ? sJ : 0);
  DebugToResult(m_strTemp);
  while (1) {
    GetExitCodeThread(m_HAcquireThread, &retval);
    if (retval != STILL_ACTIVE) {
      CloseHandle(m_HAcquireThread);   // HOPE THAT IS RIGHT TO ADD!
      m_HAcquireThread = NULL;
      return retval;
    }
    if ((waitType == WAIT_FOR_RETURN && GetWatchedDataValue(mTDcopy.iReadyToReturn)) ||
      (waitType == WAIT_FOR_NEW_SHOT && GetWatchedDataValue(mTDcopy.iReadyToAcquire)))
      return 0;
    if (waitType == WAIT_FOR_CONTINUOUS && TickInterval(waitStart) > 5000.) {
      /*sprintf(m_strTemp, "Giving up on thread ending, counter %d  time %.3f\n", sJ,
        (GetTickCount() % (DWORD)3600000) / 1000.);
      ErrorToResult(m_strTemp, "INfo: ");*/
      ErrorToResult("The SerialEM continuous acquire thread did not end yet.  "
        "This may be OK,\n  or you may have to restart DigitalMicrograph if you can't "
        "take any more images", "Warning: ");
      return 1;
    }
    if (!SleepMsg(10)) {
      quitStart = GetTickCount();
      SetWatchedDataValue(mTDcopy.iDMquitting, 1);
    }
    if (quitStart >= 0. && TickInterval(quitStart) > 5000.)
      return QUIT_DURING_SAVE;
  }
  return 0;
}

/*
 * The actual procedure for acquiring and processing image
 */
static DWORD WINAPI AcquireProc(LPVOID pParam)
{
  ThreadData *td = (ThreadData *)pParam;
  long ID, imageID, stackID, frameDivide = td->divideBy2;
  void *array = td->array;
  void *unbinnedArray = NULL;
  int outLimit = td->arrSize;
  long divideBy2 = td->divideBy2;
  long transpose = td->transpose;
  long delImage = td->delImage;
  long saveFrames = td->saveFrames;
  DM::Image image, sumImage;
  int i, j, numDim, loop, numLoop = 1;
  GatanPlugIn::ImageDataLocker *imageLp = NULL;
#if defined(GMS2) && GMS_SDK_VERSION >= 30
  ImageDataPlugin::image_data_t fData;
#else
  ImageData::image_data_t fData;
#endif
  std::string filter = td->bAlignFrames ? td->strFilterName : "";
  void *imageData;
  short *outForRot;
  short *procOut;
  bool doingStack, needProc, needTemp, needSum, exposureDone;
  bool stackAllReady, doingAsyncSave, frameNeedsDelete = false;
  double retval, procWall, saveWall, getFrameWall, wallStart;
  int fileSlice, tmin, tmax, numSlices, nxout, nyout, grabInd, grabSlice;
  float meanSum, scaleSave = td->fFloatScaling;
  int slice = 0;
  int errorRet = 0;
#ifdef _WIN64
  DM::ScriptObject dummyObj;
#if GMS_SDK_VERSION < 31
  K2_DoseFracAcquisition *k2dfaP = NULL;
#else
  CM::ImageStackPtr stack;
#endif
#endif

  // Set these values to zero in case of error returns
  td->width = 0;
  td->height = 0;
  td->iNumSummed = 0;
  td->iFramesSaved = 0;
  doingAsyncSave = saveFrames && td->bAsyncSave;
  exposureDone = !doingAsyncSave;
  needTemp = saveFrames && td->bEarlyReturn;
  needSum = saveFrames && td->iNumFramesToSum != 0 && 
    ((GMS_SDK_VERSION >= 31 && !td->bUseOldAPI) ||
    ((GMS_SDK_VERSION < 31 || td->bUseOldAPI) && td->bEarlyReturn)); 
  td->iifile = NULL;
  td->fp = NULL;
  td->rotBuf = td->tempBuf = NULL;
  td->sumBuf = NULL;
  td->outSumBuf = NULL;
  td->numAddedToOutSum = 0;
  td->outSumFrameIndex = 0;
  td->numOutSumsDoneAtIndex = 0;

  // Get array for unbinned image with antialias reduction
  if (td->iAntialias) {
    try {
      i = sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE ? 4 : 1;
      outLimit = (td->iRight - td->iLeft) * (td->iBottom - td->iTop) * i;
      unbinnedArray = new short[outLimit];
      array = unbinnedArray;
    }
    catch (...) {
      return ROTBUF_MEMORY_ERROR;
    }
  }

  // Execute the command string as developed
  if (td->strCommand.length() > 0)
    retval = ExecuteScript((char *)td->strCommand.c_str());
  else
    retval = td->iDSimageID;
  td->dLastReturnTime = GetTickCount();
  
  // If error, return error code
  if (retval == SCRIPT_ERROR_RETURN) {
    td->arrSize = 0;
    delete unbinnedArray;
    return GENERAL_SCRIPT_ERROR;
  }
  if (td->bDoContinuous)
    return RunContinuousAcquire(td);

  td->arrSize = 0;

  // Get the image ID for simple cases
  ID = imageID = (long)(retval + 0.01);

  // Do asynchronous save by old and new API
  if (doingAsyncSave) {
#ifdef _WIN64
    try {

      // If doing asynchronous save, now have to do the parameter setup and acquisition
      j = 0;
      CM::CameraPtr camera = CM::GetCurrentCamera();  j++;
      CM::CameraManagerPtr manager = CM::GetCameraManager();  j++;
      CM::AcquisitionParametersPtr acqParams = CM::CreateAcquisitionParameters_FullCCD(camera,
        (CM::AcquisitionProcessing)td->iK2Processing, td->dK2Exposure + 0.001, td->iK2Binning, 
        td->iK2Binning);//, td->iK2Top, td->iK2Left, td->iK2Bottom, td->iK2Right);
      j++;
#if GMS_SDK_VERSION < 31
      k2dfaP = new K2_DoseFracAcquisition;
      k2dfaP->SetFrameExposure(td->dFrameTime);  j++;
      k2dfaP->SetAlignOption(td->bAlignFrames);  j++;
      k2dfaP->SetHardwareProcessing(td->iReadMode ? sK2HardProcs[td->iHardwareProc / 2] : 0);
      j++;
      k2dfaP->SetAsyncOption(true);  j++;
      if (td->bAlignFrames)
        k2dfaP->SetFilter(filter);
#else
      CM::SetHardwareCorrections(acqParams, CM::CCD::Corrections::from_bits(
        td->iReadMode ? sCMHardCorrs[td->iHardwareProc / 2] : 0));  j++;
      CM::SetAlignmentFilter(acqParams, filter);  j++;
      CM::SetFrameExposure(acqParams, td->dFrameTime);  j++;
      CM::SetDoAcquireStack(acqParams, 1);  j++;
      CM::SetStackFormat(acqParams, CM::StackFormat::Series);  j++;
      CM::SetDoAsyncReadout(acqParams, td->bAsyncToRAM ? 1 : 0);   j++;
      i = sInverseTranspose[td->iRotationFlip];
      CM::SetAcqTranspose(acqParams, (i & 1) != 0, (i & 2) != 0, (i & 256) != 0);  
#endif

      j = 10;
      CM::SetSettling(acqParams, td->dK2Settling);  j++;
      CM::SetShutterIndex(acqParams, td->iK2Shutter);  j++;
      CM::SetReadMode(acqParams, sReadModes[td->iReadMode]);  j++;
      CM::PrepareCameraForAcquire(manager, camera, acqParams, dummyObj, retval);  j++;
      if (retval >= 0.001)
        Sleep((DWORD)(retval * 1000. + 0.5));

#if GMS_SDK_VERSION < 31
      sumImage = k2dfaP->AcquireImage(camera, acqParams, image);
      numLoop = 2;
#else
      stack = CM::AcquireImageStack(camera, acqParams);   j++;
      numSlices = stack->GetNumFrames();
      stackAllReady = true;
      td->iNumFramesToSum = B3DMIN(td->iNumFramesToSum, numSlices);
#endif
      DebugToResult("Returned from asynchronous start of acquire\n");
    }
    catch (exception exc) {
      sprintf(td->strTemp, "Caught an exception from call %d to start dose frac exposure:"
        "\n  %s\n", j, exc.what());
      ErrorToResult(td->strTemp);
      try {
#if GMS_SDK_VERSION < 31
        k2dfaP->Abort();
        DM::DeleteImage(sumImage.get());
        DM::DeleteImage(image.get());
        delete k2dfaP;
#else
        stack->Abort();
        // TODO: delete anything?
#endif
      }
      catch (exception exc) {
      }
      delete unbinnedArray;
      return GENERAL_SCRIPT_ERROR;
    }
#endif
  } else if (saveFrames) {

    // Synchronous dose-fractionation and saving: the stack is complete
    if (GMS_SDK_VERSION < 31 || td->bUseOldAPI) {

      // Old way: stack and image exist
      stackID = (int)((retval + 0.1) / ID_MULTIPLIER);
      imageID = B3DNINT(retval - (double)stackID * ID_MULTIPLIER);
      if (stackID) {
        numLoop = 2;
        ID = stackID;
      } else {
        saveFrames = 0;
        td->iErrorFromSave = NO_STACK_ID;
      }
      sprintf(td->strTemp, "Image ID %d  stack ID %d\n", imageID, stackID);
      DebugToResult(td->strTemp);
    } else {

      // New way, just a stack is created, set up to use its ID first time
      ID = imageID;
    }
  }

  // Loop on stack then image if there are both
  for (loop = 0; loop < numLoop; loop++) {
    doingStack = saveFrames && !loop;
    delete imageLp;
    imageLp = NULL;

    // Second time through (old GMS), substitute the sum image or its ID
    if (loop) {
      if (doingAsyncSave) 
        image = sumImage;
      else
        ID = imageID;
    }
    try {

      if (!doingAsyncSave && !DM::GetImageFromID(image, ID)) {
        sprintf(td->strTemp, "Image not found from ID %d\n", ID);
        ErrorToResult(td->strTemp);
        SET_ERROR(IMAGE_NOT_FOUND);
      }

      if (!doingStack && !(saveFrames && (needSum || td->bEarlyReturn))) {
        j = 20;

       // REGULAR OLD IMAGE

       // Sets byteSize, isInteger, isUnsignedInt, isFloat, signedBytes and width/height
       if (GetTypeAndSizeInfo(td, image, loop, outLimit, doingStack))
          SET_ERROR(WRONG_DATA_TYPE);

        // Get data pointer and transfer the data
        j++;
        imageLp = new GatanPlugIn::ImageDataLocker( image );   j++;
        numDim = DM::ImageGetNumDimensions(image.get());   j++;
        if (numDim != 2) {
          sprintf(td->strTemp, "image with ID %d has %d dimensions!\n", ID, numDim);
          DebugToResult(td->strTemp);
        }
        imageData = imageLp->get();   j++;
        ProcessImage(imageData, array, td->dataSize, td->width, td->height, divideBy2, 
          transpose, td->byteSize, td->isInteger, td->isUnsignedInt, td->fFloatScaling);
        AntialiasReduction(td, array);
        delete imageLp;
        imageLp = NULL;

      } else if (doingStack) {
        j = 30;

        // STACK PROCESSING : check dimensions for an image

        if (GMS_SDK_VERSION < 31 || !td->bAsyncSave) {
          numDim = DM::ImageGetNumDimensions(image.get());   j++;
          if (numDim < 3) {
            td->iErrorFromSave = STACK_NOT_3D;
            continue;
          }
          stackAllReady = !td->bAsyncSave;
          numSlices = td->bAsyncSave ? 0 : DM::ImageGetDimensionSize(image.get(), 2);   j++;
        }

        procWall = saveWall = getFrameWall = 0.;
        do {
#ifdef _WIN64

          // If doing asynchronous save, wait until a slice is ready to process
          if (td->bAsyncSave) {
            wallStart = wallTime();
            while (1) {
#if GMS_SDK_VERSION < 31
              stackAllReady = k2dfaP->IsDone();   j++;
              numSlices = k2dfaP->GetNumFramesProcessed();   j++;
              if (numSlices > slice || stackAllReady) {
                exposureDone = true;
                break;
              }
#else
              image = stack->GetNextFrame(&exposureDone);   j++;
              frameNeedsDelete = image.IsValid();
              if (frameNeedsDelete) {
                double elapsed = 0.;
                bool elapsedRet = stack->GetElapsedExposureTime(elapsed);
                sprintf(td->strTemp, "Got frame %d of %d   exp done %d  "
                  "elapsed %s %.2f\n", slice + 1, numSlices, exposureDone ? 1:0, 
                  elapsedRet ? "T" : "F", elapsed);
                DebugToResult(td->strTemp);
              }
              if (frameNeedsDelete || slice >= numSlices)
                break;
#endif

              // Sleep while processing events.  If it returns false, it is a quit,
              // so break and let the loop finish
              if (!SleepMsg(10) || GetWatchedDataValue(td->iDMquitting)) {
                td->iErrorFromSave = QUIT_DURING_SAVE;
                SetWatchedDataValue(td->iDMquitting, 1);
                break;
              }
            }
            sprintf(td->strTemp, "numSlices %d  isStackDone %s\n", numSlices,
              stackAllReady ? "Y":"N");
            if (GMS_SDK_VERSION < 31)
              DebugToResult(td->strTemp);
            if (slice >= numSlices || GetWatchedDataValue(td->iDMquitting) || 
              td->iErrorFromSave == QUIT_DURING_SAVE || 
              td->iErrorFromSave == DM_CALL_EXCEPTION)
              break;
          }
          if (slice)
            AccumulateWallTime(getFrameWall, wallStart);
          if (exposureDone && !td->iReadyToReturn && 
            td->iNumSummed >= td->iNumFramesToSum)       
            SetWatchedDataValue(td->iReadyToReturn, 1);
#endif

          if (!slice) {

            // First slice initialization and checks
            // Sets byteSize, isInteger, isUnsignedInt, isFloat, signedBytes and width/height
            if (GetTypeAndSizeInfo(td, image, loop, outLimit, doingStack)) {
              td->iErrorFromSave = WRONG_DATA_TYPE;
              break;
            }
            j++;

            // Sets frameDivide, outByteSize, fileMode, save4bit and copies gain reference
            if (InitOnFirstFrame(td, needTemp, needSum, frameDivide, needProc))
              break;
            if (!needTemp)
              td->tempBuf = (short *)array;
            if (GMS_SDK_VERSION < 31)
              SetWatchedDataValue(td->iReadyToAcquire, 1);
          }
          wallStart = wallTime();

          // Get data pointer
          imageLp = new GatanPlugIn::ImageDataLocker( image );   j++;
          if (GMS_SDK_VERSION < 31 || !td->bAsyncSave) {
            imageLp->GetImageData(2, slice, fData);   j++;
            imageData = fData.get_data();   j++;
          } else {
            imageData = imageLp->get();   j++;
          }
          td->outData = (short *)imageData;
          outForRot = (short *)td->tempBuf;

          // Add to sum if needed in sum, and process it now if it is done
          if (needSum && slice < td->iNumFramesToSum) {
            AddToSum(td, imageData, td->sumBuf);
            if (td->bEarlyReturn && slice == td->iNumFramesToSum - 1) {
                ProcessImage(td->sumBuf, array, td->dataSize, td->width, td->height,
                  divideBy2, transpose, 4, !td->isFloat, false, scaleSave);
                DebugToResult("Partial sum completed by thread\n");
                AntialiasReduction(td, array);
            }
            td->iNumSummed++;
            if (exposureDone && !td->iReadyToReturn && 
              td->iNumSummed >= td->iNumFramesToSum)
                SetWatchedDataValue(td->iReadyToReturn, 1);
          }

          // If grabbing frames at this point, allocate the frame, copy over if not proc
          procOut = td->tempBuf;
          grabInd = slice - (numSlices - td->iNumGrabAndStack);
          if (grabInd >= 0) {
            try {
              td->grabStack[grabInd] = new char[td->width * td->height * td->outByteSize];
            }
            catch (...) {
              td->grabStack[grabInd] = NULL;
              td->iErrorFromSave = ROTBUF_MEMORY_ERROR;
              break;
            }
            procOut = (short *)td->grabStack[grabInd];
            if (!needProc)
              memcpy(procOut, imageData, td->width * td->height * td->outByteSize);
          }

          // Process a float image (from software normalized frames)
          // It goes from the DM array into the passed or temp array or grab stack
          if (needProc) {
            ProcessImage(imageData, procOut, td->dataSize, td->width, td->height, 
              frameDivide, transpose, td->byteSize, td->isInteger, td->isUnsignedInt,
              td->fFloatScaling);
            td->outData = (short *)td->tempBuf;
            outForRot = td->rotBuf;
          }

          // If just grabbing, set slice number first time, skip other steps
          if (grabInd >= 0) {
            if (!grabInd) {
              grabSlice = slice;
              DebugToResult("Starting to grab frames from rest of stack\n");
            }
          } else {

            // Rotate and flip if desired and change the array pointer to save to use the 
            // passed array or the rotation array
            if (td->iFrameRotFlip || !td->bWriteTiff) {
              RotateFlip(td->outData, td->fileMode, td->width, td->height, 
                td->iFrameRotFlip, !td->bWriteTiff, outForRot, &nxout, &nyout);
              td->outData = outForRot;
            } else {
              nxout = td->width;
              nyout = td->height;
            }
            AccumulateWallTime(procWall, wallStart);

            // Save the image to file; keep going on error if need a sum
            i = PackAndSaveImage(td, td->tempBuf, nxout, nyout, slice, 
              stackAllReady && slice == numSlices - 1, fileSlice, tmin, tmax, meanSum);
            if (i && !(needSum && slice < td->iNumFramesToSum))
              break;

            AccumulateWallTime(saveWall, wallStart);
          }

          // Increment slice, clean up image at end of slice loop
          slice++;
          delete imageLp;
          imageLp = NULL;
          DeleteImageIfNeeded(td, image, &frameNeedsDelete);
        } while (slice < numSlices || !stackAllReady);  // End of slice loop

        DeleteImageIfNeeded(td, image, &frameNeedsDelete);

        // If there are stacked frames, rotate/flip and save them
        if (!td->iErrorFromSave && td->iNumGrabAndStack) {

          // Signal that we are done with the DM stack and then process
          DebugToResult("Done with stack, processing grabbed frames\n");
          SetWatchedDataValue(td->iReadyToAcquire, 1);
          for (grabInd = 0; grabInd < td->iNumGrabAndStack; grabInd++) {
            td->outData = (short *)td->grabStack[grabInd];
            if (td->iFrameRotFlip || !td->bWriteTiff) {
              RotateFlip(td->outData, td->fileMode, td->width, td->height, 
                td->iFrameRotFlip, !td->bWriteTiff, td->tempBuf, &nxout, &nyout);
              td->outData = (short *)td->tempBuf;
              AccumulateWallTime(procWall, wallStart);
            } else {
              nxout = td->width;
              nyout = td->height;
            }
            i = PackAndSaveImage(td, td->tempBuf, nxout, nyout, grabSlice + grabInd, 
               grabInd == td->iNumGrabAndStack - 1, fileSlice, tmin, tmax, meanSum);
            if (i)
              break;
            delete [] td->grabStack[grabInd];
            td->grabStack[grabInd] = NULL;
            AccumulateWallTime(saveWall, wallStart);
          }
        }

        sprintf(td->strTemp, "Processing %.3f   saving %.3f   getting frame  %.3f sec\n",
          procWall, saveWall, getFrameWall);
        if (!td->iErrorFromSave)
          DebugToResult(td->strTemp);
      }
    }
    catch (exception exc) {
      sprintf(td->strTemp, "Caught an exception from call %d to a DM:: function:\n  %s\n", 
        j, exc.what());
      ErrorToResult(td->strTemp);
      SET_ERROR(DM_CALL_EXCEPTION);
    }
    delete imageLp;
    imageLp = NULL;
    DeleteImageIfNeeded(td, image, &frameNeedsDelete);
    td->fFloatScaling = scaleSave;

    // Return the full sum here after all temporary use of array is over
    if (needSum && !td->bEarlyReturn) {
      ProcessImage(td->sumBuf, array, td->dataSize, td->width, td->height,
        divideBy2, transpose, 4, !td->isFloat, false, scaleSave);
      AntialiasReduction(td, array);
    }

#ifdef _WIN64
    // Delete image here in asynchronous case
    if (doingAsyncSave) {
      try {
#if GMS_SDK_VERSION < 31
        if (doingStack && !stackAllReady)
          k2dfaP->Abort();
        DM::DeleteImage(image.get());
#else
        if (slice < numSlices)
          stack->Abort();
#endif
      }
      catch (exception exc) {
      }
    }
#endif
  }  // End of loop on stack and sum or single image

  // Clean up files now in case an exception got us to here
  if (td->iifile)
    iiDelete(td->iifile);
  td->iifile = NULL;
  if (td->fp)
    fclose(td->fp);
  td->fp = NULL;

  // Clean up all buffers
  delete [] td->rotBuf;
  delete [] td->sumBuf;
  delete [] td->outSumBuf;
  if (needTemp)
    delete [] td->tempBuf;
  if (td->iNumGrabAndStack) {
    if (td->grabStack)
      for (grabInd = 0; grabInd < td->iNumGrabAndStack; grabInd++)
        delete [] td->grabStack[grabInd];
    delete [] td->grabStack;
  }

  // Delete image(s) before return if they can be found
  if (delImage && !doingAsyncSave) {
    if (DM::GetImageFromID(image, imageID))
      DM::DeleteImage(image.get());
    else 
      DebugToResult("Cannot find image for deleting it\n");
    if (saveFrames && (GMS_SDK_VERSION < 31 || td->bUseOldAPI)) {
      if (DM::GetImageFromID(image, stackID))
        DM::DeleteImage(image.get());
      else
        DebugToResult("Cannot find stack for deleting it\n");
    }
  }
#if defined(_WIN64) && GMS_SDK_VERSION < 31
  delete k2dfaP;
#endif

  delete unbinnedArray;
  td->arrSize = td->iAntialias ? td->iFinalWidth * td->iFinalHeight :
    td->width * td->height;
  sprintf(td->strTemp, "Leaving thread with return value %d arrSize %d\n", errorRet, 
    td->arrSize); 
  DebugToResult(td->strTemp);
  return errorRet;
}

static void AntialiasReduction(ThreadData *td, void *array)
{
#ifdef _WIN64
  int error, i, xStart, yStart, nxr, nyr;
  int type = td->divideBy2 ? SLICE_MODE_SHORT : SLICE_MODE_USHORT;
  if (!td->iAntialias)
    return;
  double filtScale = 1. / td->iFinalBinning;
  sprintf(td->strTemp, "AntialiasReduction from %d x %d bin %d to %d x %d\n",
    td->width, td->height, td->iFinalBinning, td->iFinalWidth, td->iFinalHeight);
  DebugToResult(td->strTemp);
  unsigned char **linePtrs = makeLinePointers(array, td->width, td->height, 2);

  // Set the offset to center a subset, get filter, and do it
  float axoff = (float)(B3DMAX(0, td->width - td->iFinalWidth * td->iFinalBinning) / 2.);
  float ayoff = (float)(B3DMAX(0, td->height - td->iFinalHeight * td->iFinalBinning) /2.);
  int filterType = B3DMIN(6, td->iAntialias) - 1;
  error = selectZoomFilter(filterType, filtScale, &i);
  if (!error && linePtrs) {
    setZoomValueScaling((float)(td->iFinalBinning * td->iFinalBinning));
    error = zoomWithFilter(linePtrs, td->width, td->height, axoff, ayoff, 
      td->iFinalWidth, td->iFinalHeight, td->iFinalWidth, 0, type,
      td->array, NULL, NULL);
  }
  B3DFREE(linePtrs);

  // It is inconvenient to have errors, so fall back to binning
  if (error) {
    xStart = B3DNINT(axoff);
    yStart = B3DNINT(ayoff);
    extractWithBinning(array, type, td->width, xStart, td->width - xStart - 1, yStart,
      td->height - yStart - 1, td->iFinalBinning, td->array, 0, &nxr, &nyr);
    ErrorToResult("Warning: an error occurred in antialias reduction, binning was used "
      "instead\n", "\nA problem occurred acquiring an image for SerialEM:\n");
  }
#endif
}

//////////////////////////////////////////////////////////////////
//  CONTINUOUS ACQUIRE ROUTINES
//////////////////////////////////////////////////////////////////

// NOTE: to debug continuous acquire, it is essential to avoid chronic debug output 
// because each output takes ~80 msec, at least on a slow system.  Thus, uncomment some of
// ErrorToResult calls to debug this without having debug mode on.  Another alternative
// is to suppress all the debug output from ProcessImage

/*
 * Start continuous acquire from thread and process images into static buffer
 */
static int RunContinuousAcquire(ThreadData *td)
{
  bool K2type = td->iReadMode >= 0;
  bool startedAcquire = false, madeImage = false;
  int outLimit = td->arrSize;
  int j, transpose, nxout, nyout, maxTime = 0, retval = 0, rotationFlip = 0;
  double delay;
  short *procOutArray = sContinuousArray;
  DM::Image image;
  DM::ScriptObject acqListen, dummyObj;
  GatanPlugIn::ImageDataLocker *imageLp = NULL;
#ifdef GMS2
  CM::AcquisitionPtr acqObj;
  Camera::FrameSetInfoPtr frameInfo;
  Imaging::Transpose2d dfltTrans2d;
  Imaging::Transpose2d zeroTrans2d = 
  Imaging::Transpose2d::RotateNoneFlipNone;
  CM::CameraPtr camera;
  CM::CameraManagerPtr manager;
  CM::AcquisitionParametersPtr acqParams;
#else
  ImageData::Transform2D dfltTrans2d;
  ImageData::Transform2D zeroTrans2d = (ImageData::Transform2D)0x0000;
  CM::Camera camera;
  CM::CameraManager manager;
  CM::AcquisitionParameters acqParams;
#endif
  void *imageData;
  td->arrSize = 0;

  try {

    // Set up the acquisition parameters
    j = 0;
    camera = CM::GetCurrentCamera();  j++;
    manager = CM::GetCameraManager();  j++;
    acqParams = CM::CreateAcquisitionParameters_FullCCD(
      camera, (CM::AcquisitionProcessing)td->iK2Processing, 
      td->dK2Exposure + (K2type ? 0.001 : 0.), td->iK2Binning,  td->iK2Binning); j++;
    CM::SetBinnedReadArea(camera, acqParams, td->iTop, td->iLeft, td->iBottom,td->iRight);
    j++;
    CM::SetSettling(acqParams, td->dK2Settling);  j++;
    CM::SetShutterIndex(acqParams, td->iK2Shutter);  j++;
    if (td->iCorrections >= 0)
      CM::SetCorrections(acqParams, 0x1000 + 49, td->iCorrections); 
    j++;
    CM::SetDoContinuousReadout(acqParams, td->bSetContinuousMode); j++;

    // Find out if there is a transpose; cancel it and set up for rotation/flip
    dfltTrans2d = CM::Config_GetDefaultTranspose(camera); j++;
#ifdef GMS2
    transpose = dfltTrans2d.ToBits();
#else
    transpose = (int)dfltTrans2d;
#endif
    if (transpose) {
      
      // The inverse is the same except for 257/258, which are inverses of each other
      if ((transpose + 1) / 2 == 129)
        transpose = 515 - transpose;

      // Set the inverse transpose and look up needed rotation/flip
      CM::SetTotalTranspose(camera, acqParams, zeroTrans2d);
      //CM::SetAcqTranspose(acqParams, zeroTrans2d);
      for (rotationFlip = 0; rotationFlip < 8; rotationFlip++)
        if (sInverseTranspose[rotationFlip] == transpose)
          break;

      try {
        procOutArray = new short[outLimit];
      }
      catch (...) {
        DELETE_CONTINUOUS;
        return ROTBUF_MEMORY_ERROR;
      }
    }
    sprintf(td->strTemp, "Default transpose %d  rotationFlip %d\n", 
      transpose, rotationFlip);
    DebugToResult(td->strTemp);
    j++;

    // Subtract 1 here; they are numbered from 0 in the script call
    if (td->iContinuousQuality > 0)
      CM::SetQualityLevel(acqParams, td->iContinuousQuality - 1);
    j++;

    // Set K2 parameters
    // Seems to be no way to set hardware correction in software for older GMS
    if (K2type) {
#ifdef _WIN64
#if GMS_SDK_VERSION < 31
      K2_SetHardwareProcessing(camera, 
        td->iReadMode ? sK2HardProcs[td->iHardwareProc / 2] : 0);
#else
      CM::SetHardwareCorrections(acqParams, CM::CCD::Corrections::from_bits(
        td->iReadMode ? sCMHardCorrs[td->iHardwareProc / 2] : 0));
#endif
      j++;
      CM::SetReadMode(acqParams, sReadModes[td->iReadMode]);
#endif
    }

    // Set diffraction mode for OneView
    if (td->iReadMode == -2)
      CM::SetReadMode(acqParams, 1);

    // Get the DM image for this acquisition
    j = 13;
    image = CM::CreateImageForAcquire(camera, acqParams, "dummy"); j++;
    madeImage = true;

#ifdef GMS2
    // Get the acquisition object if using one
    if (td->bUseAcquisitionObj)
      acqObj = CM::CreateAcquisition(camera, acqParams);
#endif

    // Prepare K2 at least
    j++;
    if (K2type) {
      CM::PrepareCameraForAcquire(manager, camera, acqParams, dummyObj, delay);
      if (delay >= 0.001)
        Sleep((DWORD)(delay * 1000. + 0.5));
    }

    j++;
#ifdef GMS2
    if (td->bUseAcquisitionObj)
      CM::ACQ_StartAcquire(acqObj); 
#endif
    //ErrorToResult("Initiated continuous acquisition\n", "INfo: ");

    // Start the loop
    startedAcquire = true;
    while (1) {
      DWORD startTime = GetTickCount();
      j = 17;
      sJ = 1;
#ifdef GMS2
      if (td->bUseAcquisitionObj)
        CM::DoAcquire_LL(acqObj, acqListen, image, frameInfo);
      else
#endif
        CM::AcquireImage(camera, acqParams, image);
      j++; sJ++;
      if (GetTypeAndSizeInfo(td, image, 0, outLimit, false)) {
        retval = WRONG_DATA_TYPE;
        break;
      }
      sJ++;

      // Get data pointer and transfer the data
      imageLp = new GatanPlugIn::ImageDataLocker( image ); j++; sJ++;
      imageData = imageLp->get(); j++; sJ++;
      if (!rotationFlip)
        WaitForSingleObject(sImageMutexHandle, IMAGE_MUTEX_WAIT);
      sJ++;
      ProcessImage(imageData, procOutArray, td->dataSize, td->width, td->height, 
        td->divideBy2, td->transpose, td->byteSize, td->isInteger, td->isUnsignedInt, 
        td->fFloatScaling); sJ++;
      delete imageLp; sJ++;
      imageLp = NULL; sJ++;
      if (rotationFlip) {
        WaitForSingleObject(sImageMutexHandle, IMAGE_MUTEX_WAIT);
        sJ++;
        RotateFlip(procOutArray, MRC_MODE_SHORT, td->width, td->height, rotationFlip, 
          false, sContinuousArray, &nxout, &nyout);
        td->width = nxout;
        td->height = nyout;
      }
      sJ++;
      ReleaseMutex(sImageMutexHandle); sJ++;
      WaitForSingleObject(sDataMutexHandle, DATA_MUTEX_WAIT); sJ++;
      td->arrSize = td->width * td->height;
      td->iContWidth = td->width;
      td->iContHeight = td->height;

      // Signal that an image is ready; check for quitting
      //DebugToResult("RunContinuousAcquire: Setting frame done\n");
      td->iReadyToReturn = 1;
      nxout = GetTickCount() - startTime;
      maxTime = B3DMAX(maxTime, nxout);
      if (td->iEndContinuous || td->iDMquitting) {
        ReleaseMutex(sDataMutexHandle);
        sprintf(td->strTemp, "RunContinuousAcquire: Ending thread, time %.3f; last "
          "acquire %d  max %d\n", (GetTickCount() % (DWORD)3600000) / 1000., nxout, 
          maxTime);
        DebugToResult(td->strTemp);
        //ErrorToResult(td->strTemp, "INfo: ");
        break;
      }

      // Set the event if the other routine is waiting for a frame
      if (sFrameReadyEvent && CallIsFromSocket()) {
        if (td->iWaitingForFrame) {
          //ErrorToResult("RunContinuousAcquire signalling frame event\n", "INfo: ");
          td->iWaitingForFrame = 0;
          SetEvent(sFrameReadyEvent);
        }
      }

      ReleaseMutex(sDataMutexHandle);
      sJ++;
      if (!SleepMsg(10)) {
        DebugToResult("RunContinuousAcquire: Interrupted by quit\n");
        SetWatchedDataValue(td->iDMquitting, 1);
        break;
      }
    }
  }
  catch (exception exc) {
    sprintf(td->strTemp, "Caught an exception from call %d in continuous acquire:"
      "\n  %s\n", j, exc.what());
    ErrorToResult(td->strTemp);
    retval = DM_CALL_EXCEPTION;
  }

  // Shut down and clean up
  DebugToResult(retval == WRONG_DATA_TYPE ? "Ending thread due to wrong data type\n" :
    "Ending thread due to end or quit or exception\n");
  try {
    if (madeImage)
      DM::DeleteImage(image.get());
#ifdef GMS2
    if (td->bUseAcquisitionObj && startedAcquire)
      CM::ACQ_StopAcquire(acqObj);
#endif
    WaitForSingleObject(sImageMutexHandle, IMAGE_MUTEX_CLEAN_WAIT);
    DELETE_CONTINUOUS;
    ReleaseMutex(sImageMutexHandle);
  }
  catch (...) {
  }
  return retval;
}

/*
 * Get the next available frame from continuous acquisition
 */
int TemplatePlugIn::GetContinuousFrame(short array[], long *arrSize, long *width, 
                                       long *height, bool firstCall)
{
  double startTime = GetTickCount();
  double kickTime = startTime;
  SetWatchedDataValue(mTDcopy.iWaitingForFrame, 1);
  //ErrorToResult("GetContinuousFrame setting waitingForFrame\n", "INfo: ");
  for (; ;) {
    if (!sContinuousArray) {
      DebugToResult("GetContinuousFrame: no array!\n");
      return CONTINUOUS_ENDED;
    }

    // It looks like the thread gets stuck in the image acquisition sometimes when coming
    // in through COM.  Thus simply return the existing frame if enough time has passed.
    // NONE of the actions below will get the thread going again, but it looks like the
    // return from the COM call then releases it and it finishes a frame soon thereafter
    // Such delays did not show up when coming in through socket.
    // This was on a single-processor system.
    if (GetWatchedDataValue(mTDcopy.iReadyToReturn) || 
      (!firstCall && TickInterval(startTime) > CONTINUOUS_RETURN_TIMEOUT)) {
        WaitForSingleObject(sImageMutexHandle, IMAGE_MUTEX_WAIT);
        if (!sContinuousArray) {
          DebugToResult("GetContinuousFrame: no array after getting image mutex\n");
          ReleaseMutex(sImageMutexHandle);
          mTDcopy.iWaitingForFrame = 0;
          return CONTINUOUS_ENDED;
        }
        WaitForSingleObject(sDataMutexHandle, DATA_MUTEX_WAIT);
        *width = mTDcopy.iContWidth;
        *height = mTDcopy.iContHeight;
        *arrSize = mTDcopy.arrSize;
        mTDcopy.iReadyToReturn = 0;
        mTDcopy.iWaitingForFrame = 0;
        ReleaseMutex(sDataMutexHandle);
        if (!*arrSize || !(*width * *height)) {
          ReleaseMutex(sImageMutexHandle);
          sprintf(m_strTemp, "GetContinuousFrame:  lousy size %d or w/h %d %d\n", 
            *arrSize, *width, *height);
          DebugToResult(m_strTemp);
          return CONTINUOUS_ENDED;
        }
        memcpy(array, sContinuousArray, *arrSize * sizeof(short));
        ReleaseMutex(sImageMutexHandle);
        //ErrorToResult("GetContinuousFrame returning frame\n", "INfo: ");
        return 0;
    }

    // And sometimes this routine goes to sleep and won't wake up with all the acquire
    // activity when this is run from the socket thread, so if possible, wait on an event
    // signalling that a frame is ready.  But with a COM connection the thread has trouble
    // putting out debug output and getting the acquisition going, so use the regular
    // sleep with message-pumping there
    if (sFrameReadyEvent && CallIsFromSocket()) {
      if (!WaitForSingleObject(sFrameReadyEvent, FRAME_EVENT_WAIT)) {
        //ErrorToResult("GetContinuousFrame woke from frame event\n", "INfo: ");
        WaitForSingleObject(sDataMutexHandle, DATA_MUTEX_WAIT);
        mTDcopy.iWaitingForFrame = 0;
        ResetEvent(sFrameReadyEvent);
        ReleaseMutex(sDataMutexHandle);
      }
      if (mTDcopy.iDMquitting)
        return QUIT_DURING_SAVE;
    } else {

      // For COM connection do regular sleep
      if (!SleepMsg(10)) {
        DebugToResult("GetContinuousFrame: Looks like a quit\n");
        SetWatchedDataValue(mTDcopy.iDMquitting, 1);
        return QUIT_DURING_SAVE;
      }
    }
  }
  return CONTINUOUS_ENDED;
}

/*
 * Stop continuous acquires: just set the flag and wait until done.  But this can be
 * call from the main thread of SerialEM, so wait with a time-out
 */
int TemplatePlugIn::StopContinuousCamera()
{
  sprintf(m_strTemp, "StopContinuousCamera called, counter %d  time %.3f\n", sJ,
    (GetTickCount() % (DWORD)3600000) / 1000.);
  DebugToResult(m_strTemp);
  //ErrorToResult(m_strTemp, "INfo:");
  SetWatchedDataValue(mTDcopy.iEndContinuous, 1);
  mTD.bDoContinuous = false;
  return WaitForAcquireThread(WAIT_FOR_CONTINUOUS);
}

///////////////////////////////////////////////////////////////
// SUPPORT FUNCTIONS FOR AcquireProc and RunContinuousAcquire
///////////////////////////////////////////////////////////////

/* 
 * Accumulate time since last start and reset the start time
 */
static void AccumulateWallTime(double &cumTime, double &wallStart)
{
  double wallNow = wallTime();
  cumTime += wallNow - wallStart;
  wallStart = wallNow;
}

/*
 * Get millisecond interval from tick counts accounting for wraparound
 */
static double TickInterval(double start)
{
  double interval = GetTickCount() - start;
  if (interval < 0)
    interval += 4294967296.;
  return interval;
}

/*
 * Delete the passed-in image if there is no needsDelete argument or if it is still true,
 * and in latter case, sets it false.  Sets frame state so get frame thread knows it can
 * pass on the next image
 */
static void DeleteImageIfNeeded(ThreadData *td, DM::Image &image, bool *needsDelete)
{
  if (needsDelete && !(*needsDelete))
    return;
  try {
    DM::DeleteImage(image.get());
  }
  catch (exception exc) {
  }
  if (needsDelete)
    *needsDelete = false;
}

/*
 * Acquire the data mutex for setting or getting values accessed by multiple threads
 */
static void SetWatchedDataValue(int &member, int value)
{
  WaitForSingleObject(sDataMutexHandle, DATA_MUTEX_WAIT);
  member = value;
  ReleaseMutex(sDataMutexHandle);
}

static int GetWatchedDataValue(int &member)
{
  WaitForSingleObject(sDataMutexHandle, DATA_MUTEX_WAIT);
  int retval = member;
  ReleaseMutex(sDataMutexHandle);
  return retval;
}

/*
 * Given the single image or first stack frame, get all the parameters about it and check
 * array size
 */
static int GetTypeAndSizeInfo(ThreadData *td, DM::Image &image, int loop,  int outLimit,
                              bool doingStack)
{
  // Check the data type (getting the Gatan data type directly might have been easier)
  td->byteSize = DM::ImageGetDataElementByteSize(image.get());
  td->isInteger = DM::ImageIsDataTypeInteger(image.get());
  td->isUnsignedInt = DM::ImageIsDataTypeUnsignedInteger(image.get());
  td->isFloat = DM::ImageIsDataTypeFloat(image.get());
  td->signedBytes = td->byteSize == 1 && !td->isUnsignedInt;
  if (td->byteSize != td->dataSize && !((td->dataSize == 2 && td->byteSize == 4 && 
    (td->isInteger || td->isFloat)) ||
    (td->dataSize == 2 && td->byteSize == 1 && doingStack))) {
      sprintf(td->strTemp, "Image data are not of the expected type (bs %d  ds %d  int"
        " %d  uint %d)\n", td->byteSize, td->dataSize, td->isInteger ? 1:0, td->isUnsignedInt?1:0);
      ErrorToResult(td->strTemp);
      return 1;
  }

  // Get the size and adjust if necessary to fit output array
  DM::GetSize( image.get(), &td->width, &td->height );
  sprintf(td->strTemp, "loop %d stack %d width %d height %d  bs %d int %d uint %d\n", loop, 
    doingStack?1:0, td->width, td->height, td->byteSize, td->isInteger ? 1:0, td->isUnsignedInt?1:0);
  if (!sContinuousArray)
    DebugToResult(td->strTemp);
  if (td->width * td->height > outLimit) {
    sprintf(td->strTemp, "Warning: image is larger than the supplied array (image %dx%d"
      " = %d, array %d)\n", td->width, td->height, td->width * td->height, outLimit);
    ErrorToResult(td->strTemp, "\nA problem occurred acquiring an image for SerialEM:\n");
    td->height = outLimit / td->width;
  }
  return 0;
}

/*
 * Does many initializations for file saving on a first frame, including getting buffers
 */
static int InitOnFirstFrame(ThreadData *td, bool needTemp, bool needSum, 
                            long &frameDivide, bool &needProc)
{
  // Float super-res image is from software processing and needs special scaling
  // into bytes, set divide -1 as flag for this
  if (sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE && td->isFloat)
    frameDivide = -1;

  // But if they are counting mode images in integer, they need to not be scaled
  // or divided by 2 if placed into shorts, and they may be packed to bytes
  if (sReadModes[td->iReadMode] == K2_COUNTING_READ_MODE && td->isInteger) {
    td->fFloatScaling = 1.;
    frameDivide = 0;
    if(td->iSaveFlags & K2_SAVE_RAW_PACKED)
      frameDivide = -2;
  }

  // For 2.3.1 simulator (so far) super-res nonGN frames come through as shorts
  // so they need to be truncated to bytes also
  if (sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE && td->isInteger && 
    td->byteSize == 2) {
      td->fFloatScaling = 1.;
      frameDivide = -2;
  }

  td->outByteSize = 2;
  if (td->byteSize == 1 || frameDivide < 0) {
    td->fileMode = MRC_MODE_BYTE;
    td->outByteSize = 1;
  } else if ((frameDivide > 0 && td->byteSize != 2) || 
    (td->dataSize == td->byteSize && !td->isUnsignedInt))
    td->fileMode = MRC_MODE_SHORT;
  else
    td->fileMode = MRC_MODE_USHORT;
  td->save4bit = sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE && td->byteSize <= 2
    && (td->iSaveFlags & K2_SAVE_RAW_PACKED);
  needProc = td->byteSize > 2 || td->signedBytes || frameDivide < 0;

  td->writeScaling = 1.;
  if (needProc && td->byteSize > 2) {
    td->writeScaling = td->fFloatScaling;
    if (frameDivide > 0)
      td->writeScaling /= 2.;
    else if (frameDivide == -1)
      td->writeScaling = SUPERRES_FRAME_SCALE;
  }

  td->iErrorFromSave = ROTBUF_MEMORY_ERROR;

  // Allocate buffer for rotation/flip if processing needs to be done too
  // Do a rotation/flip if needed or if writing MRC, to get flip for output
  if (needProc && (td->iFrameRotFlip || !td->bWriteTiff)) {
    try {
      if (td->outByteSize == 1)
        td->rotBuf = (short *)(new unsigned char [td->width * td->height]);
      else
        td->rotBuf = new short [td->width * td->height];
    }
    catch (...) {
      td->rotBuf = NULL;
      return 1;
    }
  }
  if (needTemp) {
    try {
      td->tempBuf = new short [td->width * td->height];
    }
    catch (...) {
      td->tempBuf = NULL;
      return 1;
    }
  }
  if (needSum) {
    try {
      td->sumBuf = new int [td->width * td->height];
      memset(td->sumBuf, 0, 4 * td->width * td->height);
    }
    catch (...) {
      td->sumBuf = NULL;
      return 1;
    }
  }
  if (td->bSaveSummedFrames) {
    try {
      td->outSumBuf = new short [td->width * td->height];
    }
    catch (...) {
      td->outSumBuf = NULL;
      return 1;
    }
  }

  // Allocate the pointer array for stack of grabbed frames
  if (td->iNumGrabAndStack) {
    try {
      td->grabStack = new void * [td->iNumGrabAndStack];
      for (int i = 0; i < td->iNumGrabAndStack; i++)
        td->grabStack[i] = NULL;
    }
    catch (...) {
      td->grabStack = NULL;
      return 1;
    }
  }

  td->iErrorFromSave = 0;

  if (((sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE ||
    sReadModes[td->iReadMode] == K2_COUNTING_READ_MODE) && !td->isFloat && 
    td->iK2Processing != NEWCM_GAIN_NORMALIZED) &&
    (td->iSaveFlags & K2_COPY_GAIN_REF) && td->strGainRefToCopy.length())
    CopyK2ReferenceIfNeeded(td);
  return 0;
}

/*
 * Opens file if needed, gets min/max/mean, packs image if needed, saves image to file
 */
static int PackAndSaveImage(ThreadData *td, void *array, int nxout, int nyout, int slice, 
                            bool finalFrame, int &fileSlice, int &tmin, 
                            int &tmax, float &meanSum)
{
#ifdef _WIN64
  float tmean;
  int i, j, tsum, val;
  int nxFile = td->save4bit ? nxout / 2 : nxout;
  short *sData, *sSum;
  unsigned short *usData, *usSum;
  unsigned char *bData, *packed;
  unsigned char lowbyte;

  // Sum frame first if flag is set, and there is a count
  if (td->bSaveSummedFrames && td->outSumFrameIndex < td->outSumFrameList.size()) {
    if (td->numFramesInOutSum[td->outSumFrameIndex] > 1) {

      // If there is just one frame in this sum, just go on and use it,
      // Otherwise add frame to sum: zero sum first if it is the first one
      if (!td->numAddedToOutSum)
        memset(td->outSumBuf, 0, 2 * nxout * nyout);
      sSum = (short *)td->outSumBuf;
      usSum = (unsigned short *)td->outSumBuf;
      if (td->fileMode == MRC_MODE_USHORT) {
        usData = (unsigned short *)td->outData;
        for (i = 0; i < nxout * nyout; i++)
          *(usSum++) += *(usData++);
      } else if (td->fileMode == MRC_MODE_SHORT) {
        sData = td->outData;
        for (i = 0; i < nxout * nyout; i++)
          *(sSum++) += *(sData++);
      } else {
        bData = (unsigned char *)td->outData;
        for (i = 0; i < nxout * nyout; i++)
          *(usSum++) += *(bData++);
      }

      // Increment count; if it still short, return
      td->numAddedToOutSum++;
      if (td->numAddedToOutSum < td->numFramesInOutSum[td->outSumFrameIndex] && 
        !finalFrame)
        return 0;

      // Otherwise, copy data back to outData
      sSum = (short *)td->outSumBuf;
      usSum = (unsigned short *)td->outSumBuf;
      if (td->fileMode == MRC_MODE_USHORT) {
        usData = (unsigned short *)td->outData;
        for (i = 0; i < nxout * nyout; i++)
          *(usData++) = *(usSum++);
      } else if (td->fileMode == MRC_MODE_SHORT) {
        sData = td->outData;
        for (i = 0; i < nxout * nyout; i++)
          *(sData++) = *(sSum++);
      } else {
        bData = (unsigned char *)td->outData;
        for (i = 0; i < nxout * nyout; i++) {
          *(bData++) = (unsigned char)B3DMIN(255, *usSum);
          usSum++;
        }
      }
    }

    // Increment number of frames done and index if necessary, zero the sum count
    td->numOutSumsDoneAtIndex++;
    if (td->numOutSumsDoneAtIndex == td->outSumFrameList[td->outSumFrameIndex]) {
      td->outSumFrameIndex++;
      td->numOutSumsDoneAtIndex = 0;
    }
    td->numAddedToOutSum = 0;
  }

  // open file if needed
  if (!slice || td->bFilePerImage) {
    if (td->bFilePerImage)
      sprintf(td->strTemp, "%s\\%s_%03d.%s", td->strSaveDir.c_str(),
      td->strRootName.c_str(), slice +1, td->bWriteTiff ? "tif" : "mrc");
    else
      sprintf(td->strTemp, "%s\\%s.%s", td->strSaveDir.c_str(), td->strRootName.c_str(), 
      td->bWriteTiff ? "tif" : "mrc");
    if (td->bWriteTiff) {

      // Set up Tiff output file structure first time
      if (!slice) {
        td->iifile = iiNew();
        td->iifile->nz = 1; // Has no effect to set it higher
        td->iifile->format = IIFORMAT_LUMINANCE;
        td->iifile->file = IIFILE_TIFF;
        td->iifile->type = IITYPE_UBYTE;
        if (td->fileMode == MRC_MODE_SHORT)
          td->iifile->type = IITYPE_SHORT;
        if (td->fileMode == MRC_MODE_USHORT)
          td->iifile->type = IITYPE_USHORT;
      }
      td->iifile->nx = nxFile;
      td->iifile->ny = nyout;
      td->iifile->filename = _strdup(td->strTemp);
      i = tiffOpenNew(td->iifile);
    } else {
      td->fp = fopen(td->strTemp, "wb");
    }
    if ((td->bWriteTiff && i) || (!td->bWriteTiff && !td->fp)) {
      j = errno;
      i = (int)strlen(td->strTemp);
      td->strTemp[i] = '\n';
      td->strTemp[i+1] = 0x00;
      ErrorToResult(td->strTemp);
      sprintf(td->strTemp, "Failed to open above file: %s\n", strerror(j));
      ErrorToResult(td->strTemp);
      td->iErrorFromSave = FILE_OPEN_ERROR;
      return 1;
    }

    // Set up header for one slice
    if (!td->bWriteTiff) {
      mrc_head_new(&td->hdata, nxFile, nyout, 1, td->fileMode);
      sprintf(td->strTemp, "SerialEMCCD: Dose frac. image, scaled by %.2f  r/f %d", 
        td->writeScaling, td->iFrameRotFlip);
      if (td->save4bit) {
        sprintf(td->strTemp, "SerialEMCCD: Dose frac. image, 4 bits packed  r/f %d",
          td->iFrameRotFlip);
        td->hdata.imodFlags |= MRC_FLAGS_4BIT_BYTES;
      }
      mrc_head_label(&td->hdata, td->strTemp);
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
    if (td->outByteSize == 1) {
      bData = ((unsigned char *)td->outData) + i * nxout;
      sData = (short *)bData;
    } else
      sData = &td->outData[i * nxout];
    usData = (unsigned short *)sData;

    // Get min/max/sum and add to mean
    tsum = 0;
    if (td->fileMode == MRC_MODE_USHORT) {
      for (j = 0; j < nxout; j++) {
        val = usData[j];
        tmin = B3DMIN(tmin, val);
        tmax = B3DMAX(tmax, val);
        tsum += val;
      }
    } else if (td->fileMode == MRC_MODE_SHORT) {
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
  if (td->save4bit) {
    /* if (!slice) {
    Islice sl;
    sliceInit(&sl, nxout, nyout, SLICE_MODE_BYTE, td->outData);
    sprintf(td->strTemp, "%s\\firstFrameUnpacked.mrc", td->strSaveDir);
    sliceWriteMRCfile(td->strTemp, &sl);
    } */
    bData = (unsigned char *)td->outData;
    packed = (unsigned char *)array;
    td->outData = (short *)array;
    for (i = 0; i < nxFile * nyout; i++) {
      lowbyte = *bData++ & 15;
      *packed++ = lowbyte | ((*bData++ & 15) << 4);
    }
  }

  if (td->bWriteTiff) {
    td->iifile->amin = (float)tmin;
    td->iifile->amax = (float)tmax;
    td->iifile->amean = tmean / (float)(nxout * nyout);

    i = tiffWriteSection(td->iifile, td->outData, td->iTiffCompression, 1, 
      B3DNINT(2.54e8 / td->dPixelSize), td->iTiffQuality);
    if (i) {
      td->iErrorFromSave = WRITE_DATA_ERROR;
      sprintf(td->strTemp, "Error (%d) writing section %d to TIFF file\n", i, 
        slice);
      ErrorToResult(td->strTemp);
      return 1;
    }
  } else {

    // Seek to slice then write it
    i = mrc_big_seek(td->fp, td->hdata.headerSize, nxFile * td->outByteSize, 
      nyout * fileSlice, SEEK_SET);
    if (i) {
      sprintf(td->strTemp, "Error %d seeking for slice %d: %s\n", i, slice, 
        strerror(errno));
      ErrorToResult(td->strTemp);
      td->iErrorFromSave = SEEK_ERROR;
      return 1;
    }

    if ((i = (int)b3dFwrite(td->outData, td->outByteSize * nxFile, nyout, td->fp)) != nyout) {
      sprintf(td->strTemp, "Failed to write data past line %d of slice %d: %s\n", i, 
        slice, strerror(errno));
      ErrorToResult(td->strTemp);
      td->iErrorFromSave = WRITE_DATA_ERROR;
      return 1;
    }

    // Completely update and write the header regardless of whether one file/slice
    fileSlice++;
    td->hdata.nz = td->hdata.mz = fileSlice;
    td->hdata.amin = (float)tmin;
    td->hdata.amax = (float)tmax;
    meanSum += tmean / B3DMAX(1.f, (float)(nxout * nyout));
    td->hdata.amean = meanSum / td->hdata.nz;
    mrc_set_scale(&td->hdata, td->dPixelSize, td->dPixelSize, td->dPixelSize);
    if (mrc_head_write(td->fp, &td->hdata)) {
      ErrorToResult("Failed to write header\n");
      td->iErrorFromSave = HEADER_ERROR;
      return 1;
    }
  }
      
  // Increment successful save count and close file if needed
  td->iFramesSaved++;
  if (td->bFilePerImage || finalFrame) {
    if (td->bWriteTiff) {
      iiClose(td->iifile);
      free(td->iifile->filename);
      td->iifile->filename = NULL;
    } else {
      fclose(td->fp);
      td->fp = NULL;
    }
  }
#endif
  return 0;
}

/*
* ProcessImage copies data from DM to given array with conversion, truncation, scaling
* and division by 2 as needed.  There are way too many cases handled here, thanks to the
* K2 camera.  Here is list of the kind of data that it produces in various cases from
* actual camera (values in parentheses are from the simulator, * for 2.3.1 only)
*                Linear              Counting                   Super-res
* Unproc single  us int              us int                     us short (int)
* Unproc df sum  us int              us int                     us int
* Unproc frames  us short            us short (us int*)         byte (us int*)
* DS single      sign int            sign int                   sign short (int)
* DS df sum      sign int            sign int                   sign int
* DS frames      sign int (short)    sign int (short -> ushort) sign byte (us int*)
* GN single/sum  float               float                      float
* GN frames      float               float                      float
*/
static void  ProcessImage(void *imageData, void *array, int dataSize, long width, 
                          long height, long divideBy2, long transpose, int byteSize, 
                          bool isInteger, bool isUnsignedInt, float floatScaling)
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
  if ((dataSize == byteSize && !divideBy2 && floatScaling == 1.) || byteSize == 1 ||
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
        operations[transpose & 3], true, (short *)array, &i, &j);
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
          outData[i] = (short)((float)bData[i] * floatScaling / 2.f + 0.5f);
      } else if (byteSize == 2 && floatScaling == 1.) {

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
          outData[i] = (short)((float)usData[i] * floatScaling / 2.f + 0.5f);
      } else if (floatScaling == 1.) {

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
          outData[i] = (short)((float)uiData[i] * floatScaling / 2.f + 0.5f);
      }
    } else if (isInteger) {
      if (byteSize == 2 && floatScaling == 1.) {

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
          outData[i] = (short)((float)sData[i] * floatScaling / 2.f + 0.5f);
      } else if (floatScaling == 1.) {

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
          outData[i] = (short)((float)iData[i] * floatScaling / 2.f + 0.5f);
      }
    } else {

      // Float to short
      DebugToResult("Dividing floats by 2 with scaling\n");
      flIn = (float *)imageData;
      for (i = 0; i < width * height; i++)
        outData[i] = (short)(flIn[i] * floatScaling / 2.f + 0.5f);
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
          usData[i] = (unsigned short)((float)bData[i] * floatScaling + 0.5f);
      } else if (byteSize == 2) {

        DebugToResult("Scaling unsigned shorts\n");
        usData2 = (unsigned short *)imageData;
        for (i = 0; i < width * height; i++)
          usData[i] = (unsigned short)((float)usData2[i] * floatScaling + 0.5f);

        // Unsigned short to ushort with scaling

      } else if (floatScaling == 1.) {

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
          usData[i] = (unsigned short)((float)uiData[i] * floatScaling + 0.5f);
      }
    } else if (isInteger) {

      if (byteSize == 2) {

        // Scaling signed shorts: convert to unsigned and truncate at 0
        DebugToResult("Converting signed shorts to unsigned shorts with scaling "
          "and truncation\n");
        sData = (short *)imageData;
        for (i = 0; i < width * height; i++) {
          if (sData[i] >= 0)
            usData[i] = (unsigned short)((float)sData[i] * floatScaling + 0.5f);
          else
            usData[i] = 0;
        }
      } else if (floatScaling == 1.) {

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
            usData[i] = (unsigned short)((float)iData[i] * floatScaling + 0.5f);
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
          usData[i] = (unsigned short)(flIn[i] * floatScaling + 0.5f);
        else
          usData[i] = 0;
      }
    }
  }
}

/*
 * Perform any combination of rotation and Y flipping for a short array: 
 * operation = 0-3 for rotation by 90 * operation, plus 4 for flipping around Y axis 
 * before rotation or 8 for flipping around Y after.  THIS IS COPY OF ProcRotateFlip
 * with the type and invertCon arguments removed and invert contrast code also
 */
static void RotateFlip(short int *array, int mode, int nx, int ny, int operation, 
                       bool invert, short int *brray, int *nxout, int *nyout)
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
  if (operation < 8 && invert)
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

/*
 * Add a frame of data of any given type to an integer or float sum image
 */
static void AddToSum(ThreadData *td, void *data, void *sumArray) 
{
  int i;
  int *outData = (int *)sumArray;
  float *flIn, *flOut;
  int numPix = td->width * td->height;
  if (td->isFloat) {                    // FLOAT
    flIn = (float *)data;
    flOut = (float *)sumArray;
    for (i = 0; i < numPix; i++)
      *flOut++ += *flIn++;
  } else if (td->signedBytes) {         // SIGNED BYTES
    char *inData = (char *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  } else if (td->byteSize == 1) {       // UNSIGNED BYTES
    unsigned char *inData = (unsigned char *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  } else if (td->byteSize == 2 && td->isUnsignedInt) {  // UNSIGNED SHORT
    unsigned short *inData = (unsigned short *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  } else if (td->byteSize == 2) {      // SIGNED SHORT
    short *inData = (short *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  } else if (td->isUnsignedInt) {      // UNSIGNED INT
    unsigned int *inData = (unsigned int *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  } else {                             // SIGNED INT
    int *inData = (int *)data;
    for (i = 0; i < numPix; i++)
      *outData++ += *inData++;
  }
}
 

/*
 * Copy a gain reference file to the directory if there is not a newer one
 */
static int CopyK2ReferenceIfNeeded(ThreadData *td)
{
  WIN32_FIND_DATA findRefData, findCopyData;
  HANDLE hFindRef, hFindCopy;
  string saveDir = td->strSaveDir;
  string rootName = td->strRootName;
  char *prefix[2] = {"Count", "Super"};
  int ind, retVal = 0;
  bool needCopy = true, namesOK = false;
  int prefInd = sReadModes[td->iReadMode] == K2_SUPERRES_READ_MODE ? 1 : 0;

  // For single image files, find the date-time root and split up the name
  if (td->bFilePerImage) {
    ind = (int)saveDir.find_last_of("\\");
    if (ind < 2 || ind >= (int)saveDir.length() - 1)
      return 1;
    rootName = saveDir.data() + ind + 1;
    saveDir.erase(ind, string::npos);
  }


  // Get the reference file first
  hFindRef = FindFirstFile(td->strGainRefToCopy.c_str(), &findRefData);
  if (hFindRef == INVALID_HANDLE_VALUE) {
    sprintf(td->strTemp, "Cannot find K2 gain reference file: %s\n", 
      td->strGainRefToCopy.c_str());
    ErrorToResult(td->strTemp);
    return 1;
  } 
  FindClose(hFindRef);

  // If there is an existing copy and the mode and the directory match, find the copy
  // Otherwise look for all matching files in directory
  if (td->strLastRefName.length() && strstr(td->strLastRefName.c_str(), prefix[prefInd])
    && !td->strLastRefDir.compare(saveDir)) {
    //sprintf(td->strTemp, "Finding %s\n", td->strLastRefName);
    hFindCopy = FindFirstFile(td->strLastRefName.c_str(), &findCopyData);
    namesOK = true;
  } else {
    sprintf(td->strTemp, "%s\\%sRef_*.dm4", saveDir.c_str(), prefix[prefInd]);
    hFindCopy = FindFirstFile(td->strTemp, &findCopyData);
    //sprintf(td->strTemp, "finding %s\\%sRef_*.dm4\n", saveDir.data(), prefix[prefInd]);
  }
  // DebugToResult(td->strTemp);

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
      //sprintf(td->strTemp, "refSec  %f  copySec %f\n", refSec, copySec);
      //DebugToResult(td->strTemp);
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
  td->strLastRefDir = saveDir;
  if (!needCopy) {
    td->strLastRefName = saveDir + "\\";
    td->strLastRefName += findCopyData.cFileName;
    return 0;
  }
  sprintf(td->strTemp, "%s\\%sRef_%s.dm4", saveDir.c_str(), prefix[prefInd],
    rootName.c_str());
  td->strLastRefName = td->strTemp;

  // Copy the reference
  DebugToResult("Making new copy of gain reference\n");
  if (!CopyFile(td->strGainRefToCopy.c_str(), td->strTemp, false)) {
    sprintf(td->strTemp, "An error occurred copying %s to %s\n", 
      td->strGainRefToCopy.c_str(), td->strTemp);
    ErrorToResult(td->strTemp);
    td->strLastRefName = "";
    return 1;
  }

  return 0;
}

/*
 * sleeps for the given amount of time while pumping messages
 * returns TRUE if successful, FALSE otherwise
 */
static BOOL SleepMsg(DWORD dwTime_ms)
{
  DWORD dwStart = GetTickCount();
  DWORD dwElapsed;
  while ((dwElapsed = GetTickCount() - dwStart) < dwTime_ms) {
    DWORD dwStatus = MsgWaitForMultipleObjects(0, NULL, FALSE,
                                               dwTime_ms - dwElapsed, QS_ALLINPUT);
    
    if (dwStatus == WAIT_OBJECT_0) {
      MSG msg;
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
          PostQuitMessage((int)msg.wParam);
          return FALSE; // abandoned due to WM_QUIT
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
  }

  return TRUE;
}

//////////////////////////////////////////////////
// DONE WITH AcquireProc SUPPORT
// REMAINING CALLS TO THE PLUGIN BELOW
//////////////////////////////////////////////////

/*
 * Add commands for selecting a camera to the command string
 */ 
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
  mTD.strCommand += m_strTemp;
}

/*
 * Make camera be the current camera and execute selection script
 */
int TemplatePlugIn::SelectCamera(long camera)
{
  m_iCurrentCamera = camera;
  mTD.strCommand.resize(0);
  AddCameraSelection();
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
  return 0;
}

/*
 * Set the read mode and the scaling factor
 */
void TemplatePlugIn::SetReadMode(long mode, double scaling)
{
  if (mode > 2)
    mode = 2;

  //  Constrain the scaling to a maximum of 1 in linear mode; old SEM always sends 
  // counting mode scaling, newer SEM sends 1 for Summit linear or 0.25 for Base mode
  if (mode <= 0 && scaling > 1.)
    scaling = 1.;
  mTD.iReadMode = mode;
  mTD.fFloatScaling = (float)scaling;
  if (mode < 0) {
    m_bSaveFrames = false;
    m_bDoseFrac = false;
  }
  mTD.iAntialias = 0;
}

/*
 * Set the parameters for the next K2 acquisition
 */
void TemplatePlugIn::SetK2Parameters(long mode, double scaling, long hardwareProc, 
                                     BOOL doseFrac, double frameTime, BOOL alignFrames, 
                                     BOOL saveFrames, long rotationFlip, long flags, 
                                     double reducedSizes, double dummy2, double dummy3, 
                                     double dummy4, char *filter)
{
  SetReadMode(mode, scaling);
  mTD.iHardwareProc = hardwareProc;
  B3DCLAMP(mTD.iHardwareProc, 0, 6);
  m_bDoseFrac = doseFrac;
  mTD.dFrameTime = frameTime;
  mTD.bAlignFrames = alignFrames;
  if (rotationFlip >= 0)
    mTD.iRotationFlip = rotationFlip;
  if (alignFrames) {
    strncpy(mTD.strFilterName, filter, MAX_FILTER_NAME - 1);
    mTD.strFilterName[MAX_FILTER_NAME - 1] = 0x00;
  }
  m_bSaveFrames = saveFrames;
  mTD.iAntialias = flags & K2_ANTIALIAS_MASK;
  if (mTD.iAntialias) {
    mTD.iFinalHeight = B3DNINT(reducedSizes / K2_REDUCED_Y_SCALE);
    mTD.iFinalWidth = B3DNINT(reducedSizes - K2_REDUCED_Y_SCALE * mTD.iFinalHeight);
  }
  sprintf(m_strTemp, "SetK2Parameters called with save %s\n", m_bSaveFrames ? "Y":"N");
  DebugToResult(m_strTemp);
}

/*
 * Setup properties for saving frames in files.  See DMCamera.cpp for call documentation
 */
void TemplatePlugIn::SetupFileSaving(long rotationFlip, BOOL filePerImage, 
                                     double pixelSize, long flags, double nSumAndGrab, 
                                     double dummy2, double dummy3, double dummy4, 
                                     char *dirName, char *rootName, char *refName,
                                     char *defects, char *command, char *sumList, 
                                     long *error)
{
  struct _stat statbuf;
  FILE *fp;
  string topDir;
  char *strForTok, *token;
  int ind1, ind2, newDir, dummy, newDefects = 0, created = 0;
  unsigned int uiSumAndGrab = (unsigned int)(nSumAndGrab + 0.1);
  mTD.iRotationFlip = rotationFlip;
  mTD.iFrameRotFlip = (flags & K2_SKIP_FRAME_ROTFLIP) ? 0 : rotationFlip;
  B3DCLAMP(mTD.iRotationFlip, 0, 7);
  mTD.bFilePerImage = filePerImage;
  mTD.dPixelSize = pixelSize;
  mTD.iSaveFlags = flags;
  mTD.bWriteTiff = (flags & (K2_SAVE_LZW_TIFF | K2_SAVE_ZIP_TIFF)) != 0;
  mTD.iTiffCompression = (flags & K2_SAVE_ZIP_TIFF) ? 8 : 5;
  mTD.bAsyncSave = (flags & K2_SAVE_SYNCHRON) == 0;
  mTD.bEarlyReturn = (flags & K2_EARLY_RETURN) != 0;
  mTD.bAsyncToRAM = (flags & K2_ASYNC_IN_RAM) != 0;
  mTD.bSaveSummedFrames = (flags & K2_SAVE_SUMMED_FRAMES) != 0;

  // Copy all the strings if they are changed
  if (CopyStringIfChanged(dirName, mTD.strSaveDir, newDir, error))
    return;
  if (CopyStringIfChanged(rootName, mTD.strRootName, dummy, error))
    return;
  if (refName && (flags & K2_COPY_GAIN_REF)) {
    if (CopyStringIfChanged(refName, mTD.strGainRefToCopy, dummy, error))
      return;
  }
  if (defects && (flags & K2_SAVE_DEFECTS)) {
    if (CopyStringIfChanged(defects, m_strDefectsToSave, newDefects, error))
      return;
  }
  if (command && (flags & K2_RUN_COMMAND)) {
    if (CopyStringIfChanged(command, m_strPostSaveCom, dummy, error))
      return;
  }

  // Unpack the list of frames to sum
  if (sumList && mTD.bSaveSummedFrames) {
    mTD.outSumFrameList.clear();
    mTD.numFramesInOutSum.clear();
    strForTok = sumList;
    while ((token = strtok(strForTok, " ")) != NULL) {
      strForTok = NULL;
      ind1 = atoi(token);
      token = strtok(strForTok, " ");
      if (token)
        ind2 = atoi(token);
      if (ind1 <= 0 || !token || ind2 <= 0 || ind1 > 32000 || ind2 > 32000) {
        *error = BAD_SUM_LIST;
        return;
      }
      try {
        mTD.outSumFrameList.push_back((short)ind1);
        mTD.numFramesInOutSum.push_back((short)ind2);
      }
      catch (...) {
        *error = BAD_SUM_LIST;
        return;
      }
    }
  }

  // Get number of frames to sum from low 16 bits and number to stack fast from high
  mTD.iNumFramesToSum = 65535;
  mTD.iNumGrabAndStack = 0;
  if (mTD.bEarlyReturn) {
    mTD.iNumFramesToSum = uiSumAndGrab & 65535;
    mTD.iNumGrabAndStack = GMS_SDK_VERSION > 30 ? (uiSumAndGrab >> 16) : 0;
  }

  sprintf(m_strTemp, "SetupFileSaving called with flags %x rf %d frf %d %s fpi %s pix %f"
    " ER %s A2R %s sum %d grab %d\n  copy %s \n  dir %s root %s\n", flags, rotationFlip,
    mTD.iFrameRotFlip, mTD.bWriteTiff ? "TIFF" : "MRC", filePerImage ? "Y":"N", pixelSize, 
    mTD.bEarlyReturn ? "Y":"N", mTD.bAsyncToRAM ? "Y":"N", mTD.iNumFramesToSum,
    mTD.iNumGrabAndStack, (flags & K2_COPY_GAIN_REF) ? mTD.strGainRefToCopy.c_str() :"NO",
    mTD.strSaveDir.c_str(), mTD.strRootName.c_str());
  DebugToResult(m_strTemp);

  if (mTD.bEarlyReturn && !mTD.bAsyncSave && GMS_SDK_VERSION > 30) {
    *error = EARLY_RET_WITH_SYNC;
    return;
  }

  // Get the top directory, which is the directory itself or the parent for one file/image
  *error = 0;
  topDir = mTD.strSaveDir;
  if (filePerImage) {
    ind1 = (int)topDir.rfind('/');
    ind2 = (int)topDir.rfind('\\');
    if (ind2 > ind1)
      ind1 = ind2;
    topDir.erase(ind1);
  }

  // make sure directory exists and is a directory, or try to create it if not
  // 
  if (_stat(topDir.c_str(), &statbuf)) {
    sprintf(m_strTemp, "Trying to create %s\n", topDir.c_str());
    DebugToResult(m_strTemp);
    if (_mkdir(topDir.c_str()))
      *error = DIR_CREATE_ERROR;
    created = 1;
  } else if ( !(statbuf.st_mode & _S_IFDIR)) {
    *error = SAVEDIR_IS_FILE;
  }

  // For one file per image, create the directory, which must not exist
  if (! *error && filePerImage) {
    if (_mkdir(mTD.strSaveDir.c_str())) {
      if (errno == EEXIST)
        *error = DIR_ALREADY_EXISTS;
      else
        *error = DIR_CREATE_ERROR;
    }
  } else if (! *error) {

    // Check whether the file exists
    // If so, and the backup already exists, that is an error; otherwise back it up
    sprintf(m_strTemp, "%s\\%s.mrc", mTD.strSaveDir.c_str(), mTD.strRootName.c_str());
    if (! *error && !_stat(m_strTemp, &statbuf)) {
      topDir = string(m_strTemp) + '~';
      if (!_stat(topDir.c_str(), &statbuf) || imodBackupFile(m_strTemp))
        *error = FILE_ALREADY_EXISTS;
    }

    // For a new directory that was not just created, check writability by opening file
    if (! *error && newDir && !created) {
      fp = fopen(m_strTemp, "wb");
      if (!fp) {
        *error = DIR_NOT_WRITABLE;
      } else {
        fclose(fp);
        DeleteFile((LPCTSTR)m_strTemp);
      }
    }
  }
  if (!*error && defects && (flags & K2_SAVE_DEFECTS)) {
    
    // Extract or make up a filename
    m_strTemp[0] = 0x00;
    if (m_strDefectsToSave[0] == '#') {
      dummy = (int)m_strDefectsToSave.find('\n') - 1;
      if (dummy > 0 && m_strDefectsToSave[dummy] == '\r')
        dummy--;
      if (dummy > 3 && dummy < MAX_TEMP_STRING - 3 - (int)mTD.strSaveDir.length()) {
        string tmpDef = m_strDefectsToSave.substr(1, dummy);
        sprintf(m_strTemp, "%s\\%s", mTD.strSaveDir.c_str(), tmpDef.c_str());
      }
    }

    if (!m_strTemp[0])
      sprintf(m_strTemp, "%s\\defects%d.txt", mTD.strSaveDir.c_str(),
      (GetTickCount() / 1000) % 10000);

    // If the string is new, one file per image, new directory, or the file doesn't
    // exist, write the text
    if (newDefects || filePerImage || newDir || !_stat(m_strTemp, &statbuf)) {
      fp = fopen(m_strTemp, "wt");
      if (!fp) {
        *error = OPEN_DEFECTS_ERROR;
      } else {
        dummy = (int)m_strDefectsToSave.length();
        if (fwrite(m_strDefectsToSave.c_str(), 1, dummy, fp) != dummy)
          *error = WRITE_DEFECTS_ERROR;
        fclose(fp);
      }
    }
  }
  if (*error) {
    sprintf(m_strTemp, "SetupFileSaving error is %d\n", *error);
    DebugToResult(m_strTemp);
  }
}

/*
 * Duplicate a string into the given member variable if it has changed
 */
int TemplatePlugIn::CopyStringIfChanged(char *newStr, string &memberStr, int &changed,
                                        long *error)
{
  changed = 1;
  if (memberStr.length())
    changed = memberStr.compare(newStr);
  if (changed) {
    try {
      memberStr = newStr;
    }
    catch (...) {
      *error = ROTBUF_MEMORY_ERROR;
      return 1;
    }
  }
  return 0;
}

/*
 * Get results of last frame saving operation
 */
void TemplatePlugIn::GetFileSaveResult(long *numSaved, long *error)
{
  *numSaved = mTD.iFramesSaved;
  *error = mTD.iErrorFromSave;
}

/*
 * Get the defect list from DM for the current camera
 */
int TemplatePlugIn::GetDefectList(short xyPairs[], long *arrSize, 
                                  long *numPoints, long *numTotal)
{
#if defined(GMS2) && GMS_SDK_VERSION > 30
  unsigned short *pairs = (unsigned short *)xyPairs;
  long minRetSize = B3DMIN(2048, *arrSize);
  memset(xyPairs, 0, 2 * minRetSize);
  try {
    DM::TagGroup defectList = CM::CCD_GetSystemDefectLocations(CM::GetCurrentCamera(), 
      "");
    *numTotal = defectList.CountTags();
    *numPoints = B3DMIN(*arrSize / 2, *numTotal);
    *arrSize = B3DMAX(minRetSize, 2 * *numPoints);
    for (long i = 0; i < *numPoints; i++) {
      SSIZE_T x, y;
      defectList.GetIndexedTagAsLongPoint(i, &x, &y);
      pairs[2 * i] = (unsigned short)x;
      pairs[2 * i + 1] = (unsigned short)y;
    }
  }
  catch (exception exc) {
    return GETTING_DEFECTS_ERROR;
  }
  return 0;
#else
  return GETTING_DEFECTS_ERROR;
#endif
}

/*
 * Return number of cameras or -1 for error
 */
int TemplatePlugIn::GetNumberOfCameras()
{
  mTD.strCommand.resize(0);
  if (m_iDMVersion < NEW_CAMERA_MANAGER)
    mTD.strCommand += "number num = MSCGetCameraCount()\n"
            "Exit(num)";
  else
    mTD.strCommand += "Object manager = CM_GetCameraManager()\n"
            "Object cameraList = CM_GetCameras(manager)\n"
            "number listsize = SizeOfList(cameraList)\n"
            "Exit(listsize)";
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return -1;
  return (int)(retval + 0.1);
}

/*
 * Determine insertion state of given camera: return -1 for error, 0 if out, 1 if in
 */
int TemplatePlugIn::IsCameraInserted(long camera)
{
  mTD.strCommand.resize(0);
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
  mTD.strCommand += m_strTemp;
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return -1;
  return (int)(retval + 0.1);
}

/*
 * Set the insertion state of the given camera
 */
int TemplatePlugIn::InsertCamera(long camera, BOOL state)
{
  mTD.strCommand.resize(0);
  if (m_iDMVersion < NEW_CAMERA_MANAGER)
    sprintf(m_strTemp, "MSCSelectCamera(%d)\n"
            "MSCSetCameraIn(%d)", camera, state ? 1 : 0);
  else
    sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
            "Object cameraList = CM_GetCameras(manager)\n"
            "Object camera = ObjectAt(cameraList, %d)\n"
            "CM_SetCameraInserted(camera, %d)\n", camera, state ? 1 : 0);
  mTD.strCommand += m_strTemp;
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
  return 0;
}

/*
 * Get version from DM, return it and set internal version number
 * HERE IS WHERE IT MATTERS HOW GMS_SDK_VERSION IS DEFINED
 * IT MUST BE 0, 1, 2, then 30, 31, etc above 2.
 */
long TemplatePlugIn::GetDMVersion()
{
  unsigned int code;
  mTD.strCommand.resize(0);
  if (m_bGMS2) {
    if (GMS_SDK_VERSION < 2) {
      DebugToResult("GMS2 version < 2, just returning 40000\n");
      m_iDMVersion = 40000;
      return 40000;
    }
    mTD.strCommand += "number major, minor, build\n"
          "GetApplicationVersion(major, minor, build)\n"
          "Exit(10000 * major + minor)";
  } else {
    mTD.strCommand += "number version\n"
          "GetApplicationInfo(2, version)\n"
          "Exit(version)";
  }
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return -1;
  code = (unsigned int)(retval + 0.1);
  if (m_bGMS2) {
    int major = code / 10000;
    int minor = code % 10000;
    m_iDMVersion = 10000 * (2 + major) + 100 * (minor / 10) + (minor % 10);
  } else {

    // They don't support the last digit
    if ((code >> 24) < 4 && ((code >> 16) & 0xff) < 11)
      m_iDMVersion = 100 * (code >> 24) + 10 * ((code >> 16) & 0xff) + 
        ((code >> 8) & 0xff);
    else
      m_iDMVersion = 10000 * (code >> 24) + 100 * ((code >> 16) & 0xff) + 
        ((code >> 8) & 0xff);
  }
  sprintf(m_strTemp, "retval = %g, code = %x, version = %d\n", retval, code, m_iDMVersion);
  DebugToResult(m_strTemp);
  return m_iDMVersion;
}

/*
 * Set selected shutter normally closed - also set other shutter normally open
 */
int TemplatePlugIn::SetShutterNormallyClosed(long camera, long shutter)
{
  if (m_iDMVersion < SET_IDLE_STATE_OK)
    return 0;
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "Object manager = CM_GetCameraManager()\n"
            "Object cameraList = CM_GetCameras(manager)\n"
            "Object camera = ObjectAt(cameraList, %d)\n"
            "CM_SetIdleShutterState(camera, %d, 1)\n"
            "CM_SetIdleShutterState(camera, %d, 0)\n", camera, shutter, 1 - shutter);
  mTD.strCommand += m_strTemp;
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
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
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetFlyBackTime()\n"
    "Exit(retval)\n");
  mTD.strCommand += m_strTemp;
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
  *flyback = retval;
  m_dFlyback = retval + addedFlyback;
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetLineFrequency()\n"
    "Exit(retval)\n");
  mTD.strCommand += m_strTemp;
  retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
  *lineFreq = retval;
  m_dLineFreq = retval;
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetRotationOffset()\n"
    "Exit(retval)\n");
  mTD.strCommand += m_strTemp;
  retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
  *rotOffset = retval;
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "Number retval = DSGetDoFlip()\n"
    "Exit(retval)\n");
  mTD.strCommand += m_strTemp;
  retval = ExecuteScript((char *)mTD.strCommand.c_str());
  if (retval == SCRIPT_ERROR_RETURN)
    return GENERAL_SCRIPT_ERROR;
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
  mTD.iAntialias = 0;

  // If continuing with continuous, 
  if (continuous < 0) {

    // Return with error if continuous is supposed to be started and isn't
    if (!m_bContinuousDS)
      return CONTINUOUS_ENDED;

    // Timeout since last return of data
    double elapsed = GetTickCount() - mTD.dLastReturnTime;
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

  mTD.strCommand.resize(0);
  mTD.strCommand += "Number exists, xsize, ysize, oldx, oldy, nbytes, idchan, idfirst\n";
  mTD.strCommand += "image imdel, imchan\n";
  mTD.strCommand += "String channame\n";

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
        mTD.strCommand += m_strTemp;
      }
    }
    m_iDSAcquired[chan] = CHAN_UNUSED;
  }

  // Set the parameters for acquisition
  sprintf(m_strTemp, "xsize = %d\n"
    "ysize = %d\n"
    "Number paramID = DSCreateParameters(xsize, ysize, %f, %f, %d)\n", 
    *width, *height, rotation, pixelTime, lineSync ? 1: 0);
  mTD.strCommand += m_strTemp;

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
    mTD.strCommand += m_strTemp;
    if (!m_bGMS2)
      mTD.strCommand += "  ShowImage(imchan)\n";
    else
      mTD.strCommand += "  KeepImage(imchan)\n";
    mTD.strCommand += "}\n"
      "idchan = GetImageID(imchan)\n";
    if (!chan)
      mTD.strCommand += "idfirst = idchan\n";
    sprintf(m_strTemp, "DSSetParametersSignal(paramID, %d, nbytes, 1, idchan)\n", 
      channels[chan]);
    mTD.strCommand += m_strTemp;
    m_iDSAcquired[channels[chan]] = CHAN_ACQUIRED;
  }

  // Acquisition and return commands
  if (!continuous) {
    //j = (int)((fullExpTime + m_iExtraDSdelay) * delayErrorFactor * 0.06 + 0.5);
    if (m_iExtraDSdelay > 0) {

      // With this loop, it doesn't seem to need any delay at the end
      mTD.strCommand += "DSStartAcquisition(paramID, 0, 0)\n"
        "while (DSIsViewActive()) {\n"
        "  Delay(2)\n"
        "}\n";
      //sprintf(m_strTemp, "Delay(%d)\n", j);
      //mTD.strCommand += m_strTemp;
    } else {
      mTD.strCommand += "DSStartAcquisition(paramID, 0, 1)\n";
    }
    mTD.strCommand += "DSDeleteParameters(paramID)\n"
    "Exit(idfirst)\n";

    mTD.bDoContinuous = false;
    again = AcquireAndTransferImage((void *)array, dataSize, arrSize, width, height,
      divideBy2, 0, m_bGMS2 ? DEL_IMAGE : NO_DEL_IM, NO_SAVE);
    if (again != DM_CALL_EXCEPTION)
      m_iDSAcquired[channels[0]] = CHAN_RETURNED;
    return again;
  } else {

    // For continuous acquire, start it, get the parameter ID, 
    mTD.strCommand += "DSStartAcquisition(paramID, 1, 0)\n"
      "Exit(paramID + 1000000. * idfirst)\n";
    double retval = ExecuteScript((char *)mTD.strCommand.c_str());
    if (retval == SCRIPT_ERROR_RETURN)
      return GENERAL_SCRIPT_ERROR;
    mTD.iDSimageID = (int)(retval / 1000000. + 0.5);
    m_iDSparamID = (int)(retval + 0.5 - 1000000. * mTD.iDSimageID);
    m_bContinuousDS = true;
    m_dContExpTime = *width * *height * pixelTime / 1000.;
    for (j = 0; j < *width * *height; j++)
      array[j] = 0;
    mTD.dLastReturnTime = GetTickCount();
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
    return DS_CHANNEL_NOT_ACQUIRED;
  mTD.strCommand.resize(0);
  mTD.iAntialias = 0;
  if (!m_bContinuousDS) {
    sprintf(m_strTemp, "image imzero\n"
      "Number idzero = -1.\n"
      "Number exists = GetNamedImage(imzero, \"SEMchan%d\")\n"
      "if (exists)\n" 
      "  idzero = GetImageID(imzero)\n"
      "Exit(idzero)\n", channel);
    mTD.strCommand += m_strTemp;
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
    return CONTINUOUS_ENDED;
  mTD.strCommand.resize(0);
  sprintf(m_strTemp, "DSStopAcquisition(%d)\n"
      "Delay(%d)\n"
      "DSDeleteParameters(%d)\n"
      "Exit(0)\n", m_iDSparamID, (int)(0.06 * m_iExtraDSdelay + 0.5), m_iDSparamID);
  mTD.strCommand += m_strTemp;
  double retval = ExecuteScript((char *)mTD.strCommand.c_str());
  m_iDSparamID = 0;
  mTD.iDSimageID = 0;
  m_bContinuousDS = false;
  //mTD.dLastReturnTime = GetTickCount();
  return (retval == SCRIPT_ERROR_RETURN) ? GENERAL_SCRIPT_ERROR : 0;
}

// Global instances of the plugin and the wrapper class for calling into this file
TemplatePlugIn gTemplatePlugIn;

PlugInWrapper gPlugInWrapper;

//////////////////////////////////////////////////////////////////////////
// THE WRAPPER FUNCTIONS
//
PlugInWrapper::PlugInWrapper()
{
  mLastRetVal = 0;
}


BOOL PlugInWrapper::GetCameraBusy()
{
  mLastRetVal = 0;
  return false;
}

double PlugInWrapper::ExecuteScript(char *strScript, BOOL selectCamera)
{
  double retVal = gTemplatePlugIn.ExecuteClientScript(strScript, selectCamera);
  mLastRetVal = SCRIPT_ERROR_RETURN ? CLIENT_SCRIPT_ERROR : 0;
  return retVal;
}

void PlugInWrapper::SetDebugMode(int inVal)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetDebugMode(inVal != 0);
}

void PlugInWrapper::SetDMVersion(long inVal)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetDMVersion((int)inVal);
}

void PlugInWrapper::SetCurrentCamera(long inVal)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetCurrentCamera((int)inVal);
}

void PlugInWrapper::QueueScript(char *strScript)
{
  mLastRetVal = 0;
  gTemplatePlugIn.QueueScript(strScript);
}

int PlugInWrapper::GetImage(short *array, long *arrSize, long *width, 
              long *height, long processing, double exposure,
              long binning, long top, long left, long bottom, 
              long right, long shutter, double settling, long shutterDelay,
              long divideBy2, long corrections)
{
  return (mLastRetVal = gTemplatePlugIn.GetImage(array, arrSize, width, height, 
    processing, exposure, binning, top, left, bottom, right, shutter, settling, 
    shutterDelay, divideBy2, corrections));
}

int PlugInWrapper::GetGainReference(float *array, long *arrSize, long *width, 
                  long *height, long binning)
{
  return (mLastRetVal = gTemplatePlugIn.GetGainReference(array, arrSize, width, height, 
    binning));
}


int PlugInWrapper::SelectCamera(long camera)
{
  return (mLastRetVal = gTemplatePlugIn.SelectCamera(camera));
}

void PlugInWrapper::SetReadMode(long mode, double scaling)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetReadMode(mode, scaling);
}

void PlugInWrapper::SetK2Parameters(long readMode, double scaling, long hardwareProc, 
                                    BOOL doseFrac, double frameTime, BOOL alignFrames, 
                                    BOOL saveFrames, long rotationFlip, long flags, 
                                    double dummy1, double dummy2, double dummy3, 
                                    double dummy4, char *filter)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetK2Parameters(readMode, scaling, hardwareProc, doseFrac, frameTime, 
    alignFrames, saveFrames, rotationFlip, flags, dummy1, dummy2, dummy3, dummy4, filter);
}

void PlugInWrapper::SetupFileSaving(long rotationFlip, BOOL filePerImage, 
                                    double pixelSize, long flags, double dummy1, 
                                    double dummy2, double dummy3, double dummy4, 
                                    long *names, long *error)
{
  char *cnames = (char *)names;
  char *command = NULL;
  char *refName = NULL;
  char *defects = NULL;
  char *sumList = NULL;
  int rootind = (int)strlen(cnames) + 1;
  int nextInd = rootind;
  mLastRetVal = 0;
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
  if (flags & K2_SAVE_SUMMED_FRAMES) {
    nextInd += (int)strlen(&cnames[nextInd]) + 1;
    sumList = &cnames[nextInd];
  }
  gTemplatePlugIn.SetupFileSaving(rotationFlip, filePerImage, pixelSize, flags, dummy1,
    dummy2, dummy3, dummy4, cnames, &cnames[rootind], refName, defects, command, sumList,
    error);
}

void PlugInWrapper::GetFileSaveResult(long *numSaved, long *error)
{
  mLastRetVal = 0;
  gTemplatePlugIn.GetFileSaveResult(numSaved, error);
}

int PlugInWrapper::GetDefectList(short xyPairs[], long *arrSize, long *numPoints,
                                 long *numTotal)
{
  return (mLastRetVal = gTemplatePlugIn.GetDefectList(xyPairs, arrSize, numPoints, 
    numTotal));
}

int PlugInWrapper::GetNumberOfCameras()
{
  int retVal = gTemplatePlugIn.GetNumberOfCameras();
  mLastRetVal = 0;
  if (retVal < 0)
    mLastRetVal = GENERAL_SCRIPT_ERROR;
  return retVal;
}

int PlugInWrapper::IsCameraInserted(long camera)
{
  int retVal = gTemplatePlugIn.IsCameraInserted(camera);
  mLastRetVal = 0;
  if (retVal < 0)
    mLastRetVal = GENERAL_SCRIPT_ERROR;
  return retVal;
}

int PlugInWrapper::InsertCamera(long camera, BOOL state)
{
  return (mLastRetVal = gTemplatePlugIn.InsertCamera(camera, state));
}

long PlugInWrapper::GetDMVersion()
{
  int retVal = gTemplatePlugIn.GetDMVersion();
  mLastRetVal = 0;
  if (retVal < 0)
    mLastRetVal = GENERAL_SCRIPT_ERROR;
  return retVal;
}

int PlugInWrapper::SetShutterNormallyClosed(long camera, long shutter)
{
  return (mLastRetVal = gTemplatePlugIn.SetShutterNormallyClosed(camera, shutter));
}

void PlugInWrapper::SetNoDMSettling(long camera)
{
  mLastRetVal = 0;
  gTemplatePlugIn.SetNoDMSettling(camera);
}

int PlugInWrapper::GetDSProperties(long timeout, double addedFlyback, double margin,
                                   double *flyback, 
                                   double *lineFreq, double *rotOffset, long *doFlip)
{
  return (mLastRetVal = gTemplatePlugIn.GetDSProperties(timeout, addedFlyback, margin, 
    flyback, lineFreq, rotOffset, doFlip));
}

int PlugInWrapper::AcquireDSImage(short array[], long *arrSize, long *width, 
                                  long *height, double rotation, double pixelTime, 
                                  long lineSync, long continuous, long numChan, 
                                  long channels[], long divideBy2)
{
  return (mLastRetVal = gTemplatePlugIn.AcquireDSImage(array, arrSize, width, height, 
    rotation, pixelTime, lineSync, continuous, numChan, channels, divideBy2));
}

int PlugInWrapper::ReturnDSChannel(short array[], long *arrSize, long *width, 
                                   long *height, long channel, long divideBy2)
{
  return (mLastRetVal = gTemplatePlugIn.ReturnDSChannel(array, arrSize, width, height, 
    channel, divideBy2));
}

int PlugInWrapper::StopDSAcquisition()
{
  return (mLastRetVal = gTemplatePlugIn.StopDSAcquisition());
}

int PlugInWrapper::StopContinuousCamera()
{
  return (mLastRetVal = gTemplatePlugIn.StopContinuousCamera());
}

void PlugInWrapper::ErrorToResult(const char *strMessage, const char *strPrefix)
{
  ::ErrorToResult(strMessage, strPrefix);
}
void PlugInWrapper::DebugToResult(const char *strMessage, const char *strPrefix)
{
  ::DebugToResult(strMessage, strPrefix);
}
int PlugInWrapper::GetDebugVal()
{
  return gTemplatePlugIn.m_iDebugVal;
}

// Dummy functions for 32-bit.
#ifndef _WIN64
double wallTime(void) { return 0.;}
void overrideWriteBytes(int inVal) {}
void iiDelete(ImodImageFile *inFile) {}
int imodBackupFile(const char *) {return 0;}
#endif
