/*
 * TemplatePlugIn.h - declarations for TemplatePlugIn.cpp and the wrapper class
 *
 * See Copyright.txt for copyright and limitations
 */
#pragma once

#include "SEMCCDDefines.h"

#define SCRIPT_ERROR_RETURN  -999.
#define DARK_REFERENCE  -1
#define UNPROCESSED 0
#define DARK_SUBTRACTED 1
#define GAIN_NORMALIZED 2
#define NEWCM_UNPROCESSED 1
#define NEWCM_DARK_SUBTRACTED 2
#define NEWCM_GAIN_NORMALIZED 3
#define K2_LINEAR_READ_MODE 0
#define K2_COUNTING_READ_MODE 2
#define K2_SUPERRES_READ_MODE 3
#define K3_SUPER_COUNT_READ_MODE 1
#define OV_DIFFRACTION_MODE   1

#define OLD_OPEN_SHUTTER_BROKEN    360
#define OLD_SELECT_SHUTTER_BROKEN    360
#define OLD_SETTLING_BROKEN   360
#define NEW_CAMERA_MANAGER    360
#define NEW_OPEN_SHUTTER_OK   370
// A guess that this went with current shutter state
#define SET_IDLE_STATE_OK     370
// These are broken in 3.7.1.5 but work in 3.7.1.6.  If you have 3.7.1.5 you
// have to disable them through SerialEM properties
#define NEW_SELECT_SHUTTER_OK 371
#define NEW_SETTLING_OK       371

extern double TickInterval(double start);
extern BOOL SleepMsg(DWORD dwTime_ms);

class PlugInWrapper
{
public:
	void SetNoDMSettling(long camera);
	int SetShutterNormallyClosed(long camera, long shutter);
	long GetDMVersion(long *build);
  long GetPluginVersion() {return SEMCCD_PLUGIN_VERSION;};
	int InsertCamera(long camera, BOOL state);
	int IsCameraInserted(long camera);
	int GetNumberOfCameras();
	int SelectCamera(long camera);
	void SetReadMode(long mode, double scaling);
  void SetK2Parameters(long readMode, double scaling, long hardwareProc, BOOL doseFrac, 
    double frameTime, BOOL alignFrames, BOOL saveFrames, long rotationFlip, long flags, 
    double dummy1, double dummy2, double dummy3, double dummy4, char *filter);
  void SetupFileSaving(long rotationFlip, BOOL filePerImage, double pixelSize, long flags,
    double nSumAndGrab, double frameThresh, double dummy3, double dummy4, long *names,
    long *error);
  void GetFileSaveResult(long *numSaved, long *error);
  int GetDefectList(short xyPairs[], long *arrSize, long *numPoints, 
    long *numTotal);
  int IsGpuAvailable(long gpuNum, double *gpuMemory);
  void SetupFrameAligning(long aliBinning, double rad2Filt1, 
    double rad2Filt2, double rad2Filt3, double sigma2Ratio, 
    double truncLimit, long alignFlags, long gpuFlags, long numAllVsAll, long groupSize, 
    long shiftLimit, long antialiasType, long refineIter, double stopIterBelow, 
    double refRad2, long nSumAndGrab, long frameStartEnd, long dumInt2, double dumDbl1, 
    long *strings, long *error);
  void FrameAlignResults(double *rawDist, double *smoothDist, 
    double *resMean, double *maxResMax, double *meanRawMax, double *maxRawMax, 
    long *crossHalf, long *crossQuarter, long *crossEighth, long *halfNyq, 
    long *dumInt1, double *dumDbl1, double *dumDbl2);
  double GetLastDoseRate();
  void MakeAlignComFile(long flags, long dumInt1, double dumDbl1, 
    double dumDbl2, long *strings, long *error);
  int ReturnDeferredSum(short array[], long *arrSize, long *width, long *height); 
	int GetImage(short *array, long *arrSize, long *width, 
		long *height, long processing, double exposure,
		long binning, long top, long left, long bottom, 
		long right, long shutter, double settling, long shutterDelay, 
    long divideBy2, long corrections);
	int GetGainReference(float *array, long *arrSize, long *width, 
		long *height, long binning);
	void QueueScript(char *strScript);
  int SaveFrameMdoc(char *strMdoc, long flags);
	void SetCurrentCamera(long inVal);
	void SetDMVersion(long inVal);
	void SetDebugMode(int inVal);
	double ExecuteScript(char *strScript, BOOL selectCamera);
  int GetDSProperties(long timeout, double addedFlyback, double margin, double *flyback, 
                      double *lineFreq, double *rotOffset, long *doFlip);
  int AcquireDSImage(short array[], long *arrSize, long *width, 
    long *height, double rotation, double pixelTime, 
    long lineSync, long continuous, long numChan, long channels[], long divideBy2);
  int ReturnDSChannel(short array[], long *arrSize, long *width, 
    long *height, long channel, long divideBy2);
  int StopDSAcquisition();
  int StopContinuousCamera();
	BOOL GetCameraBusy();
	PlugInWrapper();
	BOOL GetPlugInRunning();
	void ErrorToResult(const char *strMessage, const char *strPrefix = NULL);
	void DebugToResult(const char *strMessage, const char *strPrefix = NULL);
  int GetDebugVal();
  int mLastRetVal;
  void FreeK2GainReference(long which);
  int WaitUntilReady(long which);
  char *UnpackString(bool doIt, long *strings, int &nextInd);
};
