// TemplatePlugIn.cpp :  The heart of the SerialEMCCD interface to DigitalMicrograph
//
// Copyright (C) 2013-2016 by the Regents of the University of
// Colorado.  See Copyright.txt for full notice of copyright and limitations.
//
// Author: David Mastronarde
//

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

/*#define XSTR(x) STR(x)
#define STR(x) #x */
//#pragma message ( XSTR(GMS_SDK_VERSION) )

#define _GATANPLUGIN_WIN32_DONT_DEFINE_DLLMAIN
#define _GATANPLUGIN_USES_LIBRARY_VERSION 2

#if GMS_SDK_VERSION >= 300
extern "C" int _forceCRTManifestCUR = 0;
extern "C" int _forceMFCManifestCUR = 0;
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

// Include and static instance for framealign
#ifdef _WIN64
#include "Shared\framealign.h"
#include "Shared\CorrectDefects.h"
#include "Shared\frameutil.h"
static FrameAlign sFrameAli;
static CameraDefects sCamDefects;
#endif

#define MAX_TEMP_STRING   1000
#define MAX_FILTER_NAME   64
#define MAX_CAMERAS  10
#define MAX_DS_CHANNELS 8
#define ID_MULTIPLIER 10000000
#define SUPERRES_FRAME_SCALE 16
#define SUPERRES_PRESERVE_SHIFT 8
#define SUPERRES_PRESERVE_SCALE (1 << SUPERRES_PRESERVE_SHIFT)
#define DATA_MUTEX_WAIT  100
#define FRAME_EVENT_WAIT  200
#define IMAGE_MUTEX_WAIT  1000
#define IMAGE_MUTEX_CLEAN_WAIT  5000
#define DFACQUIRE_MUTEX_WAIT 10000
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

// THE debug mode flag and an integer that came through environment variable
static BOOL sDebug;
static int sEnvDebug;

// Handle for mutexes for writing critical components of thread data and continuous image
static HANDLE sDataMutexHandle;
static HANDLE sImageMutexHandle;
static HANDLE sDFAcquireMutexHandle;
static HANDLE sFrameReadyEvent = NULL;
static int sJ;

// Array for storing continuously acquired images to pass from thread to caller
static short *sContinuousArray = NULL;

// Array for leaving the deferred sum after early return
static short *sDeferredSum = NULL;
static bool sValidDeferredSum = false;

// Static data about loaded gain refs that needs to be set by the threads
static float *sK2GainRefData[2] = {NULL, NULL};
static double sK2GainRefTime[2];
static int sK2GainRefWidth[2];
static int sK2GainRefHeight[2];

// Other items that need to be saved by thread and accessible in main
static string sLastRefName;
static string sLastRefDir;
static float sWriteScaling;
static float sLastSaveFloatScaling;
static bool sLastSaveNeededRef;

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
  float fSavedScaling;
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
  string strAlignComName;
  string strLastDefectName;
  bool bLastSaveHadDefects;

  // New non-internal items
  int iNumFramesToSum;
  int iNumSummed;
  int iNumGrabAndStack;
  bool bEarlyReturn;
  bool bAsyncToRAM;
  bool isTDcopy;
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
  bool isSuperRes;
  bool isCounting;
  int iAntialias;
  int iFinalBinning;
  int iFinalWidth;
  int iFinalHeight;
  int iFullSizeX;
  int iFullSizeY;
  bool bGainNormSum;
  float fGainNormScale;
  bool bSaveSummedFrames;
  bool bMakeSubarea;
  vector<short> outSumFrameList;
  vector<short> numFramesInOutSum;
  int outSumFrameIndex;
  int numAddedToOutSum;
  int numOutSumsDoneAtIndex;
  int iExpectedFrames;
  bool bUseFrameAlign;
  bool bMakeAlignComFile;
  bool bFaKeepPrecision;
  bool bFaOutputFloats;
  int iGrabByteSize;
  int iFaGpuFlags;
  int iFaDataMode;
  bool bMakeDeferredSum;
  bool bCorrectDefects;
  bool bFaMakeEvenOdd;
  int iFaCamSizeX, iFaCamSizeY;
  int iFaAliBinning;
  int iFaNumFilters;
  float fFaRadius2[4];
  float fFaSigmaRatio;
  float fFaTruncLimit;
  float fAlignScaling;
  int iFaDeferGpuSum, iFaSmoothShifts, iFaGroupRefine, iFaHybridShifts;
  int iFaNumAllVsAll, iFaGroupSize, iFaShiftLimit, iFaAntialiasType, iFaRefineIter;
  float fFaStopIterBelow;
  float fFaRefRadius2;
  float fFaRawDist, fFaSmoothDist, fFaResMean, fFaMaxResMax, fFaMeanRawMax;
  float fFaMaxRawMax, fFaCrossHalf, fFaCrossQuarter, fFaCrossEighth, fFaHalfNyq;
  
  // Items needed internally in save routine and its functions
  FILE *fp;
  ImodImageFile *iifile;
  short *outData, *rotBuf, *tempBuf, *outSumBuf;
  int *sumBuf;
  void **grabStack;
  MrcHeader hdata;
  int fileMode, outByteSize, byteSize;
  bool isInteger, isUnsignedInt, isFloat, signedBytes, save4bit;
  int refCopyReturnVal;
  double curRefTime;
};

// Framealign Results may be put in main or copy, so put pointer to them here
static ThreadData *sTDwithFaResult = NULL;

// Local functions callable from thread
static DWORD WINAPI AcquireProc(LPVOID pParam);
static void  ProcessImage(void *imageData, void *array, int dataSize, long width, 
                          long height, long divideBy2, long transpose, int byteSize, 
                          bool isInteger, bool isUnsignedInt, float floatScaling);
static int AlignOrSaveImage(ThreadData *td, short *outForRot, bool saveImage, 
  bool alignFrame, int slice, bool finalFrame, int &fileSlice, int &tmin, int &tmax,
  float &meanSum, double &procWall, double &saveWall, double &alignWall, 
  double &wallStart);
static int PackAndSaveImage(ThreadData *td, void *array, int nxout, int nyout, int slice, 
                            bool finalFrame, int &fileSlice, int &tmin,
                            int &tmax, float &meanSum);
static int GetTypeAndSizeInfo(ThreadData *td, DM::Image &image, int loop, int outLimit,
                              bool doingStack);
static int InitOnFirstFrame(ThreadData *td, bool needTemp, bool needSum, 
                            long &frameDivide, bool &needProc);
static int InitializeFrameAlign(ThreadData *td);
static int FinishFrameAlign(ThreadData *td, short *procOut, int numSlice);
static int WriteAlignComFile(ThreadData *td, string inputFile, bool ifMdoc);
static void RotateFlip(short int *array, int mode, int nx, int ny, int operation, 
                       bool invert, short int *brray, int *nxout, int *nyout);
static void AddToSum(ThreadData *td, void *data, void *sumArray);
static void AccumulateWallTime(double &cumTime, double &wallStart);
static void DeleteImageIfNeeded(ThreadData *td, DM::Image &image, bool *needsDelete);
static void SetWatchedDataValue(int &member, int value);
static int GetWatchedDataValue(int &member);
static double TickInterval(double start);
static int CopyK2ReferenceIfNeeded(ThreadData *td);
static int LoadK2ReferenceIfNeeded(ThreadData *td, bool sizeMustMatch, string &errStr);
static int CheckK2ReferenceTime(ThreadData *td);
static void DebugToResult(const char *strMessage, const char *strPrefix = NULL);
static void ErrorToResult(const char *strMessage, const char *strPrefix = NULL);
static void ProblemToResult(const char *strMessage);
static void framePrintFunc(const char *strMessage);
static BOOL SleepMsg(DWORD dwTime_ms);
static double ExecuteScript(char *strScript);
static int RunContinuousAcquire(ThreadData *td);
static void SubareaAndAntialiasReduction(ThreadData *td, void *array);
static void GainNormalizeSum(ThreadData *td, void *array);
static void ExtractSubarea(ThreadData *td, void *inArray, int iTop, int iLeft, 
  int iBottom, int iRight, void *outArray, long &width, long &height);
static int RelativePath(string fromDir, string toDir, string &relPath);
static int StandardizePath(string &dir);
static void SplitExtension(const string &file, string &root, string &ext);
static void SplitFilePath(const string &path, string &dir, string &file);
static int WriteTextFile(const char *filename, const char *text, int length, 
  int openErr, int writeErr, bool asBinary);

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
  int IsGpuAvailable(long gpuNum, double *gpuMemory);
  void SetupFrameAligning(long aliBinning, double rad2Filt1, 
    double rad2Filt2, double rad2Filt3, double sigma2Ratio, 
    double truncLimit, long alignFlags, long gpuFlags, long numAllVsAll, long groupSize, 
    long shiftLimit, long antialiasType, long refineIter, double stopIterBelow, 
    double refRad2, long nSumAndGrab, long dumInt1, long dumInt2, double dumDbl1, 
    char *refName, char *defects, char *comName, long *error);
  void FrameAlignResults(double *rawDist, double *smoothDist, 
    double *resMean, double *maxResMax, double *meanRawMax, double *maxRawMax, 
    double *crossHalf, double *crossQuarter, double *crossEighth, double *halfNyq, 
    long *dumInt1, double *dumDbl1, double *dumDbl2, double *dumDbl3);
  void MakeAlignComFile(long flags, long dumInt1, double dumDbl1, 
    double dumDbl2, char *mdocName, char *mdocFileOrText, long *error);
  int ReturnDeferredSum(short array[], long *arrSize, long *width, long *height); 
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
  void FreeK2GainReference(long which);
  void ClearSpecialFlags() {mTD.iAntialias = 0; mTD.bGainNormSum = false;
    mTD.bMakeSubarea = false;};
  int ManageEarlyReturn(int flags, int iSumAndGrab);
  virtual void Start();
  virtual void Run();
  virtual void Cleanup();
  virtual void End();
  TemplatePlugIn();
  
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
  int m_iGpuAvailable;
  bool m_bDefectsParsed;
  bool m_bNextSaveResultsFromCopy;
};

