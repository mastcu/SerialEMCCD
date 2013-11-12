#include "stdafx.h"
#include "K2DoseFractionation.h"

using namespace Gatan;
using namespace PlugIn;

K2_DoseFracAcquisition::K2_DoseFracAcquisition()
{
	PlugIn::DM_PlugInEnv dm_env;
	m_DoseFracAcquisitionObj = DM::NewScriptObjectFromNamedClass( dm_env.get(), "k2_dosefracacquisition" );

	if (!m_DoseFracAcquisitionObj.IsValid())
		throw(std::runtime_error("Failed to create K2_DoseFracAcquisition script object!"));
}

void K2_DoseFracAcquisition::SetFilter(const std::string &filter)
{
	DM::String filterStr(filter);

	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_SetFilter(ScriptObject, dm_string)";

	Gatan::PlugIn::DM_Variant params[2];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[1].v_object = (DM_ObjectToken) filterStr.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );
}

// 0 = no align, 1 = aligned sum
void K2_DoseFracAcquisition::SetAlignOption(long align)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_SetAlignOption(ScriptObject, long)";

	Gatan::PlugIn::DM_Variant params[2];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[1].v_sint32 = align;

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );
}
	
// hardware processing bit masks: 0=none, 2=dark, 4=gain
void K2_DoseFracAcquisition::SetHardwareProcessing(long processing)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_SetHardwareProcessing(ScriptObject, long)";

	Gatan::PlugIn::DM_Variant params[2];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[1].v_sint32 = processing;

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );
}

void K2_DoseFracAcquisition::SetFrameExposure(double exposure)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_SetFrameExposure(ScriptObject, double)";

	Gatan::PlugIn::DM_Variant params[2];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[1].v_float64 = exposure;

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );
}

// async flag allows AcquireImage() call to return asynchronously,
// to allow for immediate display of intermediate results, or
// for pipelined processing of stack images
void K2_DoseFracAcquisition::SetAsyncOption(bool async)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_SetAsyncOption(ScriptObject, bool)";

	Gatan::PlugIn::DM_Variant params[2];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[1].v_binary8 = async;

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );
}

std::string K2_DoseFracAcquisition::GetFilter()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "dm_string DoseFrac_GetFilter(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	DM::String filterStr((DM_StringToken) params[0].v_object);
	return filterStr.get_string_as<std::string>();
}

long K2_DoseFracAcquisition::GetAlignOption()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "long DoseFrac_GetAlignOption(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_sint32;
}

long K2_DoseFracAcquisition::GetHardwareProcessing()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "long DoseFrac_GetProcessing(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_sint32;
}
double K2_DoseFracAcquisition::GetFrameExposure()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "double DoseFrac_GetFrameExposure(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_float64;
}
bool K2_DoseFracAcquisition::GetAsyncOption()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "bool DoseFrac_GetAsyncOption(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_binary8;
}

// acquire sum image only
DM::Image K2_DoseFracAcquisition::AcquireImage(const CM::CameraPtr &camera, const CM::AcquisitionParametersPtr &acq_params)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "BasicImage DoseFrac_AcquireImage(ScriptObject, ScriptObject, ScriptObject)";

	Gatan::PlugIn::DM_Variant params[4];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[2].v_object = (DM_ObjectToken) camera.get();
	params[3].v_object = (DM_ObjectToken) acq_params.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 4, params, __sSignature );

	DM::Image sumImage = (DM_ImageToken_1Ref) params[0].v_object;

	return sumImage;
}

// acquire sum image and stack image
DM::Image K2_DoseFracAcquisition::AcquireImage(const CM::CameraPtr &camera, const CM::AcquisitionParametersPtr &acq_params, DM::Image &stackImage)
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "BasicImage DoseFrac_AcquireImage(ScriptObject, ScriptObject, ScriptObject, BasicImage*)";

	Gatan::PlugIn::DM_Variant params[5];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();
	params[2].v_object = (DM_ObjectToken) camera.get();
	params[3].v_object = (DM_ObjectToken) acq_params.get();
	params[4].v_object_ref = (DM_ObjectToken*) stackImage.get_ptr();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 5, params, __sSignature );

	DM::Image sumImage = (DM_ImageToken_1Ref) params[0].v_object;

	return sumImage;
}

// helper functions for asynchronous acquisition
void K2_DoseFracAcquisition::Abort()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "void DoseFrac_Abort(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[1];
	params[0].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 1, params, __sSignature );
}

DWORD K2_DoseFracAcquisition::GetNumFramesProcessed()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "uint32 DoseFrac_GetNumFramesProcessed(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_uint32;
}

bool K2_DoseFracAcquisition::IsDone()
{
	static DM::Function __sFunction = (DM_FunctionToken) NULL;
	static const char *__sSignature = "bool DoseFrac_IsDone(ScriptObject)";

	Gatan::PlugIn::DM_Variant params[2];
	params[1].v_object = (DM_ObjectToken) m_DoseFracAcquisitionObj.get();

	GatanPlugIn::gDigitalMicrographInterface.CallFunction( __sFunction.get_ptr(), 2, params, __sSignature );

	return params[0].v_binary8;
}
