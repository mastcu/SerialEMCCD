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
		m_bBusy = false;
		m_bRunning = false;
		m_bDebug = true;
		m_iDMVersion = 340;
		m_iCurrentCamera = 0;
		m_strQueue.resize(0);
	}

	BOOL GetPlugInRunning() {return m_bRunning;};
	
private:
	BOOL m_bBusy;
	BOOL m_bRunning;
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
	m_bRunning = true;
	if (WasCOMInitialized())
		DebugToResult("COM was initialized through DllMain\n");
		//PlugIn::gResultOut << "COM was initialized through DllMain" << std::endl;
	else {
		m_bDebug = true;
		DebugToResult("DllMain was never called - trouble!\n");
		//PlugIn::gResultOut << "DllMain was never called - trouble!" << std::endl;
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
	m_bRunning = false;
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
	m_strCommand.append(strScript);
	char last = m_strCommand[m_strCommand.length() - 1];
	if (last != '\n' && last != '\r')
		m_strCommand.append("\n");
	return ExecuteScript((char *)m_strCommand.c_str());
}

void TemplatePlugIn::QueueScript(char *strScript)
{
	m_strQueue.append(strScript);
	char last = m_strQueue[m_strQueue.length() - 1];
	if (last != '\n' && last != '\r')
		m_strQueue.append("\n");
	DebugToResult("QueueScript called, queue is now:\n");
	if (m_bDebug)
		DM::Result((char *)m_strQueue.c_str());
}

// Add commands for selecting a camera to the command string
void TemplatePlugIn::AddCameraSelection(int camera)
{
	if (camera < 0)
		camera = m_iCurrentCamera;
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
		m_strCommand.append(m_strQueue);
		m_strQueue.resize(0);
	}

	// Old version, set the settling and the alternate shutter through notes
	if (m_iDMVersion < OLD_SETTLING_BROKEN) {
		sprintf(m_strTemp, 
			"SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", %g)\n",	settling);
		m_strCommand.append(m_strTemp);
	}

	if (m_iDMVersion < OLD_SHUTTER_BROKEN) {
		sprintf(m_strTemp, "SetPersistentNumberNote"
			"(\"MSC:Parameters:2:Alternate Shutter\", %d)\r", shutter);
		m_strCommand.append(m_strTemp);
	}

	// Open shutter if a delay is set
	if (shutterDelay) {
		m_strCommand.append("SSCOpenShutter()\n");
		sprintf(m_strTemp, "Delay(%d)\n", shutterDelay);
		m_strCommand.append(m_strTemp);
		m_strCommand.append("SSCCloseShutter()\n");
	}

	// Get the image acquisition command
	switch (processing) {
	case UNPROCESSED:
		m_strCommand.append("Image img := SSCUnprocessedBinnedAcquire");
		break;
	case DARK_SUBTRACTED:
		m_strCommand.append("Image img := SSCDarkSubtractedBinnedAcquire");
		break;
	case GAIN_NORMALIZED:
		m_strCommand.append("Image img := SSCGainNormalizedBinnedAcquire");
		break;
	case DARK_REFERENCE:
		m_strCommand.append("Image img := SSCGetDarkReference");
		break;
	}

	sprintf(m_strTemp, "(%f, %d, %d, %d, %d, %d)\n", exposure, binning, top, left, 
		bottom, right);
	m_strCommand.append(m_strTemp);
	
	// Restore drift settling to zero if it was set
	if (m_iDMVersion < OLD_SETTLING_BROKEN && settling > 0.)
		m_strCommand += "SetPersistentNumberNote(\"MSC:Parameters:2:Settling\", 0.)\n";
	
	// Final calls to retain image and return its ID
	sprintf(m_strTemp, "KeepImage(img)\n"
		"number retval = GetImageID(img)\n"
		"Exit(retval)");
	m_strCommand.append(m_strTemp);

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
	m_strCommand.append(m_strTemp);
	return AcquireAndTransferImage((void *)array, 4, arrSize, width, height);	
}

int TemplatePlugIn::AcquireAndTransferImage(void *array, int dataSize, long *arrSize, 
											long *width, long *height)
{
	long ID;
	DM::Image image;
	long outLimit = *arrSize;

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
		if (DM::ImageGetDataElementByteSize(image.get()) != dataSize) {
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
			memcpy(array, imageL.get(), *width * *height * dataSize);
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
	return 1;
}

// Determine insertion state of given camera: return -1 for error, 0 if out, 1 if in
int TemplatePlugIn::IsCameraInserted(long camera)
{
	m_strCommand.resize(0);
	AddCameraSelection(camera);
	return 1;
}

int TemplatePlugIn::InsertCamera(long camera, BOOL state)
{
	m_strCommand.resize(0);
	AddCameraSelection(camera);
	return 0;
}

// Global instances of the plugin and the wrapper class for calling into this file
TemplatePlugIn gTemplatePlugIn;

PlugInWrapper gPlugInWrapper;

// THE WRAPPER FUNCTIONS
PlugInWrapper::PlugInWrapper()
{
}

BOOL PlugInWrapper::GetPlugInRunning ()
{
	return gTemplatePlugIn.GetPlugInRunning();
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