TemplatePlugIn::TemplatePlugIn()
{
  sDebug = getenv("SERIALEMCCD_DEBUG") != NULL;
  sEnvDebug = 0;
  if (sDebug)
    sEnvDebug = atoi(getenv("SERIALEMCCD_DEBUG"));

  sDataMutexHandle = CreateMutex(0, 0, 0);
  sImageMutexHandle = CreateMutex(0, 0, 0);
#if defined(_WIN64) && GMS_SDK_VERSION < 31
  sDFAcquireMutexHandle = CreateMutex(0, 0, 0);
#endif
#ifdef _WIN64
  b3dSetStoreError(1);
  sFrameAli.setPrintFunc(framePrintFunc);
#endif
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
  mTD.bUseFrameAlign = false;
  m_bDefectsParsed = false;
  m_iGpuAvailable = -1;
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

static void ProblemToResult(const char *strMessage)
{
  ErrorToResult(strMessage, "\nA problem occurred acquiring an image for SerialEM:\n");
}

static void framePrintFunc(const char *strMessage)
{
  ErrorToResult(strMessage, "SerialEMCCD framealign : ");
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
  int newProc, err, procIn, binAdj, heightAdj, widthAdj, scaleAdj;
  bool swapXY = mTD.iRotationFlip > 0 && mTD.iRotationFlip % 2 && m_bDoseFrac;
  string errStr, aliHead;

  // Process flags to do with saving/aligning for consistency
  if (m_bSaveFrames && mTD.iReadMode >= 0 && m_bDoseFrac && mTD.strSaveDir.length() && 
    mTD.strRootName.length())
      saveFrames = SAVE_FRAMES;
  if (saveFrames == NO_SAVE)
    mTD.bMakeAlignComFile = false;
  if (!(mTD.iReadMode >= 0 && m_bDoseFrac))
    mTD.bUseFrameAlign = false;
  if (m_bDoseFrac)
    mTD.iExpectedFrames = B3DNINT(exposure / mTD.dFrameTime);
  if (mTD.iExpectedFrames < 2)
    mTD.bUseFrameAlign = false;
  if (saveFrames == NO_SAVE && !mTD.bUseFrameAlign) {
    mTD.bEarlyReturn = false;
    mTD.bMakeDeferredSum = false;
  }
  if (mTD.bUseFrameAlign && mTD.bEarlyReturn)
    mTD.bMakeDeferredSum = true;
  if (saveFrames == SAVE_FRAMES)
    sLastSaveFloatScaling = mTD.fFloatScaling / (divideBy2 ? 2.f : 1.f);

  // Set flag to keep precision when indicated or when it has no cost
  mTD.bFaKeepPrecision = (!mTD.iNumGrabAndStack || mTD.bFaKeepPrecision) && 
    mTD.bUseFrameAlign && sReadModes[mTD.iReadMode] != K2_LINEAR_READ_MODE && 
    processing == GAIN_NORMALIZED;
  m_bNextSaveResultsFromCopy = false;

  // Check validity of making a com file
  if (mTD.bMakeAlignComFile) { 
    if (mTD.bFilePerImage || mTD.bUseFrameAlign) {
      sprintf(m_strTemp, "You cannot make an align com file when %s\n",
        mTD.bFilePerImage ? "saving one frame per file" : "aligning in the plugin");
      ProblemToResult(m_strTemp);
      return MAKECOM_BAD_PARAM;
    }
    SplitFilePath(mTD.strAlignComName, aliHead, errStr);
    if (RelativePath(aliHead, mTD.strSaveDir, errStr)) {
      sprintf(m_strTemp, "The command file %s and save directory %s are not on the same "
        "drive", mTD.strAlignComName, mTD.strSaveDir);
      ProblemToResult(m_strTemp);
      return MAKECOM_NO_REL_PATH;
    }
  }

  // Strip continuous mode information from processing, set flag to do it if no conflict
  mTD.bDoContinuous = false;
  if (processing > 7) {
    procIn = processing;
    processing &= 7;
    if (saveFrames == NO_SAVE && !mTD.bUseFrameAlign) {
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

  // Give up on an existing deferred sum for a new dose frac shot or starting continuous
  if ((m_bDoseFrac || mTD.bDoContinuous) && (sDeferredSum || sValidDeferredSum)) {
    delete [] sDeferredSum;
    sDeferredSum = NULL;
    sValidDeferredSum = false;
  }

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

  // Gain normalize the return sum if flag is set and saving dark-subtracted counting or
  // super-res frames, and there is a gain reference name set, or if aligning such frames
  // and doing a nin-empty early return
  mTD.bGainNormSum = ((mTD.bUseFrameAlign && mTD.bEarlyReturn && mTD.iNumFramesToSum) ||
    ((mTD.iSaveFlags & K2_GAIN_NORM_SUM) != 0 && saveFrames == SAVE_FRAMES)) &&
    newProc == NEWCM_DARK_SUBTRACTED && mTD.iReadMode > K2_LINEAR_READ_MODE && 
    !mTD.strGainRefToCopy.empty();
  if (mTD.bGainNormSum) {
    scaleAdj = (divideBy2 ? 2 : 1) * 
      B3DNINT(B3DMAX(1., mTD.fFloatScaling / (divideBy2 ? 2 : 1)));
    mTD.fGainNormScale = (float)(mTD.fFloatScaling / scaleAdj);
    mTD.fFloatScaling = (float)scaleAdj;
  }

  // single frame doesn't work in older GMS for async saving; but don't know about newer
  mTD.iK2Processing = newProc;
  if (saveFrames == SAVE_FRAMES && mTD.iExpectedFrames == 1 && GMS_SDK_VERSION < 31)
    mTD.bAsyncSave = false;

  // Also cancel asynchronous save if aligning and saving frames since we need to use
  // the old API, and that is set up to happen only through script calls
  mTD.bUseOldAPI = saveFrames == SAVE_FRAMES && mTD.bAlignFrames && 
    GMS_SDK_VERSION >= 31;
  if (mTD.bUseOldAPI)
    mTD.bAsyncSave = false;

  // Get binning and width/height needed to test if final size is correct
  binAdj = binning / (sReadModes[mTD.iReadMode] == K2_SUPERRES_READ_MODE ? 2 : 1);
  heightAdj = swapXY ? right - left : bottom - top;
  widthAdj = swapXY ? bottom - top : right - left;
  mTD.iFinalBinning = binning;

  // For antialias reduction here, make sure it is allowed and set binning to 1
  if (mTD.iReadMode >= 0 && mTD.iAntialias && !(!mTD.bUseFrameAlign && mTD.bEarlyReturn &&
    !mTD.iNumFramesToSum && !mTD.bMakeDeferredSum) && 
    !(mTD.bUseFrameAlign && (!mTD.bEarlyReturn || !mTD.iNumFramesToSum))) {
      if (binning == 1 || mTD.iFinalWidth * binAdj > widthAdj || 
        mTD.iFinalHeight * binAdj > heightAdj || mTD.bDoContinuous) {
          if (mTD.bDoContinuous)
            sprintf(m_strTemp,"Attempting to use antialias reduction in continuous "
            "mode\n");
          else
            sprintf(m_strTemp, "Bad parameters for antialiasing: from %d x %d to %d x %d "
            "binning by %d\n", widthAdj, heightAdj, mTD.iFinalWidth, mTD.iFinalHeight, 
            binAdj);
          ProblemToResult(m_strTemp);
          return BAD_ANTIALIAS_PARAM;
      }
      binning = 1;
      DebugToResult("Set up antialias");
  } else
    mTD.iAntialias = 0;

  if (mTD.bUseFrameAlign) {
    
    // For reduced framealign image, check final sizes here
    if (binning > 1 && (mTD.iFinalWidth > widthAdj / binAdj || 
      mTD.iFinalHeight > heightAdj / binAdj)) {
        sprintf(m_strTemp, "Bad parameters for final width after frame align with "
          "reduction:\n reduction expected to be %d x %d, final width %d x %d\n",
          widthAdj / binAdj, heightAdj / binAdj, mTD.iFinalWidth, mTD.iFinalHeight);
        ProblemToResult(m_strTemp);
        return BAD_FRAME_REDUCE_PARAM;
    }

    // Make sure gain reference is available if needed
    if (newProc == NEWCM_DARK_SUBTRACTED && mTD.iReadMode > K2_LINEAR_READ_MODE && 
      !mTD.strGainRefToCopy.empty() && LoadK2ReferenceIfNeeded(&mTD, false, errStr)) {
        sprintf(mTD.strTemp, "%s\n", errStr.c_str());
        ErrorToResult(mTD.strTemp);
        return GAIN_REF_LOAD_ERROR;
    }
    binning = 1;
  }

  if (mTD.bMakeSubarea) {
    mTD.iFullSizeX = (mTD.iFullSizeX + binning - 1) / binning;
    mTD.iFullSizeY = (mTD.iFullSizeY + binning - 1) / binning;
  }

  // Intercept K2 asynchronous saving and continuous mode here
  mTD.iTop = top;
  mTD.iBottom = bottom;
  mTD.iLeft = left;
  mTD.iRight = right;
  if (((saveFrames == SAVE_FRAMES || mTD.bUseFrameAlign) && mTD.bAsyncSave) || 
    mTD.bDoContinuous) {
      sprintf(m_strTemp, "Exit(0)");
      mTD.strCommand += m_strTemp;
      mTD.dK2Exposure = exposure;
      mTD.dK2Settling = settling;
      mTD.iK2Shutter = shutter;
      mTD.iK2Binning = binning;
      mTD.iCorrections = corrections;

      // Make call to acquire image or start continuous acquires
      err = AcquireAndTransferImage((void *)array, 2, arrSize, width, height,
        divideBy2, 0, DEL_IMAGE, saveFrames);
      ClearSpecialFlags();

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
    if (!m_bDoseFrac && !mTD.bMakeSubarea) {
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

  // A read mode of -3 or -2 means set mode 0 for regular or 1 for diffraction
  if (mTD.iReadMode == -2 || mTD.iReadMode == -3) {
    sprintf(m_strTemp, "CM_SetReadMode(acqParams, %d)\n", 3 + mTD.iReadMode);
    mTD.strCommand += m_strTemp;
  }
  
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
  ClearSpecialFlags();

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
  ClearSpecialFlags();
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
  bool hasMutex = false;
  ThreadData *retTD = &mTD;
  bool useFinal;
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
  if (!retval && (mTD.bDoContinuous || ((saveFrames || mTD.bUseFrameAlign) && 
    (mTD.bAsyncSave || (mTD.bEarlyReturn && GMS_SDK_VERSION < 31))))) {
      *arrSize = *width = *height = 0;
      mTDcopy = mTD;
      mTDcopy.isTDcopy = true;
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
      useFinal = mTDcopy.iAntialias || mTDcopy.bMakeSubarea || mTDcopy.bUseFrameAlign;
      retWidth = useFinal ? retTD->iFinalWidth : retTD->width;
      retHeight = useFinal ? retTD->iFinalHeight : retTD->height;
      sprintf(m_strTemp, "Back from thread, retval %d errfs %d  #saved %d w %d h %d\n",
        retval, mTD.iErrorFromSave, mTD.iFramesSaved, retWidth, retHeight);
      DebugToResult(m_strTemp);
      if (!retval && !retTD->arrSize && retTD->iNumFramesToSum)
        retTD->arrSize = retWidth * retHeight;
  } else if (!retval) {

    // Acquire a mutex to prevent thread from finishing up during a single shot
#if defined(_WIN64) && GMS_SDK_VERSION < 31
    if (m_HAcquireThread) {
      WaitForSingleObject(sDFAcquireMutexHandle, DFACQUIRE_MUTEX_WAIT);
      hasMutex = true;
    }
#endif
    retval = AcquireProc(&mTD);
 #if defined(_WIN64) && GMS_SDK_VERSION < 31
    if (hasMutex)
      ReleaseMutex(sDFAcquireMutexHandle);
#endif
    useFinal = mTD.iAntialias || mTD.bMakeSubarea || mTD.bUseFrameAlign;
    retWidth = useFinal ? mTD.iFinalWidth : mTD.width;
    retHeight = useFinal ? mTD.iFinalHeight : mTD.height;
    /*sprintf(m_strTemp, "Back from call, retval %d w %d h %d  arrsize %d\n",
        retval, retWidth, retHeight, mTD.arrSize);
    DebugToResult(m_strTemp);*/
  }
  *width = retWidth;
  *height = retHeight;
  if (!retval && !retTD->arrSize)
    *arrSize = B3DMIN(1024, sizeOrig);
  else if (!retval && (retTD->iAntialias || retTD->bMakeSubarea || retTD->bUseFrameAlign))
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
  bool doingStack, needProc, needTemp, needSum, exposureDone, copiedToProc;
  bool stackAllReady, doingAsyncSave, frameNeedsDelete = false;
  double retval, procWall, saveWall, getFrameWall, wallStart, alignWall;
  int fileSlice, tmin, tmax, numSlices, grabInd, grabSlice;
  float meanSum; 
  bool saveOrFrameAlign = saveFrames || td->bUseFrameAlign;
  bool useOldAPI = GMS_SDK_VERSION < 31 || td->bUseOldAPI;
  bool alignBeforeProc = td->bFaKeepPrecision && !td->iNumGrabAndStack;
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
  td->fSavedScaling = td->fFloatScaling;
  doingAsyncSave = saveOrFrameAlign && td->bAsyncSave;
  exposureDone = !doingAsyncSave;
  needTemp = saveOrFrameAlign && td->bEarlyReturn;
  needSum = (saveFrames && 
    ((td->iNumFramesToSum != 0 && (!useOldAPI || (useOldAPI && td->bEarlyReturn))) || 
    (!useOldAPI && td->bMakeDeferredSum))) ||
    (td->bUseFrameAlign && td->bEarlyReturn && td->iNumFramesToSum != 0); 
  td->iifile = NULL;
  td->fp = NULL;
  td->rotBuf = td->tempBuf = NULL;
  td->sumBuf = NULL;
  td->outSumBuf = NULL;
  td->numAddedToOutSum = 0;
  td->outSumFrameIndex = 0;
  td->numOutSumsDoneAtIndex = 0;

  // Get array for unbinned image with antialias reduction, or for subarea
  if (td->iAntialias || td->bMakeSubarea || td->bUseFrameAlign) {
    try {
      i = 1;
      if (td->iReadMode >= 0)
        i = td->isSuperRes ? 4 : 1;
      if (td->bMakeSubarea)
        outLimit = td->iFullSizeX * td->iFullSizeY * i;
      else
        outLimit = (td->iRight - td->iLeft) * (td->iBottom - td->iTop) * i;
      if (td->iAntialias || td->bMakeSubarea || !needTemp) {
        unbinnedArray = new short[outLimit];
        array = unbinnedArray;
      }
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
  } else if (saveOrFrameAlign) {

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
        if (td->bUseFrameAlign)
          errorRet = td->iErrorFromSave;
        ID = imageID;
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
    doingStack = (saveOrFrameAlign) && !loop;
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

      if (!doingStack && !(saveFrames && (needSum || td->bEarlyReturn)) && 
        !td->bUseFrameAlign) {
          j = 20;

          // REGULAR OLD IMAGE

          // Sets byteSize, isInteger, isUnsignedInt, isFloat, signedBytes and 
          // width/height
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
          GainNormalizeSum(td, array);
          SubareaAndAntialiasReduction(td, array);
          delete imageLp;
          imageLp = NULL;

      } else if (doingStack) {
        j = 30;

        // STACK PROCESSING : check dimensions for an image

        if (GMS_SDK_VERSION < 31 || !td->bAsyncSave) {
          numDim = DM::ImageGetNumDimensions(image.get());   j++;
          if (numDim < 3) {
            td->iErrorFromSave = STACK_NOT_3D;
            if (td->bUseFrameAlign)
              errorRet = td->iErrorFromSave;
            continue;
          }
          stackAllReady = !td->bAsyncSave;
          numSlices = td->bAsyncSave ? 0 : DM::ImageGetDimensionSize(image.get(), 2);   j++;
        }

        procWall = saveWall = getFrameWall = alignWall = 0.;
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
              if (td->bUseFrameAlign)
                errorRet = td->iErrorFromSave;
              break;
            }
            j++;

            // Sets frameDivide, outByteSize, fileMode, save4bit and copies gain reference
            if (InitOnFirstFrame(td, needTemp, needSum, frameDivide, needProc)) {
              if (td->bUseFrameAlign)
                errorRet = td->iErrorFromSave;
              break;
            }
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
                  divideBy2, transpose, 4, !td->isFloat, false, td->fSavedScaling);
                DebugToResult("Partial sum completed by thread\n");
                GainNormalizeSum(td, array);
                SubareaAndAntialiasReduction(td, array);
            }
            td->iNumSummed++;
            if (exposureDone && !td->iReadyToReturn && 
              td->iNumSummed >= td->iNumFramesToSum)
                SetWatchedDataValue(td->iReadyToReturn, 1);
          } else if (needSum && !td->bUseFrameAlign && td->bMakeDeferredSum) {

            // Keep adding into sumBuf if not doing frame align and deferred sum is wanted
            AddToSum(td, imageData, td->sumBuf);
          }

          // For frame alignment with high precision data, pass the float frame for
          // alignment now if there is no grabbing at all or if it is counting
          if (alignBeforeProc) {
            i = AlignOrSaveImage(td, td->outData, false, true, slice, false, fileSlice,
              tmin, tmax, meanSum, procWall, saveWall, alignWall, wallStart);
            if (i) {
              errorRet = td->iErrorFromSave;
              break;
            }
          }

          // If grabbing frames at this point, allocate the frame, copy over if not proc
          procOut = td->tempBuf;
          copiedToProc = false;
          grabInd = slice - (numSlices - td->iNumGrabAndStack);
          if (grabInd >= 0) {
            try {
              td->grabStack[grabInd] = new char[td->width * td->height * 
                td->iGrabByteSize];
            }
            catch (...) {
              td->grabStack[grabInd] = NULL;
              td->iErrorFromSave = ROTBUF_MEMORY_ERROR;
              if (td->bUseFrameAlign)
                errorRet = td->iErrorFromSave;
              break;
            }
            procOut = (short *)td->grabStack[grabInd];

            // Copy the array over either if no processing is needed or if doing high 
            // precision counting mode align
            if (!needProc || (td->bFaKeepPrecision && td->isCounting)) {
              DebugToResult("Copying array to procOut\n");
              memcpy(procOut, imageData, td->width * td->height * td->iGrabByteSize);
              copiedToProc = true;
            }
          }

          // Process a float image (from software normalized frames) or other image that
          // needs scaling or conversion
          // It goes from the DM array into the passed or temp array or grab stack
          if (needProc && (!alignBeforeProc || td->saveFrames) && !copiedToProc) {
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

            i = AlignOrSaveImage(td, outForRot, td->saveFrames, 
              td->bUseFrameAlign && !alignBeforeProc, slice, 
              stackAllReady && slice == numSlices - 1, fileSlice, tmin, tmax, meanSum,
              procWall, saveWall, alignWall, wallStart);
            if (td->iErrorFromSave == FRAMEALI_NEXT_FRAME)
              errorRet = td->iErrorFromSave;

            // Save the image to file; keep going on error if need a sum
            if (errorRet || (i && !(needSum && slice < td->iNumFramesToSum)))
              break;

          }

          // Increment slice, clean up image at end of slice loop
          slice++;
          delete imageLp;
          imageLp = NULL;
          DeleteImageIfNeeded(td, image, &frameNeedsDelete);
        } while (slice < numSlices || !stackAllReady);  // End of slice loop

        DeleteImageIfNeeded(td, image, &frameNeedsDelete);

        // If there are stacked frames, align and/or rotate/flip and save them
        if (!td->iErrorFromSave && td->iNumGrabAndStack) {

          // Signal that we are done with the DM stack and then process
          DebugToResult("Done with stack, processing grabbed frames\n");
          SetWatchedDataValue(td->iReadyToAcquire, 1);
          for (grabInd = 0; grabInd < td->iNumGrabAndStack; grabInd++) {
            td->outData = (short *)td->grabStack[grabInd];
            i = AlignOrSaveImage(td, outForRot, td->saveFrames, td->bUseFrameAlign,
              grabSlice + grabInd, grabInd == td->iNumGrabAndStack - 1, fileSlice, tmin,
              tmax, meanSum, procWall, saveWall, alignWall, wallStart);
            if (td->iErrorFromSave == FRAMEALI_NEXT_FRAME)
              errorRet = td->iErrorFromSave;
            if (i)
              break;
            delete [] td->grabStack[grabInd];
            td->grabStack[grabInd] = NULL;
          }
        }
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
    td->fFloatScaling = td->fSavedScaling;

#ifdef _WIN64
    if (doingStack && !errorRet) {

      // For a deferred sum, simply direct the output to the temp buf and make it the 
      // deferred sum array
      procOut = (short *)(td->bUseFrameAlign ? td->array : array);
      if (td->bMakeDeferredSum) {
        procOut = td->tempBuf;
        sDeferredSum = procOut;
        td->tempBuf = NULL;
     }

      // Return the full sum here after all temporary use of array is over
      if (td->bUseFrameAlign) {
        i = FinishFrameAlign(td, procOut, slice);
        if (i)
          errorRet = td->iErrorFromSave;
        AccumulateWallTime(alignWall, wallStart);
      } else if (needSum && (!td->bEarlyReturn || td->bMakeDeferredSum)) {
        ProcessImage(td->sumBuf, procOut, td->dataSize, td->width, td->height,
          divideBy2, transpose, 4, !td->isFloat, false, td->fSavedScaling);
        GainNormalizeSum(td, procOut);
        if (td->iAntialias && td->bMakeDeferredSum) {
          sDeferredSum = (short*)td->sumBuf;
          td->array = sDeferredSum;
          td->sumBuf = NULL;
        }
        SubareaAndAntialiasReduction(td, procOut);
        if (td->iAntialias && td->bMakeDeferredSum) 
          td->array = NULL;
      }
      if (td->bMakeDeferredSum) {
        if (errorRet && sDeferredSum) {
          delete [] sDeferredSum;
          sDeferredSum = NULL;
        } else if (!errorRet) {
          sValidDeferredSum = true;
        }
      }

      sprintf(td->strTemp, "Processing %.3f   saving %.3f   getting frame  %.3f   "
        "aligning %.3f sec\n", procWall, saveWall, getFrameWall, alignWall);
      if (!td->iErrorFromSave)
        DebugToResult(td->strTemp);

      if (!td->iErrorFromSave && td->bMakeAlignComFile) {
        td->iErrorFromSave = WriteAlignComFile(td, 
          td->strRootName + (td->bWriteTiff ? ".tif" : ".mrc"), false);
      }
    }

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

  // Acquire mutex so that final cleanup does not occur during a single-shot
#if defined(_WIN64) && GMS_SDK_VERSION < 31
  if (td->isTDcopy) 
    WaitForSingleObject(sDFAcquireMutexHandle, DFACQUIRE_MUTEX_WAIT);
#endif

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
#ifdef _WIN64
  if (td->bUseFrameAlign)
    sFrameAli.cleanup();
#endif
#if defined(_WIN64) && GMS_SDK_VERSION < 31
  delete k2dfaP;
  if (td->isTDcopy) 
    ReleaseMutex(sDFAcquireMutexHandle);
#endif

  delete unbinnedArray;

  td->arrSize = (td->iAntialias || td->bMakeSubarea || td->bUseFrameAlign) ? 
    td->iFinalWidth * td->iFinalHeight : td->width * td->height;
  sprintf(td->strTemp, "Leaving acquire %s with return value %d arrSize %d\n",
    td->isTDcopy ? "thread" : "proc", errorRet, td->arrSize); 
  DebugToResult(td->strTemp);
  return errorRet;
}

// Do antialias reduction if it is called for
static void SubareaAndAntialiasReduction(ThreadData *td, void *array)
{
  long width = td->width;
  long height = td->height;
  if (td->bMakeSubarea) {
    ExtractSubarea(td, array, td->iTop, td->iLeft, td->iBottom, td->iRight, 
      (short *)(td->iAntialias ? array : td->array), width, height);
    sprintf(td->strTemp, "Subarea extraction from %d x %d to %d x %d\n",
      td->width, td->height, width, height);
    DebugToResult(td->strTemp);
  }
#ifdef _WIN64
  int error, i, xStart, yStart, nxr, nyr;
  int type = td->divideBy2 ? SLICE_MODE_SHORT : SLICE_MODE_USHORT;
  if (!td->iAntialias)
    return;
  double filtScale = 1. / td->iFinalBinning;
  sprintf(td->strTemp, "AntialiasReduction from %d x %d bin %d to %d x %d\n",
    width, height, td->iFinalBinning, td->iFinalWidth, td->iFinalHeight);
  DebugToResult(td->strTemp);
  unsigned char **linePtrs = makeLinePointers(array, width, height, 2);

  // Set the offset to center a subset, get filter, and do it
  float axoff = (float)(B3DMAX(0, width - td->iFinalWidth * td->iFinalBinning) / 2.);
  float ayoff = (float)(B3DMAX(0, height - td->iFinalHeight * td->iFinalBinning) /2.);
  int filterType = B3DMIN(6, td->iAntialias) - 1;
  error = selectZoomFilter(filterType, filtScale, &i);
  if (!error && linePtrs) {
    setZoomValueScaling((float)(td->iFinalBinning * td->iFinalBinning));
    error = zoomWithFilter(linePtrs, width, height, axoff, ayoff, 
      td->iFinalWidth, td->iFinalHeight, td->iFinalWidth, 0, type,
      td->array, NULL, NULL);
  }
  B3DFREE(linePtrs);

  // It is inconvenient to have errors, so fall back to binning
  if (error) {
    xStart = B3DNINT(axoff);
    yStart = B3DNINT(ayoff);
    extractWithBinning(array, type, width, xStart, width - xStart - 1, yStart,
      height - yStart - 1, td->iFinalBinning, td->array, 0, &nxr, &nyr);
    ProblemToResult("Warning: an error occurred in antialias reduction, binning was used "
      "instead\n");
  }
#endif
}

// Simple routine to extract subarea and adjust the passed width and height values
static void ExtractSubarea(ThreadData *td, void *inArray, int iTop, int iLeft, 
  int iBottom, int iRight, void *outArray, long &width, long &height)
{
  int ix, iy;
  int bottom = B3DMIN(iBottom, height);
  int right = B3DMIN(iRight, width);
  short *outArr = (short *)outArray;
  for (iy = iTop; iy < bottom; iy++) {
    short *line = ((short *)inArray) + iy * width;
    for (ix = iLeft; ix < right; ix++)
      *outArr++ = line[ix];
  }
  width = right - iLeft;
  height = bottom - iTop;
  if (!td->iAntialias) {
    td->iFinalWidth = width;
    td->iFinalHeight = height;
  }
}

// Gain normalize the return sum from a dark-subtracted dose frac shot
static void GainNormalizeSum(ThreadData *td, void *array)
{
#ifdef _WIN64
  int refInd = td->isSuperRes ? 1 : 0;
  int ind, error = 0;
  int iVal;
  short *sData = (short *)array;
  unsigned short *usData = (unsigned short *)array;
  float *refData;
  string errStr;
  if (!td->bGainNormSum)
    return;
  if (td->refCopyReturnVal) {
    error = 1;
    errStr = "there was a problem copying the gain reference";
  } else {
    error = LoadK2ReferenceIfNeeded(td, true, errStr);
  }

  // Here, check that size matches for an existing reference
  if (!error && (td->width != sK2GainRefWidth[refInd] || 
    td->height != sK2GainRefHeight[refInd]))
      error = 3;
  if (error == 3) {
    sprintf(td->strTemp, "gain reference size (%d x %d) is not the same as the "
      "image size (%d x %d)", sK2GainRefWidth[refInd], sK2GainRefHeight[refInd], 
      td->width, td->height);
    errStr = td->strTemp;
  }

  // Normalize at last; no more errors
  if (!error) {
    refData = sK2GainRefData[refInd];
    if (td->divideBy2) {
      for (ind = 0; ind < td->width * td->height; ind++) {
        iVal = (int)(sData[ind] * refData[ind] * td->fGainNormScale + 0.5);
        sData[ind] = B3DMAX(-32768, B3DMIN(32767, iVal));
      }
    } else {
      for (ind = 0; ind < td->width * td->height; ind++) {
        iVal = (int)(usData[ind] * refData[ind] * td->fGainNormScale + 0.5);
        usData[ind] = B3DMAX(0, B3DMIN(65535, iVal));
      }
    }
  }

  if (error) {
    sprintf(td->strTemp, "Warning: The returned summed image could not be gain "
      "normalized:\n%s\n", errStr.c_str());
    ProblemToResult(td->strTemp);
  }
#endif
}

/*
 * read in one of the K2 gain references if necessary
 */
static int LoadK2ReferenceIfNeeded(ThreadData *td, bool sizeMustMatch, string &errStr)
{
  int refInd = td->isSuperRes ? 1 : 0;
  ImodImageFile *iiFile = NULL;
  int error = 0;
#ifdef _WIN64
  if (!sK2GainRefData[refInd] || sK2GainRefTime[refInd] + 10. < td->curRefTime) {
    iiFile = iiOpen(td->strGainRefToCopy.c_str(), "rb");
    if (!iiFile) {
      error = 2;
      errStr = "the gain reference file could not be opened as an IMOD image file: " +
        string(b3dGetError());
    } else {
      if (sizeMustMatch && (iiFile->nx != td->width || iiFile->ny != td->height)) {
        error = 3;
      }
    }

    if (!error) {
      try {
        sK2GainRefData[refInd] = new float[iiFile->nx * iiFile->nx];
        error = iiReadSection(iiFile, (char *)sK2GainRefData[refInd], 0);
        if (error) {
          sprintf(td->strTemp, "error %d occurred reading the gain reference file",
            error);
          errStr = td->strTemp;
          error = 5;
          delete sK2GainRefData[refInd];
        } else {
          sK2GainRefTime[refInd] = td->curRefTime;
          sK2GainRefWidth[refInd] = iiFile->nx;
          sK2GainRefHeight[refInd] = iiFile->ny;
          sprintf(td->strTemp, "Loaded %s mode gain reference\n", 
            refInd ? "super-res" : "counting");
          DebugToResult(td->strTemp);
        }
      }
      catch (...) {
        error = 4;
        errStr = "could not allocate memory for gain reference";
      }
      if (error)
        sK2GainRefData[refInd] = NULL;
    }
  }
  if (iiFile)
    iiDelete(iiFile);
#endif
  return error;
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
  int tleft, ttop, tbot, tright;
  //int tsizeX, tsizeY, tfullX, tfullY;
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

  // Get modified sizes and temp array size for subarea
  if (td->bMakeSubarea) {
    outLimit = td->iFullSizeX * td->iFullSizeY;
    if (K2type && td->iReadMode >= 0 && td->isSuperRes)
      outLimit *= 4;
  }

  try {

    // Set up the acquisition parameters
    j = 0;
    camera = CM::GetCurrentCamera();  j++;
    manager = CM::GetCameraManager();  j++;
    acqParams = CM::CreateAcquisitionParameters_FullCCD(
      camera, (CM::AcquisitionProcessing)td->iK2Processing, 
      td->dK2Exposure + (K2type ? 0.001 : 0.), td->iK2Binning,  td->iK2Binning); j++;
    if (!td->bMakeSubarea)
      CM::SetBinnedReadArea(camera, acqParams, td->iTop, td->iLeft, td->iBottom,
        td->iRight);
    j++;
    CM::SetSettling(acqParams, td->dK2Settling);  j++;
    CM::SetShutterIndex(acqParams, td->iK2Shutter);  j++;
    if (td->iCorrections >= 0)
      CM::SetCorrections(acqParams, 0x1000 + 49, td->iCorrections); 
    j++;
    CM::SetDoContinuousReadout(acqParams, td->bSetContinuousMode); j++;

    transpose = 0;

    // Switch to this test if some other camera needs to have subareas made and can cancel
    // the rotationFlip
    //if (td->iReadMode != -2 && td->iReadMode != -3) {
    if (!td->bMakeSubarea) {

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

      }
      sprintf(td->strTemp, "Default transpose %d  rotationFlip %d\n", 
        transpose, rotationFlip);
      DebugToResult(td->strTemp);
      j++;
    }


    if (transpose || td->bMakeSubarea) {
      try {
        procOutArray = new short[outLimit];
      }
      catch (...) {
        DELETE_CONTINUOUS;
        return ROTBUF_MEMORY_ERROR;
      }
    }

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

    // Set read mode for OneView
    if (td->iReadMode == -2 || td->iReadMode == -3)
      CM::SetReadMode(acqParams, td->iReadMode + 3);

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

      if (td->bMakeSubarea) {
        tright = td->iRight;
        tleft = td->iLeft;
        ttop = td->iTop;
        tbot = td->iBottom;
        /* This would be needed if rotation/Flip needed to be done for a subarea
        if (rotationFlip) {
          tfullX = td->iFullSizeX;
          tfullY = td->iFullSizeY;
          tsizeX = tright - tleft;
          tsizeY = tbot - ttop;
          CorDefUserToRotFlipCCD(rotationFlip, 1, tfullX, tfullY, tsizeX, tsizeY, ttop,
            tleft, tbot, tright);
        } */
        ExtractSubarea(td, procOutArray, ttop, tleft, tbot, tright,  
          rotationFlip ? procOutArray : sContinuousArray, td->width, td->height);
      }
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
  if (transpose || td->bMakeSubarea)
    delete [] procOutArray;
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
    ProblemToResult(td->strTemp);
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
  if (td->isSuperRes && td->isFloat)
    frameDivide = (td->bFaKeepPrecision && td->iNumGrabAndStack) ? -3 : -1;

  // But if they are counting mode images in integer, they need to not be scaled
  // or divided by 2 if placed into shorts, and they may be packed to bytes
  if (td->isCounting && td->isInteger) {
    td->fFloatScaling = 1.;
    frameDivide = 0;
    if(td->iSaveFlags & K2_SAVE_RAW_PACKED)
      frameDivide = -2;
  }

  // For 2.3.1 simulator (so far) super-res nonGN frames come through as shorts
  // so they need to be truncated to bytes also
  if (td->isSuperRes && td->isInteger && td->byteSize == 2) {
      td->fFloatScaling = 1.;
      frameDivide = -2;
  }

  // Set # of bytes in the saving output
  td->outByteSize = 2;
  if (td->byteSize == 1 || frameDivide < 0) {
    td->fileMode = MRC_MODE_BYTE;
    td->outByteSize = 1;
  } else if ((frameDivide > 0 && td->byteSize != 2) || 
    (td->dataSize == td->byteSize && !td->isUnsignedInt))
    td->fileMode = MRC_MODE_SHORT;
  else
    td->fileMode = MRC_MODE_USHORT;
  td->save4bit = ((td->isSuperRes && td->byteSize <= 2) || 
    (td->isCounting && td->isInteger && (td->iSaveFlags & K2_RAW_COUNTING_4BIT)))
    && (td->iSaveFlags & K2_SAVE_RAW_PACKED);
  needProc = td->byteSize > 2 || td->signedBytes || frameDivide < 0;

  // Set the scaling that will be put in the header
  sWriteScaling = 1.;
  if (needProc && td->byteSize > 2) {
    sWriteScaling = td->fFloatScaling;
    if (frameDivide > 0)
      sWriteScaling /= 2.;
    else if (frameDivide == -1 ||frameDivide == -3 )
      sWriteScaling = SUPERRES_FRAME_SCALE;
  }

  // Set the byte size for a grab stack if any
  td->iGrabByteSize = td->outByteSize;
  td->iFaDataMode = td->fileMode;
  if (td->bFaKeepPrecision) {
    td->iGrabByteSize = td->isCounting ? 4 : 2;
    td->iFaDataMode = MRC_MODE_FLOAT;
    if (td->isSuperRes && td->iNumGrabAndStack)
      td->iFaDataMode = MRC_MODE_USHORT;
  }

  if (td->bUseFrameAlign && InitializeFrameAlign(td))
    return 1;

  td->iErrorFromSave = ROTBUF_MEMORY_ERROR;

  // Allocate buffer for rotation/flip if processing needs to be done too
  // Do a rotation/flip if needed or if writing MRC, to get flip for output
  if (needProc && td->saveFrames && (td->iFrameRotFlip || !td->bWriteTiff)) {
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

  sLastSaveNeededRef = false;
  if (td->saveFrames && ((td->isSuperRes || td->isCounting) && !td->isFloat && 
    td->iK2Processing != NEWCM_GAIN_NORMALIZED) && 
    (td->iSaveFlags & K2_COPY_GAIN_REF) && td->strGainRefToCopy.length()) {
      td->refCopyReturnVal = CopyK2ReferenceIfNeeded(td);
      sLastSaveNeededRef = !td->refCopyReturnVal;
  }
  return 0;
}

/*
 * Initializes framealign routine on first frame
 */
static int InitializeFrameAlign(ThreadData *td)
{
#ifdef _WIN64
  int numFilters = 0, ind;
  float radius2[4], sigma2[4], sigma1 = 0.03f; 
  float fullTaperFrac = 0.02f;
  float trimFrac = fullTaperFrac;
  float taperFrac = 0.1f;
  float kFactor = 4.5f;
  float maxMaxWeight = 0.1f;
  float divideScale = td->divideBy2 ? 0.5f : 1.f;

  // Get the scaling that will be applied to the images being aligned
  if (td->bFaKeepPrecision)
    td->fAlignScaling = B3DCHOICE(td->isSuperRes && td->iNumGrabAndStack, 
      SUPERRES_FRAME_SCALE * SUPERRES_PRESERVE_SCALE, 1.f);
  else if (td->iNumGrabAndStack && td->isFloat)
    td->fAlignScaling = B3DCHOICE(td->isSuperRes, 16.f, td->fSavedScaling * divideScale);
  else
    td->fAlignScaling = 1.;

  // Set up filters
  if (!td->fFaRadius2[0])
    td->fFaRadius2[0] = 0.06f;
  for (ind = 0; ind < 4; ind++) {
    radius2[ind] = td->fFaRadius2[ind];
    if (radius2[ind] <= 0)
      break;
    sigma2[ind] = (float)(0.001 * B3DNINT(1000. * td->fFaSigmaRatio * radius2[ind]));
    numFilters++;
  }
  if (numFilters > 1)
    rsSortFloats(radius2, numFilters);

  ind = sFrameAli.initialize(td->iFinalBinning, td->iFaAliBinning, trimFrac, 
    td->iFaNumAllVsAll, td->iFaRefineIter, td->iFaHybridShifts, 
    (td->iFaDeferGpuSum | td->iFaSmoothShifts) ? 1 : 0, td->iFaGroupSize, td->width, 
    td->height, fullTaperFrac, taperFrac, td->iFaAntialiasType, 0., radius2, sigma1, 
    sigma2, numFilters, td->iFaShiftLimit, kFactor, maxMaxWeight, 0, td->iExpectedFrames, 
    td->iFaGpuFlags, B3DCHOICE(sDebug, B3DMAX(1, sEnvDebug), 0));
  td->fFaRawDist = td->fFaSmoothDist = 0.;
  sTDwithFaResult = td;
  if (ind) {
    sprintf(td->strTemp, "The framealign routine failed to initialize (error %d)\n", ind);
    ErrorToResult(td->strTemp);
    td->iErrorFromSave = FRAMEALI_INITIALIZE;
    return 1;
  }
#endif
  return 0;
}

/*
 * Common routine that aligns, rotates, and/or saves a frame
 */
static int AlignOrSaveImage(ThreadData *td, short *outForRot, bool saveFrame, 
  bool alignFrame, int slice, bool finalFrame, int &fileSlice, int &tmin, int &tmax,
  float &meanSum, double &procWall, double &saveWall, double &alignWall, 
  double &wallStart)
{
  int i = 0;
  unsigned char *bData = (unsigned char *)td->outData;
  unsigned short *usData = (unsigned short *)td->outData;
  int refInd = td->isSuperRes ? 1 : 0;
#ifdef _WIN64
  int nxout, nyout;

  // Align the frame
  if (alignFrame) {
    DebugToResult("Passing frame to nextFrame\n");
    i = sFrameAli.nextFrame(td->outData, td->iFaDataMode, 
      td->iK2Processing == NEWCM_DARK_SUBTRACTED ? sK2GainRefData[refInd] : NULL, 
      sK2GainRefWidth[refInd], sK2GainRefHeight[refInd], NULL, 
      td->fAlignScaling * td->fFaTruncLimit, td->bCorrectDefects ? &sCamDefects : NULL, 
      td->iFaCamSizeX, td->iFaCamSizeY, 2 - refInd, 0., 0.);
    AccumulateWallTime(alignWall, wallStart);
    if (i) {
      td->iErrorFromSave = FRAMEALI_NEXT_FRAME;
      return i;
    }
  }
  if (saveFrame) {

    // If float precision was preserved and there is a grab stack involved, first process
    // the data: floats as the normal processing, or super-res data down to bytes.  All 
    // can be done in place
    if (td->bFaKeepPrecision && td->iNumGrabAndStack) {
      if (td->isCounting) {
        ProcessImage(td->outData, td->outData, td->dataSize, td->width, td->height, 
          td->divideBy2, 0, td->byteSize, td->isInteger, td->isUnsignedInt,
          td->fFloatScaling);

      } else {
        for (i = 0; i <  td->width * td->height; i++)
          bData[i] = (unsigned char)(usData[i] >> SUPERRES_PRESERVE_SHIFT);
      }
    }

    // Next rotate-flip if needed
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
      finalFrame, fileSlice, tmin, tmax, meanSum);
    AccumulateWallTime(saveWall, wallStart);
  }
#endif
  return i;
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
  int use4bitMode = (td->save4bit && (td->iSaveFlags & K2_SAVE_4BIT_MRC_MODE)) ? 1 : 0;
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

    // Set up title/description line
    if (td->save4bit)
      sprintf(td->strTemp, "SerialEMCCD: Dose frac. image, 4 bits packed  r/f %d",
        td->iFrameRotFlip);
    else
      sprintf(td->strTemp, "SerialEMCCD: Dose frac. image, scaled by %.2f  r/f %d", 
        sWriteScaling, td->iFrameRotFlip);

    // Set up header for one slice
    if (!td->bWriteTiff) {
      set4BitOutputMode(use4bitMode);
      mrc_head_new(&td->hdata, use4bitMode ? nxout : nxFile, nyout, 1, td->fileMode);
      if (td->save4bit && !use4bitMode)
        td->hdata.imodFlags |= MRC_FLAGS_4BIT_BYTES;
      mrc_head_label(&td->hdata, td->strTemp);
    } else {
      tiffAddDescription(td->strTemp);
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
    B3DCLAMP(tmin, 0, 15);
    B3DCLAMP(tmax, 0, 15);
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
 * Finish up the frame alignment and get statistics into td for return
 */
static int FinishFrameAlign(ThreadData *td, short *procOut, int numSlice)
{
#ifdef _WIN64
  int err, bestFilt, nxOut, nyOut, ix, iy;
  float resMean[5], smoothDist[5], rawDist[5], resSD[5], meanResMax[5];
  float maxResMax[5], meanRawMax[5], maxRawMax[5], ringCorrs[26], frcDelta = 0.02f;
  float refSigma = (float)(0.001 * B3DNINT(1000. * td->fFaRefRadius2 * 
    td->fFaSigmaRatio));
  float *aliSum = sFrameAli.getFullWorkArray();
  float *xShifts = new float [numSlice + 10];
  float *yShifts = new float [numSlice + 10];
  float finalScale;
  finalScale = (float)(td->iFinalBinning * td->iFinalBinning * td->fFloatScaling * 
    B3DCHOICE(td->bGainNormSum, td->fGainNormScale, 1.f) / td->fAlignScaling);
  
  // Get the final sum, which will have this size
  nxOut = td->width / td->iFinalBinning;
  nyOut = td->height / td->iFinalBinning;
  err = sFrameAli.finishAlignAndSum(td->fFaRefRadius2, refSigma, td->fFaStopIterBelow,
    td->iFaGroupRefine, td->iFaSmoothShifts, aliSum, xShifts, yShifts, xShifts, yShifts,
    td->bFaMakeEvenOdd ? ringCorrs : NULL, frcDelta, bestFilt, smoothDist, rawDist,
    resMean, resSD, meanResMax, maxResMax, meanRawMax, maxRawMax);
  if (err) {
    sprintf(td->strTemp, "Framealign failed to finish alignment (error %d)\n", err);
    ErrorToResult(td->strTemp);
    td->iErrorFromSave = FRAMEALI_FINISH_ALIGN;
    delete [] xShifts;
    delete [] yShifts;
    return td->iErrorFromSave;
  }

  // Get all the result statistics
  td->fFaResMean = resMean[bestFilt];
  td->fFaMaxResMax = maxResMax[bestFilt];
  td->fFaMeanRawMax = meanRawMax[bestFilt];
  td->fFaMaxRawMax = maxRawMax[bestFilt];
  td->fFaRawDist = rawDist[bestFilt];
  td->fFaSmoothDist = smoothDist[bestFilt];
  sTDwithFaResult = td;
  if (td->bFaMakeEvenOdd) {
    sFrameAli.analyzeFRCcrossings(ringCorrs, frcDelta, td->fFaCrossHalf, 
      td->fFaCrossQuarter, td->fFaCrossEighth, td->fFaHalfNyq);
  } else {
    td->fFaCrossHalf = td->fFaCrossQuarter = td->fFaCrossEighth = td->fFaHalfNyq = 0.;
  }

  // Get it scaled to integers and trimmed down to an expected size
  ProcessImage(aliSum, procOut, 2, nxOut, nyOut, td->divideBy2, 0, 4, false, false, 
    finalScale);
  ix = (nxOut - td->iFinalWidth) / 2;
  iy = (nyOut - td->iFinalHeight) / 2;
  sprintf(td->strTemp, "Sizes: %d %d %d %d offsets %d %d   alignScaled %.2f  "
    "finalScale %f\n", nxOut, nyOut, td->iFinalWidth, td->iFinalHeight, ix, iy, 
    td->fAlignScaling, finalScale);
  DebugToResult(td->strTemp);
  if (ix < 0 || iy < 0) {
    ErrorToResult("Unswapped sizes were sent\n");
  } else if (ix > 0 || iy > 0) {
    extractWithBinning(procOut, SLICE_MODE_SHORT, nxOut, ix, ix + td->iFinalWidth - 1, iy,
    iy + td->iFinalHeight - 1, 1, procOut, 0, &err, &nyOut);
  }
  delete [] xShifts;
  delete [] yShifts;
  sFrameAli.cleanup();
#endif
  return 0;
}

/*
 * Write a com file for alignment with alignframes
 */
static int WriteAlignComFile(ThreadData *td, string inputFile, bool ifMdoc)
{
  string comStr, aliHead, inputPath, relPath, outputRoot, outputExt, temp, temp2;
  int ind, error = 0;

  // Do all the easy options
  sprintf(td->strTemp, "$alignframes -StandardInput\n"
    "UseGPU %d\n"
    "PairwiseFrames %d\n"
    "GroupSize %d\n"
    "AlignAndSumBinning %d 1\n"
    "AntialiasFilter %d\n"
    "RefineAlignment %d\n"
    "StopIterationsAtShift %f\n"
    "ShiftLimit %d\n"
    "MinForSplineSmoothing %d\n"
    "FilterRadius2 %f\n"
    "FilterSigma2 %f\n"
    "VaryFilter %f", td->iFaGpuFlags ? 0 : -1, td->iFaNumAllVsAll, td->iFaGroupSize, 
    td->iFaAliBinning, td->iFaAntialiasType, td->iFaRefineIter, td->fFaStopIterBelow,
    td->iFaShiftLimit, td->iFaSmoothShifts ? 10 : 0, td->fFaRadius2[0], 
    td->fFaRadius2[0] * td->fFaSigmaRatio, td->fFaRadius2[0]);
  comStr = td->strTemp;
  for (ind = 1; ind < 4; ind++) {
    if (td->fFaRadius2[ind] > 0) {
      sprintf(td->strTemp, " %f");
      comStr += td->strTemp;
    }
  }
  comStr += "\n";
  if (td->fFaRefRadius2 > 0) {
    sprintf(td->strTemp, "RefineRadius2 %f\n", td->fFaRefRadius2);
    comStr += td->strTemp;
  }
  if (td->iFaHybridShifts)
    comStr += "UseHybridShifts 1\n";
  if (td->iFaGroupRefine)
    comStr += "RefineWithGroupSums 1\n";
  if (td->bFaOutputFloats)
    comStr += "ModeToOutput 2\n";

  // get the relative path and make sure it is OK
  SplitFilePath(td->strAlignComName, aliHead, temp);
  if (RelativePath(aliHead, td->strSaveDir, relPath))
    return MAKECOM_NO_REL_PATH;

  // Set input file with the relative path and the right option
  inputPath = relPath + inputFile;
  comStr += (ifMdoc ? "MetadataFile " : "InputFile ") + inputPath + "\n";
  if (ifMdoc && relPath.length())
    comStr += "PathToFramesInMdoc " + relPath + "\n";
  if (ifMdoc)
    comStr += "AdjustAndWriteMdoc 1\n";

  // Get the output file name, take 2 extension for an mdoc or replace .tif by .mrc
  SplitExtension(inputFile, outputRoot, outputExt);
  if (ifMdoc) {
    temp = outputRoot;
    outputExt;
    SplitExtension(temp, outputRoot, temp2);
    if (temp2.length())
      outputExt = temp2 + outputExt;
  } else if (outputExt == ".tif")
    outputExt = ".mrc";
  comStr += "OutputImageFile " + outputRoot + "_ali" + outputExt + "\n";

  // Truncation and scaling
  if (td->fFaTruncLimit > 0) {
    sprintf(td->strTemp, "TruncateAbove %f\n", td->fFaTruncLimit * sWriteScaling);
    comStr += td->strTemp;
  }
  sprintf(td->strTemp, "ScalingOfSum %f\n", sLastSaveFloatScaling / sWriteScaling);
  comStr += td->strTemp;

  // Defects and gain reference
  if (td->bLastSaveHadDefects)
    comStr += "CameraDefectFile " + relPath + td->strLastDefectName + "\n";
  if (sLastSaveNeededRef) {
    SplitFilePath(sLastRefName, temp, temp2);
    comStr += "GainReferenceFile " + relPath + temp2 + "\n";
    comStr += "RotationAndFlip -1\n";
  }

  // Write the file
  error = WriteTextFile(td->strAlignComName.c_str(), comStr.c_str(), (int)comStr.length(),
    OPEN_COM_ERROR, WRITE_COM_ERROR, false);
  return error;
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
  float scale;
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


  } else if (divideBy2 == -1) {

    // Scale super-res normalized floats into byte image with scaling by 16 because the
    // hardware limits the raw frames to 15 counts
    DebugToResult("Scaling super-res floats into bytes\n");
    scale = (float)SUPERRES_FRAME_SCALE;
    flIn = (float *)imageData;
    bData = (unsigned char *)array;
    for (i = 0; i < width * height; i++)
      bData[i] = (unsigned char)(flIn[i] * scale + 0.5f);

  } else if (divideBy2 == -3) {

    // Scale super-res normalized floats into full range of unsigned shorts to preserve
    // absurd amounts of the precision in the floats
    DebugToResult("Scaling super-res floats into full-range unsigned shorts\n");
    scale = (float)(SUPERRES_FRAME_SCALE * SUPERRES_PRESERVE_SCALE);
    flIn = (float *)imageData;
    usData = (unsigned short *)array;
    for (i = 0; i < width * height; i++)
      usData[i] = (unsigned short)(flIn[i] * scale + 0.5f);

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
      char mess[512];
      sprintf(mess, "Dividing floats by 2 with scaling by %f\n", floatScaling);
      DebugToResult(mess);
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
  WIN32_FIND_DATA findCopyData;
  HANDLE hFindCopy;
  string saveDir = td->strSaveDir;
  string rootName = td->strRootName;
  char *prefix[2] = {"Count", "Super"};
  int ind, retVal = 0;
  bool needCopy = true, namesOK = false;
  double maxCopySec = 0.;
  int prefInd = td->isSuperRes ? 1 : 0;

  // For single image files, find the date-time root and split up the name
  if (td->bFilePerImage) {
    ind = (int)saveDir.find_last_of("\\");
    if (ind < 2 || ind >= (int)saveDir.length() - 1)
      return 1;
    rootName = saveDir.data() + ind + 1;
    saveDir.erase(ind, string::npos);
  }


  // Get the reference file first
  if (CheckK2ReferenceTime(td))
    return 1;

  // If there is an existing copy and the mode and the directory match, find the copy
  // Otherwise look for all matching files in directory
  if (sLastRefName.length() && strstr(sLastRefName.c_str(), prefix[prefInd])
    && !sLastRefDir.compare(saveDir)) {
    //sprintf(td->strTemp, "Finding %s\n", sLastRefName.c_str());
    hFindCopy = FindFirstFile(sLastRefName.c_str(), &findCopyData);
    namesOK = true;
  } else {
    sprintf(td->strTemp, "%s\\%sRef_*.dm4", saveDir.c_str(), prefix[prefInd]);
    hFindCopy = FindFirstFile(td->strTemp, &findCopyData);
    //sprintf(td->strTemp, "finding %s\\%sRef_*.dm4\n", saveDir.data(), prefix[prefInd]);
  }
  //DebugToResult(td->strTemp);

  // Test that file or all candidate files in the directory and stop if find one
  // is sufficiently newer then the ref
  if (hFindCopy != INVALID_HANDLE_VALUE) {
    while (needCopy) {

      // The DM reference access and creation times match and the last write time is a bit
      // later.  Copies on a samba drive are given the original creation and write times,
      // but can lose precision but can come out earlier when comparing last write time,
      // although they are given current access times.  Copies on the SSD RAID are given 
      // the original write time but a current creation and access time
      double copySec = 429.4967296 * findCopyData.ftCreationTime.dwHighDateTime + 
        1.e-7 * findCopyData.ftCreationTime.dwLowDateTime;
      /*sprintf(td->strTemp, "refSec  %f  copySec %f\n", td->curRefTime, copySec);
      DebugToResult(td->strTemp);*/
      needCopy = td->curRefTime > copySec + 10.;
      maxCopySec = B3DMAX(maxCopySec, copySec);
      if (!needCopy || !FindNextFile(hFindCopy, &findCopyData))
        break;
    }
    FindClose(hFindCopy);
  }
  if (!needCopy && namesOK)
    return 0;

  // Fix up the directory and reference name if they are changed, whether it is an old
  // reference or a new copy
  sLastRefDir = saveDir;
  if (!needCopy) {
    sLastRefName = saveDir + "\\";
    sLastRefName += findCopyData.cFileName;
    return 0;
  }
  sprintf(td->strTemp, "%s\\%sRef_%s.dm4", saveDir.c_str(), prefix[prefInd],
    rootName.c_str());
  sLastRefName = td->strTemp;

  // Copy the reference
  sprintf(td->strTemp, "Making new copy of gain reference: refSec  %f  maxCopySec %f\n",
    td->curRefTime, maxCopySec);
  DebugToResult(td->strTemp);
  if (!CopyFile(td->strGainRefToCopy.c_str(), sLastRefName.c_str(), false)) {
    sprintf(td->strTemp, "An error occurred copying %s to %s\n", 
      td->strGainRefToCopy.c_str(), sLastRefName.c_str());
    ErrorToResult(td->strTemp);
    sLastRefName = "";
    return 1;
  }

  return 0;
}

/*
 * Looks for indicated gain reference and computes the reference time
 */
static int CheckK2ReferenceTime(ThreadData *td)
{
  WIN32_FIND_DATA findRefData;
  HANDLE hFindRef;
  hFindRef = FindFirstFile(td->strGainRefToCopy.c_str(), &findRefData);
  if (hFindRef == INVALID_HANDLE_VALUE) {
    sprintf(td->strTemp, "Cannot find K2 gain reference file: %s\n", 
      td->strGainRefToCopy.c_str());
    ErrorToResult(td->strTemp);
    return 1;
  } 
  FindClose(hFindRef);
  td->curRefTime = 429.4967296 * findRefData.ftCreationTime.dwHighDateTime + 
        1.e-7 * findRefData.ftCreationTime.dwLowDateTime;
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
    mTD.bUseFrameAlign = false;
    mTD.bMakeDeferredSum = false;
  }
  mTD.isCounting = mode >= 0 && sReadModes[mode] == K2_COUNTING_READ_MODE;
  mTD.isSuperRes = mode >= 0 && sReadModes[mode] == K2_SUPERRES_READ_MODE;
  ClearSpecialFlags();
}

/*
 * Set the parameters for the next K2 acquisition
 */
void TemplatePlugIn::SetK2Parameters(long mode, double scaling, long hardwareProc, 
                                     BOOL doseFrac, double frameTime, BOOL alignFrames, 
                                     BOOL saveFrames, long rotationFlip, long flags, 
                                     double reducedSizes, double fullSizes, double dummy3, 
                                     double dummy4, char *filter)
{
  SetReadMode(mode, scaling);
  mTD.iHardwareProc = hardwareProc;
  B3DCLAMP(mTD.iHardwareProc, 0, 6);
  m_bDoseFrac = doseFrac;
  mTD.dFrameTime = frameTime;

  // Override the DM align option with framealign option
  mTD.bUseFrameAlign = doseFrac && (flags & K2_USE_FRAMEALIGN) != 0;
  mTD.bAlignFrames = alignFrames && !mTD.bUseFrameAlign;
  mTD.bMakeAlignComFile = doseFrac && saveFrames && (flags & K2_MAKE_ALIGN_COM) != 0 &&
    mTD.strAlignComName.size() > 0;
  if (rotationFlip >= 0)
    mTD.iRotationFlip = rotationFlip;
  if (alignFrames) {
    strncpy(mTD.strFilterName, filter, MAX_FILTER_NAME - 1);
    mTD.strFilterName[MAX_FILTER_NAME - 1] = 0x00;
  }
  m_bSaveFrames = saveFrames;
  mTD.iAntialias = flags & K2_ANTIALIAS_MASK;
  if (mTD.iAntialias || mTD.bUseFrameAlign) {
    mTD.iFinalHeight = B3DNINT(reducedSizes / K2_REDUCED_Y_SCALE);
    mTD.iFinalWidth = B3DNINT(reducedSizes - K2_REDUCED_Y_SCALE * mTD.iFinalHeight);
  }
  mTD.bMakeSubarea = (flags & K2_OVW_MAKE_SUBAREA) && fullSizes > 0.;
  if (mTD.bMakeSubarea) {
    mTD.iFullSizeX = B3DNINT(fullSizes / K2_REDUCED_Y_SCALE);
    mTD.iFullSizeY = B3DNINT(fullSizes - K2_REDUCED_Y_SCALE * mTD.iFullSizeX);
  }
  sprintf(m_strTemp, "SetK2Parameters called with save %s  flags 0x%x\n", 
    m_bSaveFrames ? "Y":"N", flags);
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
  mTD.iRotationFlip = rotationFlip;
  mTD.iFrameRotFlip = (flags & K2_SKIP_FRAME_ROTFLIP) ? 0 : rotationFlip;
  B3DCLAMP(mTD.iRotationFlip, 0, 7);
  mTD.bFilePerImage = filePerImage;
  mTD.dPixelSize = pixelSize;
  mTD.iSaveFlags = flags;
  mTD.bWriteTiff = (flags & (K2_SAVE_LZW_TIFF | K2_SAVE_ZIP_TIFF)) != 0;
  mTD.iTiffCompression = (flags & K2_SAVE_ZIP_TIFF) ? 8 : 5;
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
    if (newDefects)
      m_bDefectsParsed = false;
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
  *error = ManageEarlyReturn(flags, (int)(nSumAndGrab + 0.1));
  if (*error)
    return;
  mTD.bMakeDeferredSum = mTD.bEarlyReturn && (flags & K2_MAKE_DEFERRED_SUM) != 0;

  sprintf(m_strTemp, "SetupFileSaving called with flags %x rf %d frf %d %s fpi %s pix %f"
    " ER %s A2R %s sum %d grab %d\n  copy %s \n  dir %s root %s\n", flags, rotationFlip,
    mTD.iFrameRotFlip, mTD.bWriteTiff ? "TIFF" : "MRC", filePerImage ? "Y":"N", pixelSize, 
    mTD.bEarlyReturn ? "Y":"N", mTD.bAsyncToRAM ? "Y":"N", mTD.iNumFramesToSum,
    mTD.iNumGrabAndStack, (flags & K2_COPY_GAIN_REF) ? mTD.strGainRefToCopy.c_str() :"NO",
    mTD.strSaveDir.c_str(), mTD.strRootName.c_str());
  DebugToResult(m_strTemp);

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
  ind1 = (int)topDir.length();
  if (ind1 && (topDir[ind1 - 1] == ('\\') || topDir[ind1 - 1] == ('/')))
    topDir.resize(ind1 - 1);
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
    string defectName;
    if (m_strDefectsToSave[0] == '#') {
      dummy = (int)m_strDefectsToSave.find('\n') - 1;
      if (dummy > 0 && m_strDefectsToSave[dummy] == '\r')
        dummy--;
      if (dummy > 3 && dummy < MAX_TEMP_STRING - 3 - (int)mTD.strSaveDir.length())
        defectName = m_strDefectsToSave.substr(1, dummy);
    }

    if (!defectName.length()) {
      sprintf(m_strTemp, "defects%d.txt", mTD.strSaveDir.c_str(),
        (GetTickCount() / 1000) % 10000);
      defectName = m_strTemp;
    }
    sprintf(m_strTemp, "%s\\%s", mTD.strSaveDir.c_str(), defectName.c_str());

    // If the string is new, one file per image, new directory, or the file doesn't
    // exist, write the text
    if (newDefects || filePerImage || newDir || !_stat(m_strTemp, &statbuf)) {
      *error = WriteTextFile(m_strTemp, m_strDefectsToSave.c_str(), 
        (int)m_strDefectsToSave.length(), OPEN_DEFECTS_ERROR, WRITE_DEFECTS_ERROR, false);
      mTD.strLastDefectName = defectName;
    }
  }
  if (!*error)
    mTD.bLastSaveHadDefects = defects && (flags & K2_SAVE_DEFECTS);

  if (*error) {
    sprintf(m_strTemp, "SetupFileSaving error is %d\n", *error);
    DebugToResult(m_strTemp);
  }
}

/*
 * Set the early return and asynchronous save flags, interpret sum&grab variable
 * and make sure that synchronous save/align isn't happening for new version
 */
int TemplatePlugIn::ManageEarlyReturn(int flags, int iSumAndGrab)
{
  mTD.bAsyncSave = (flags & K2_SAVE_SYNCHRON) == 0;
  mTD.bEarlyReturn = (flags & K2_EARLY_RETURN) != 0;
  mTD.bAsyncToRAM = (flags & K2_ASYNC_IN_RAM) != 0;
  mTD.iNumFramesToSum = 65535;
  mTD.iNumGrabAndStack = 0;
  if (mTD.bEarlyReturn) {
    mTD.iNumFramesToSum = iSumAndGrab & 65535;
    mTD.iNumGrabAndStack = GMS_SDK_VERSION > 30 ? (iSumAndGrab >> 16) : 0;
  }
  if (mTD.bEarlyReturn && !mTD.bAsyncSave && GMS_SDK_VERSION > 30)
    return EARLY_RET_WITH_SYNC;
  return 0;
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
  ThreadData *td = m_bNextSaveResultsFromCopy ? &mTDcopy : &mTD;
  *numSaved = td->iFramesSaved;
  *error = td->iErrorFromSave;
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
 * Test if GPU is available and get memory
 */
int TemplatePlugIn::IsGpuAvailable(long gpuNum, double *gpuMemory)
{
  int m_iGpuAvailable = 0;
  *gpuMemory = 0;
#ifdef _WIN64
  float memory;
  m_iGpuAvailable = sFrameAli.gpuAvailable(gpuNum, &memory, sDebug);
  *gpuMemory = m_iGpuAvailable ? memory : 0.;
#endif
  return m_iGpuAvailable;
}

/*
 * Set up for alignment with framealign
 */
void TemplatePlugIn::SetupFrameAligning(long aliBinning, double rad2Filt1, 
    double rad2Filt2, double rad2Filt3, double sigma2Ratio, 
    double truncLimit, long alignFlags, long gpuFlags, long numAllVsAll, long groupSize, 
    long shiftLimit, long antialiasType, long refineIter, double stopIterBelow, 
    double refRad2, long nSumAndGrab, long dumInt1, long dumInt2, double dumDbl1, 
    char *refName, char *defects, char *comName, long *error)
{
  string errStr;
  int newDefects = 0;
  int gpuNum = gpuFlags / 65536;
  *error = 0;
#ifdef _WIN64
  bool makingCom = (alignFlags & K2_MAKE_ALIGN_COM) != 0 && comName != NULL;
  float memory;
  gpuFlags = gpuFlags & 65535;
  if (gpuFlags && m_iGpuAvailable < 0)
    m_iGpuAvailable = sFrameAli.gpuAvailable(gpuNum, &memory, sDebug);
  if (gpuFlags && m_iGpuAvailable <= 0) {
    *error = NO_GPU_AVAILABLE;
    return;
  }

  // If correcting defects, see if the string has changed since last saved or parsed
  // and parse them if needed
  if ((alignFlags & K2_SAVE_DEFECTS) && defects) {
    if (CopyStringIfChanged(defects, m_strDefectsToSave, newDefects, error))
      return;
    if (newDefects || !m_bDefectsParsed) {
      if (CorDefParseDefects(m_strDefectsToSave.c_str(), 1, sCamDefects, mTD.iFaCamSizeX, 
        mTD.iFaCamSizeY)) {
          *error = DEFECT_PARSE_ERROR;
          return;
      }
      CorDefFindTouchingPixels(sCamDefects, mTD.iFaCamSizeX, mTD.iFaCamSizeY, 0);
      if (!sCamDefects.wasScaled)
        CorDefScaleDefectsForK2(&sCamDefects, false);
      if (sCamDefects.rotationFlip % 2) {
        newDefects = mTD.iFaCamSizeX;
        mTD.iFaCamSizeX = mTD.iFaCamSizeY;
        mTD.iFaCamSizeY = newDefects;
      }
      CorDefRotateFlipDefects(sCamDefects, 0, mTD.iFaCamSizeX, mTD.iFaCamSizeY);
    }
    m_bDefectsParsed = true;
    mTD.bCorrectDefects = true;
  }

  // Get a gain reference name, load it only if needed in GetImage
  if ((alignFlags & K2_COPY_GAIN_REF) && refName) {
    if (CopyStringIfChanged(refName, mTD.strGainRefToCopy, newDefects, error))
      return;
  }

  // Get name of com file to make
  if (makingCom) {
    if (CopyStringIfChanged(comName, mTD.strAlignComName, newDefects, error))
      return;
  }

  mTD.bUseFrameAlign = true;    // Probably useless, SetK2Parameters must have flag
  mTD.iFaAliBinning = aliBinning;
  mTD.fFaRadius2[0] = (float)rad2Filt1;
  mTD.fFaRadius2[1] = (float)rad2Filt2;
  mTD.fFaRadius2[2] = (float)rad2Filt3;
  mTD.fFaRadius2[3] = 0.;
  mTD.fFaSigmaRatio = (float)sigma2Ratio;
  mTD.fFaTruncLimit = (float)truncLimit;
  mTD.iFaGpuFlags = gpuFlags;
  mTD.iFaNumAllVsAll = numAllVsAll;
  mTD.iFaGroupSize = groupSize;
  mTD.iFaShiftLimit = shiftLimit;
  mTD.iFaAntialiasType = antialiasType;
  mTD.iFaRefineIter = refineIter;
  mTD.fFaStopIterBelow = (float)stopIterBelow;
  mTD.fFaRefRadius2 = (float)refRad2;
  mTD.iFaDeferGpuSum = (alignFlags & K2FA_DEFER_GPU_SUM);
  mTD.iFaSmoothShifts = (alignFlags & K2FA_SMOOTH_SHIFTS);
  mTD.iFaGroupRefine = (alignFlags & K2FA_GROUP_REFINE);
  mTD.iFaHybridShifts = (alignFlags & K2FA_USE_HYBRID_SHIFTS);
  mTD.bFaMakeEvenOdd = (alignFlags & K2FA_MAKE_EVEN_ODD);
  mTD.bFaKeepPrecision = !makingCom && (alignFlags & K2FA_KEEP_PRECISION);
  mTD.bFaOutputFloats = makingCom && (alignFlags & K2FA_KEEP_PRECISION);
  *error = ManageEarlyReturn(alignFlags, nSumAndGrab);
#endif
}

/*
 * Get the statistics after framealign finished
 */
void TemplatePlugIn::FrameAlignResults(double *rawDist, double *smoothDist, 
    double *resMean, double *maxResMax, double *meanRawMax, double *maxRawMax, 
    double *crossHalf, double *crossQuarter, double *crossEighth, double *halfNyq, 
    long *dumInt1, double *dumDbl1, double *dumDbl2, double *dumDbl3)
{
  *rawDist = mTDcopy.fFaRawDist;
  *smoothDist = mTDcopy.fFaSmoothDist;
  *resMean = mTDcopy.fFaResMean;
  *maxResMax = mTDcopy.fFaMaxResMax;
  *meanRawMax = mTDcopy.fFaMeanRawMax;
  *maxRawMax = mTDcopy.fFaMaxRawMax;
  *crossHalf = mTDcopy.fFaCrossHalf;
  *crossQuarter = mTDcopy.fFaCrossQuarter;
  *crossEighth = mTDcopy.fFaCrossEighth;
  *halfNyq = mTDcopy.fFaHalfNyq;
}

/*
 * Write the com file for aligning based on an mdoc file (for tilt series)
 */
void TemplatePlugIn::MakeAlignComFile(long flags, long dumInt1, double dumDbl1, 
    double dumDbl2, char *mdocName, char *mdocFileOrText, long *error)
{
  string mdocInSaveDir = mTD.strSaveDir + "\\" + mdocName;
  *error = 0;

  // Either save the string or try to copy the given file to frame directory
  if (flags & K2FA_WRITE_MDOC_TEXT) {
    *error = WriteTextFile(mdocInSaveDir.c_str(), mdocFileOrText, 
      (int)strlen(mdocFileOrText), OPEN_MDOC_ERROR, WRITE_MDOC_ERROR, true);
     sprintf(mTD.strTemp, "An error occurred writing an mdoc to %s\n", 
       mdocInSaveDir.c_str());
 } else if (!CopyFile(mdocFileOrText, mdocInSaveDir.c_str(), false)) {
    sprintf(mTD.strTemp, "An error occurred copying %s to %s\n", 
      mdocFileOrText, mdocInSaveDir.c_str());
    *error  = COPY_MDOC_ERROR;
  }
  if (*error) {
    ErrorToResult(mTD.strTemp);
    return;
  }
  *error = WriteAlignComFile(&mTD, mdocName, true);
}

/*
 * Return a deferred sum if available
 */
int TemplatePlugIn::ReturnDeferredSum(short *array, long *arrSize, long *width, 
		long *height)
{
  bool useFinal = mTDcopy.iAntialias || mTDcopy.bMakeSubarea || mTDcopy.bUseFrameAlign;
  m_bNextSaveResultsFromCopy = true;
  if (m_HAcquireThread && WaitForAcquireThread(WAIT_FOR_THREAD))
    return 1;
  if (!sValidDeferredSum || !sDeferredSum) {
    return NO_DEFERRED_SUM;
  }
  *width = useFinal ? mTDcopy.iFinalWidth : mTDcopy.width;
  *height = useFinal ? mTDcopy.iFinalHeight : mTDcopy.height;
  if (*width * *height > *arrSize) {
    sprintf(mTD.strTemp, "Warning: deferred sum is larger than the supplied array (sum "
      "%dx%d = %d, array %d)\n", *width, *height, *width * *height, *arrSize);
    ProblemToResult(mTD.strTemp);
    *width = *arrSize / *height;
  }
  *arrSize = *width * *height;
  if (*arrSize)
    memcpy(array, sDeferredSum, *arrSize * 2);
  delete [] sDeferredSum;
  sDeferredSum = NULL;
  sValidDeferredSum = false;
  return 0;
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
                                  long flags, long continuous, long numChan, 
                                  long channels[], long divideBy2)
{
  int chan, j, again, dataSize = 2;
  // 7/25/13: Set based on fact that 20 second exposures were not safe even with 800
  // msec extra shot delay.  This is extra-generous, 1.04 should do it.
  float delayErrorFactor = 1.05f;
  int lineSync = flags & DS_LINE_SYNC;
  flags = flags % 2000;   // Temporarily keep this so Weizmann test settings work
  int beamToSafe = flags & DS_BEAM_TO_SAFE;
  int beamToFixed = flags & DS_BEAM_TO_FIXED;
  int beamToEdge = flags & DS_BEAM_TO_EDGE;
  bool controlScan = (flags & (DS_CONTROL_SCAN | DS_BEAM_TO_SAFE | DS_BEAM_TO_FIXED |
    DS_BEAM_TO_EDGE)) != 0 && m_iDMVersion >= 40200;
  double fullExpTime = *height * (*width * pixelTime + m_dFlyback + 
    (lineSync ? m_dSyncMargin : 0.)) / 1000.;
  ClearSpecialFlags();

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
    if (controlScan) {
      mTD.strCommand += "DSControlBeamEnabled(1)\n";
      if (beamToEdge || beamToFixed)
        mTD.strCommand += "TagGroup tg = GetPersistentTagGroup()\n"
          "tg.TagGroupSetTagAsNumber(\"DigiScan:AllowBeamMarkerOutsideImage\", 1)\n";
      if (sDebug)
        mTD.strCommand += "number xDS, yDS\n"
          "DSGetBeamDSPosition( xDS, yDS )\n"
          "Result(\"Before shot, beam at \" + xDS + \",\" + yDS + \"\\n\")\n";
      mTD.strCommand += "if (!DSHasScanControl()) {\n"
        "  DSSetScanControl(1)\n"
        "  Result(\"Have to take scan control before shot\\n\")\n"
        "}\n";
    }
    if (m_iExtraDSdelay > 0 && !controlScan) {

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
    if (controlScan) {
      mTD.strCommand += "Delay(1)\n";
      if (sDebug)
        mTD.strCommand += "DSGetBeamDSPosition( xDS, yDS )\n"
          "Result(\"After shot, beam at \" + xDS + \",\" + yDS + \"\\n\")\n";
      mTD.strCommand += "if (!DSHasScanControl()) {\n"
        "  DSSetScanControl(1)\n"
        "  Result(\"Have to take scan control after shot\\n\")\n"
        "}\n"
        "if (DSHasScanControl()) {\n";
      if (beamToSafe)
        mTD.strCommand += "  DSSetBeamToSafePosition()\n";
      else if (beamToFixed)
        mTD.strCommand += "  DSPositionBeam(imchan, -xsize / 8, -ysize / 8)\n";
      else if (beamToEdge)
        mTD.strCommand += "  DSPositionBeam(imchan, xsize + xsize / 8, ysize + "
        "ysize / 8)\n";
      mTD.strCommand += "} else {\n"
        "  Result(\"Failed to get scan control after shot\\n\")\n"
        "}\n";
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
  ClearSpecialFlags();
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

// Remove one or both K2 gain references
void TemplatePlugIn::FreeK2GainReference(long which)
{
  for (int ind = 0; ind < 2; ind++) {
    if (which & (ind + 1)) {
      if (sK2GainRefData[ind]) {
        sprintf(m_strTemp, "Freed memory for %s mode gain reference\n", 
          ind ? "super-res" : "counting");
        DebugToResult(m_strTemp);
        delete sK2GainRefData[ind];
        sK2GainRefData[ind] = NULL;
      }
    }
  }
}

// Returns a relative path from fromDIR to toDir in relPath, returns 1 if no possible
static int RelativePath(string fromDir, string toDir, string &relPath)
{
  int ind, fromLen, toLen, findInd;
  fromLen = StandardizePath(fromDir);
  toLen = StandardizePath(toDir);

  // Find first non-matching character
  for (ind = 0; ind < B3DMIN(fromLen, toLen); ind++)
    if (fromDir[ind] != toDir[ind])
      break;
  if (!ind)
    return 1;

  // Switch to index of last match
  ind--;

  // If both at a /, back up
  if (fromDir[ind] == '/' && toDir[ind] == '/') {
    ind--;

    // For windows, do not allow a starting /
    if (ind < 0)
      return 1;

    // if both are either at the end or followed by a /, it is at a directory, but
    // but otherwise need to back up to previous /
  } else if (!((ind == fromLen - 1 || fromDir[ind + 1] == '/') &&
               (ind == toLen - 1 || toDir[ind + 1] == '/'))) {
    ind = (int)fromDir.find_last_of('/', ind);

    // The ind == 0 is specific to Windows to require drive letter at start
    if (ind == string::npos || ind == 0)
      return 1;
    ind--;
  }

  // return blank path if strings match to ends
  relPath.clear();
  if (ind == fromLen - 1 && ind == toLen - 1)
    return 0;

  // Start with a ../ for each directory after the match in the FROM directory
  findInd = fromLen - 1;
  while (findInd > ind + 1) {
    relPath = relPath + "../";
    findInd = (int)fromDir.find_last_of('/', findInd);
    if (findInd == string::npos || findInd <= ind + 1)
      break;
    findInd--;
  }

  // Then add the path from the match to the end in the TO directory
  if (ind < toLen - 2)
    relPath += toDir.substr(ind + 2) + "/";
  return 0;
}

// change \ to /, remove //, and remove trailing /, and return final length
static int StandardizePath(string &dir)
{
  size_t ind;
  while ((ind = dir.find('\\')) != string::npos)
    dir = dir.replace(ind, 1, "/");
  while ((ind = dir.find("//")) != string::npos)
    dir = dir.replace(ind, 2, "/");
  ind = dir.length();
  if (ind > 0 && dir[ind - 1] == '/') {
    dir.resize(ind - 1);
    ind--;
  }
  return (int)ind;
}

// Split a file path into directory (with trail slash) and filename
static void SplitFilePath(const string &path, string &dir, string &file)
{
  size_t ind;
  dir = "";
  file = path;
  ind = path.find_last_of("\\/");
  if (ind == string::npos)
    return;
  dir = path.substr(0, ind + 1);
  file = path.substr(ind + 1);
}

// Split a filename into a rootname and extension with . attached to extension
static void SplitExtension(const string &file, string &root, string &ext)
{
  size_t ind;
  ext = "";
  root = file;
  ind = file.find_last_of('.');
  if (ind == string::npos || ind == 0)
    return;
  root = file.substr(0, ind);
  ext = file.substr(ind);
}

static int WriteTextFile(const char *filename, const char *text, int length, 
  int openErr, int writeErr, bool asBinary)
{
  int error = 0;
  FILE *fp = fopen(filename, asBinary ? "wb" : "wt");
  if (!fp) {
    error = openErr;
  } else {
    if (fwrite(text, 1, length, fp) != length)
      error = writeErr;
    fclose(fp);
  }
  return error;
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
  int rootind = (int)strlen(cnames) + 1;
  int nextInd = rootind + (int)strlen(&cnames[rootind]) + 1;
  char *refName = UnpackString((flags & K2_COPY_GAIN_REF) != 0, names, nextInd);
  char *defects = UnpackString((flags & K2_SAVE_DEFECTS) != 0, names, nextInd);
  char *command = UnpackString((flags & K2_RUN_COMMAND) != 0, names, nextInd);
  char *sumList = UnpackString((flags & K2_SAVE_SUMMED_FRAMES) != 0, names, nextInd);
  mLastRetVal = 0;
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

int PlugInWrapper::IsGpuAvailable(long gpuNum, double *gpuMemory)
{
  int retVal = gTemplatePlugIn.IsGpuAvailable(gpuNum, gpuMemory);
  mLastRetVal = 0;
  if (retVal < 0)
    mLastRetVal = retVal;
  return retVal;
}

void PlugInWrapper::SetupFrameAligning(long aliBinning, double rad2Filt1, 
  double rad2Filt2, double rad2Filt3, double sigma2Ratio, 
  double truncLimit, long alignFlags, long gpuFlags, long numAllVsAll, long groupSize, 
  long shiftLimit, long antialiasType, long refineIter, double stopIterBelow, 
    double refRad2, long nSumAndGrab, long dumInt1, long dumInt2, double dumDbl1, 
    long *strings, long *error)
{
  mLastRetVal = 0;
  int nextInd = 0;
  char *refName = UnpackString((alignFlags & K2_COPY_GAIN_REF) != 0, strings, nextInd);
  char *defects = UnpackString((alignFlags & K2_SAVE_DEFECTS) != 0, strings, nextInd);
  char *comName = UnpackString((alignFlags & K2_MAKE_ALIGN_COM) != 0, strings, nextInd);
  gTemplatePlugIn.SetupFrameAligning(aliBinning, rad2Filt1, rad2Filt2, rad2Filt3, 
    sigma2Ratio, truncLimit, alignFlags, gpuFlags, numAllVsAll, groupSize, 
    shiftLimit, antialiasType, refineIter, stopIterBelow, refRad2, nSumAndGrab, dumInt1, 
    dumInt2, dumDbl1, refName, defects, comName, error);
}

void PlugInWrapper::FrameAlignResults(double *rawDist, double *smoothDist, 
  double *resMean, double *maxResMax, double *meanRawMax, double *maxRawMax, 
  double *crossHalf, double *crossQuarter, double *crossEighth, double *halfNyq, 
  long *dumInt1, double *dumDbl1, double *dumDbl2, double *dumDbl3)
{
  mLastRetVal = 0;
  gTemplatePlugIn.FrameAlignResults(rawDist, smoothDist, resMean, maxResMax, meanRawMax, 
    maxRawMax, crossHalf, crossQuarter, crossEighth, halfNyq, dumInt1, dumDbl1, dumDbl2, 
    dumDbl3);
}

void PlugInWrapper::MakeAlignComFile(long flags, long dumInt1, double dumDbl1, 
    double dumDbl2, long *strings, long *error)
{
  mLastRetVal = 0;
  int nextInd = 0;
  char *mdocName = UnpackString(true, strings, nextInd);
  char *mdocFileOrText = UnpackString(true, strings, nextInd);
  gTemplatePlugIn.MakeAlignComFile(flags, dumInt1, dumDbl1, dumDbl2, mdocName,
    mdocFileOrText, error);
}

int PlugInWrapper::ReturnDeferredSum(short *array, long *arrSize, long *width, 
		long *height)
{
  return (mLastRetVal = gTemplatePlugIn.ReturnDeferredSum(array, arrSize, width, height)); 
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
  return sEnvDebug;
}
void PlugInWrapper::FreeK2GainReference(long which)
{
  gTemplatePlugIn.FreeK2GainReference(which);
}

char *PlugInWrapper::UnpackString(bool doIt, long *strings, int &nextInd)
{
  char *cnames = (char *)strings;
  char *retVal = NULL;
  if (doIt) {
    retVal = &cnames[nextInd];
    nextInd += (int)strlen(&cnames[nextInd]) + 1;
  }
  return retVal;
}


// Dummy functions for 32-bit.
#ifndef _WIN64
double wallTime(void) { return 0.;}
void overrideWriteBytes(int inVal) {}
void iiDelete(ImodImageFile *inFile) {}
int imodBackupFile(const char *) {return 0;}
#endif
