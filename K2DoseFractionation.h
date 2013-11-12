#ifndef K2_DOSEFRACACQUISITION_H
#define K2_DOSEFRACACQUISITION_H

#define _GATANPLUGIN_USES_LIBRARY_VERSION 2
#include "DMPlugInBasic.h"
#include "DMPlugInCamera.h"

class K2_DoseFracAcquisition
{
private:

public:
	K2_DoseFracAcquisition();
	Gatan::DM::ScriptObject m_DoseFracAcquisitionObj;

	// set image series alignment option: 1=align the frames in the series, 0=don?t align series
	void		SetAlignOption(long align);

	// set alignment filter; see settings dialog on K2 Dose Fractionation palette for filter options
	void		SetFilter(const std::string &filter);

	// hardware processing bit masks: 0=none, 2=dark, 4=gain
	void		SetHardwareProcessing(long processing);

	// set the exposure duration of each frame in the image series
	void		SetFrameExposure(double exposure);

	// async flag allows AcquireImage() call to return asynchronously,
	// to allow for immediate display of intermediate results, or
	// for pipelined processing of stack images
	void		SetAsyncOption(bool async);

	std::string GetFilter();
	long		GetAlignOption();
	long		GetHardwareProcessing();
	double		GetFrameExposure();
	bool		GetAsyncOption();

	// acquire sum image only
	Gatan::DM::Image	AcquireImage(const Gatan::CM::CameraPtr &camera, const Gatan::CM::AcquisitionParametersPtr &acq_params);

	// acquire sum image and stack image
	Gatan::DM::Image	AcquireImage(const Gatan::CM::CameraPtr &camera, const Gatan::CM::AcquisitionParametersPtr &acq_params, Gatan::DM::Image &stackImage);

	// helper functions for asynchronous acquisition
	void		Abort();
	DWORD		GetNumFramesProcessed();
	bool		IsDone();

};

#endif // K2_DOSEFRACACQUISITION_H
