#pragma once
#define SCRIPT_ERROR_RETURN  -999.
#define IMAGE_NOT_FOUND  1
#define WRONG_DATA_TYPE  2
#define DM_CALL_EXCEPTION 3
#define DARK_REFERENCE  -1
#define UNPROCESSED 0
#define DARK_SUBTRACTED 1
#define GAIN_NORMALIZED 2
#define NEWCM_UNPROCESSED 1
#define NEWCM_DARK_SUBTRACTED 2
#define NEWCM_GAIN_NORMALIZED 3

#define OLD_SHUTTER_BROKEN  360
#define OLD_SETTLING_BROKEN  360
#define NEW_CAMERA_MANAGER  360
#define NEW_SHUTTER_OK      370
#define NEW_SETTLING_OK     370

class PlugInWrapper
{
public:
	int InsertCamera(long camera, BOOL state);
	int IsCameraInserted(long camera);
	int GetNumberOfCameras();
	int SelectCamera(long camera);
	int GetImage(short *array, long *arrSize, long *width, 
		long *height, long processing, double exposure,
		long binning, long top, long left, long bottom, 
		long right, long shutter, double settling, long shutterDelay);
	int GetGainReference(float *array, long *arrSize, long *width, 
		long *height, long binning);
	void QueueScript(char *strScript);
	void SetCurrentCamera(long inVal);
	void SetDMVersion(long inVal);
	void SetDebugMode(int inVal);
	double ExecuteScript(char *strScript, BOOL selectCamera);
	BOOL GetCameraBusy();
	PlugInWrapper();
	BOOL GetPlugInRunning();
};