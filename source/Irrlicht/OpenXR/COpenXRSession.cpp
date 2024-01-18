#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRSession.h"
#include "IOpenXRSwapchain.h"
#include "Common.h"

#include <SDL_video.h>

using std::unique_ptr;

namespace irr {

class COpenXRSession : public IOpenXRSession {
public:
	COpenXRSession(
		XrInstance instance,
		video::IVideoDriver* driver,
		XrReferenceSpaceType playSpaceType)
		: Instance(instance), VideoDriver(driver), PlaySpaceType(playSpaceType)
	{
	}
	virtual ~COpenXRSession() {
		if (PlaySpace != XR_NULL_HANDLE)
			xrDestroySpace(PlaySpace);
		if (Session != XR_NULL_HANDLE)
			xrDestroySession(Session);
	}

	virtual bool TryBeginFrame(int64_t *predicted_time_delta);
	virtual bool NextView(ViewRenderInfo *info);
	bool Init();
protected:
	bool getSystem();
	bool getViewConfigs();
	bool setupViews();
	bool verifyGraphics();
	bool createSession();
	bool setupPlaySpace();
	bool beginSession();
	bool setupSwapchains();
	bool setupCompositionLayers();
protected:
	XrInstance Instance;
	video::IVideoDriver* VideoDriver;
	XrReferenceSpaceType PlaySpaceType;

	// System
	XrSystemId SystemId = XR_NULL_SYSTEM_ID;
	XrSystemProperties SystemProps;

	// Supported View Configurations (mono, stereo, etc)
	std::vector<XrViewConfigurationType> ViewConfigTypes;
	std::vector<XrViewConfigurationProperties> ViewConfigProperties;

	XrSession Session = XR_NULL_HANDLE;

	// Parameters for the view config we're using
	// For stereo, this has left and right eyes
	XrViewConfigurationType ViewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	std::vector<XrViewConfigurationView> ViewConfigs;

	// Set by setupPlaySpace()
	XrSpace PlaySpace = XR_NULL_HANDLE;

	// Initialized by getSwapchainFormats
	// Ordered by optimal performance/quality (best first)
	std::vector<int64_t> SupportedFormats;
	int64_t ColorFormat;
	int64_t DepthFormat;
	float NearZ = 0.01f;
	float FarZ = 100.f;

	struct ViewStateData {
		// Initialized by setupSwapchains
                unique_ptr<IOpenXRSwapchain> Swapchain;
                unique_ptr<IOpenXRSwapchain> DepthSwapchain;

		// Initialized by setupCompositionLayers
		// `Layers` holds pointers to these structs
		XrCompositionLayerDepthInfoKHR DepthInfo;
	};
	std::vector<ViewStateData> ViewState;

	// Initialized by setupCompositionLayers
	std::vector<XrCompositionLayerProjectionView> Layers;

	bool check(XrResult result, const char* func)
	{
		return openxr_check(Instance, result, func);
	}
};

bool COpenXRSession::Init()
{
	if (!getSystem()) return false;
	if (!getViewConfigs()) return false;
	if (!setupViews()) return false;
	if (!verifyGraphics()) return false;
	if (!createSession()) return false;
	// TODO: Initialize hand tracking
	if (!setupPlaySpace()) return false;
	if (!beginSession()) return false;
	if (!setupSwapchains()) return false;
	// TODO: Setup actions
	return true;
}

bool COpenXRSession::getSystem()
{
	XrFormFactor formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemGetInfo getInfo = {
		.type = XR_TYPE_SYSTEM_GET_INFO,
		.formFactor = formFactor,
	};
	XR_CHECK(xrGetSystem, Instance, &getInfo, &SystemId);

	SystemProps = XrSystemProperties{
		.type = XR_TYPE_SYSTEM_PROPERTIES,
	};
	XR_CHECK(xrGetSystemProperties, Instance, SystemId, &SystemProps);

	// Print out information about the system
	char buf[128 + XR_MAX_SYSTEM_NAME_SIZE];
	snprintf_irr(buf, sizeof(buf), "[XR] HMD: %s", SystemProps.systemName);
	os::Printer::log(buf, ELL_INFORMATION);

	snprintf_irr(buf, sizeof(buf), "[XR] Vendor id: %u", SystemProps.vendorId);
	os::Printer::log(buf, ELL_INFORMATION);

	snprintf_irr(buf, sizeof(buf), "[XR] Graphics: max swapchain %u x %u; %u composition layers",
		SystemProps.graphicsProperties.maxSwapchainImageWidth,
		SystemProps.graphicsProperties.maxSwapchainImageHeight,
		SystemProps.graphicsProperties.maxLayerCount);
	os::Printer::log(buf, ELL_INFORMATION);

	const char *tracking = "None";
	bool orientationTracking = SystemProps.trackingProperties.orientationTracking;
	bool positionTracking = SystemProps.trackingProperties.positionTracking;
	if (orientationTracking && positionTracking)
		tracking = "Orientation and Position";
	else if (orientationTracking)
		tracking = "Orientation only";
	else if (positionTracking)
		tracking = "Position only";
	snprintf_irr(buf, sizeof(buf), "[XR] Tracking: %s", tracking);
	os::Printer::log(buf, ELL_INFORMATION);

	return true;
}

bool COpenXRSession::getViewConfigs()
{
	uint32_t count = 0;
	XR_CHECK(xrEnumerateViewConfigurations, Instance, SystemId, 0, &count, NULL);

	ViewConfigTypes.clear();
	ViewConfigTypes.resize(count);
	XR_CHECK(xrEnumerateViewConfigurations, Instance, SystemId, count, &count, ViewConfigTypes.data());
	ViewConfigTypes.resize(count);

	// Fetch viewconfig properties
	ViewConfigProperties.clear();
	ViewConfigProperties.resize(count, {
		.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES,
	});
	for (uint32_t i = 0; i < count; i++) {
		XR_CHECK(xrGetViewConfigurationProperties, Instance, SystemId, ViewConfigTypes[i], &ViewConfigProperties[i]);
	}

	// Print out some info
	for (const auto &prop : ViewConfigProperties) {
		char buf[128];
		const char *view = "other";
		switch (prop.viewConfigurationType) {
		case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO: view = "mono"; break;
		case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO: view = "stereo"; break;
		default: break;
		}
		snprintf_irr(buf, sizeof(buf), "[XR] Supported view: %s [type=%d, fovMutable=%s]",
			view, prop.viewConfigurationType, prop.fovMutable ? "yes" : "no");
		os::Printer::log(buf, ELL_INFORMATION);
	}
	return true;
}

bool COpenXRSession::setupViews()
{
	uint32_t count = 0;
	XR_CHECK(xrEnumerateViewConfigurationViews, Instance, SystemId, ViewType, 0, &count, NULL);

	ViewConfigs.clear();
	ViewConfigs.resize(count, { .type = XR_TYPE_VIEW_CONFIGURATION_VIEW});
	XR_CHECK(xrEnumerateViewConfigurationViews, Instance, SystemId, ViewType, count, &count, ViewConfigs.data());
	ViewConfigs.resize(count);

	// Print out info
	os::Printer::log("[XR] Using stereo view", ELL_INFORMATION);
	for (uint32_t i = 0; i < ViewConfigs.size(); i++) {
		const auto &conf = ViewConfigs[i];
		char buf[256];
		snprintf_irr(buf, sizeof(buf),
			"[XR] View %d: Recommended/Max Resolution %dx%d/%dx%d, Swapchain samples %d/%d",
			i,
			conf.recommendedImageRectWidth,
			conf.recommendedImageRectHeight,
			conf.maxImageRectWidth,
			conf.maxImageRectHeight,
			conf.recommendedSwapchainSampleCount,
			conf.maxSwapchainSampleCount);
		os::Printer::log(buf, ELL_INFORMATION);
	}
	return true;

}

bool COpenXRSession::verifyGraphics()
{
	// OpenXR requires checking graphics compatibility before creating a session.
	// xrGetInstanceProcAddr must be used, since these methods might load in dynamically.
	XrVersion minApiVersionSupported = 0;
	XrVersion maxApiVersionSupported = 0;
	bool gles = false;

#ifdef XR_USE_GRAPHICS_API_OPENGL
	{
		PFN_xrGetOpenGLGraphicsRequirementsKHR pfn_xrGetOpenGLGraphicsRequirementsKHR = nullptr;
		XR_CHECK(xrGetInstanceProcAddr, Instance, "xrGetOpenGLGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)&pfn_xrGetOpenGLGraphicsRequirementsKHR);

		XrGraphicsRequirementsOpenGLKHR reqs;
		XR_CHECK(pfn_xrGetOpenGLGraphicsRequirementsKHR, Instance, SystemId, &reqs);
		minApiVersionSupported = reqs.minApiVersionSupported;
		maxApiVersionSupported = reqs.maxApiVersionSupported;
	}
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
	{
		PFN_xrGetOpenGLESGraphicsRequirementsKHR pfn_xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
		XR_CHECK(xrGetInstanceProcAddr, Instance, "xrGetOpenGLESGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)&pfn_xrGetOpenGLESGraphicsRequirementsKHR);

		XrGraphicsRequirementsOpenGLESKHR reqs;
		XR_CHECK(pfn_xrGetOpenGLESGraphicsRequirementsKHR, Instance, SystemId, &reqs);
		minApiVersionSupported = reqs.minApiVersionSupported;
		maxApiVersionSupported = reqs.maxApiVersionSupported;
		gles = true;
	}
#endif

	char buf[128];
	snprintf_irr(buf, sizeof(buf),
		"[XR] OpenXR supports OpenGL%s version range (%d.%d.%d, %d.%d.%d)",
		gles ? "ES" : "",
		XR_VERSION_MAJOR(minApiVersionSupported),
		XR_VERSION_MINOR(minApiVersionSupported),
		XR_VERSION_PATCH(minApiVersionSupported),
		XR_VERSION_MAJOR(maxApiVersionSupported),
		XR_VERSION_MINOR(maxApiVersionSupported),
		XR_VERSION_PATCH(maxApiVersionSupported));
	os::Printer::log(buf, ELL_INFORMATION);

	int glmajor = 0;
	int glminor = 0;
	int glmask = 0;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &glmajor);
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &glminor);
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &glmask);
	XrVersion sdl_gl_version = XR_MAKE_VERSION(glmajor, glminor, 0);
	bool is_gles = glmask & SDL_GL_CONTEXT_PROFILE_ES;

	snprintf_irr(buf, sizeof(buf),
		"[XR] SDL is configured for OpenGL%s %d.%d.%d",
		is_gles ? "ES" : "",
		glmajor,
		glminor,
		glmask);
	os::Printer::log(buf, ELL_INFORMATION);

	if (is_gles != gles) {
		os::Printer::log("[XR] Unexpected profile mismatch (OpenGL vs. OpenGLES)", ELL_ERROR);
		return false;
	}

	if (sdl_gl_version < minApiVersionSupported || sdl_gl_version > maxApiVersionSupported) {
		os::Printer::log("[XR] OpenGL initialized with incompatible version", ELL_ERROR);
		return false;
	}
	return true;
}

// SDL and OpenXR don't know how to talk to each other
//
// For them to work together, it is necessary to pass
// the raw GL/display context from SDL to OpenXR.
//
// SDL doesn't expose this, so it has to be pulled
// directly from the underlying api:
//
//     Windows + OpenGL         -> WGL
//     X11 + OpenGL             -> GLX
//     OpenGLES, WebGL, Wayland -> EGL
//     OS X + OpenGL            -> CGL
//
// This is pretty fragile, since the API we query has
// to match the one SDL is using exactly.
//
// If SDL is compiled to support both GL and GLES, then it
// could potentially use GLX or EGL on X11. For now this
// code assumes that platforms with GLES support will only
// use EGL. If this turns out to not always be the case, it
// might make sense to use SDL_HINT_VIDEO_X11_FORCE_EGL to
// make it certain.
bool COpenXRSession::createSession()
{
	XrSessionCreateInfo session_create_info = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.next = nullptr, // to be filled in
		.systemId = SystemId,
	};

	const char* raw_sdl_driver = SDL_GetCurrentVideoDriver();
	std::string sdl_driver = raw_sdl_driver ? raw_sdl_driver : "";

#ifdef XR_USE_PLATFORM_WIN32
	if (sdl_driver != "windows") {
		os::Printer::log("[XR] Expected SDL driver 'windows'", ELL_ERROR);
		return false;
	}

	XrGraphicsBindingOpenGLWin32KHR binding{
		.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
	};
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
	session_create_info.next = &binding;

#endif

#ifdef XR_USE_PLATFORM_XLIB
	if (sdl_driver != "x11") {
		os::Printer::log("[XR] Expected SDL driver 'x11'", ELL_ERROR);
		return false;
	}
	XrGraphicsBindingOpenGLXlibKHR binding{
		.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
	};
	binding.xDisplay = XOpenDisplay(NULL);
	binding.glxContext = glXGetCurrentContext();
	binding.glxDrawable = glXGetCurrentDrawable();
	session_create_info.next = &binding;
#endif

#ifdef XR_USE_PLATFORM_EGL
#error "Not implemented"
#endif
	XR_CHECK(xrCreateSession, Instance, &session_create_info, &Session);
	return true;
}

bool COpenXRSession::setupPlaySpace()
{
	XrPosef identity = {
		.orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
		.position = {.x = 0, .y = 0, .z = 0},
	};
	XrReferenceSpaceCreateInfo create_info = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = PlaySpaceType,
		.poseInReferenceSpace = identity,
	};
	XR_CHECK(xrCreateReferenceSpace, Session, &create_info, &PlaySpace);
	return true;
}

bool COpenXRSession::beginSession()
{
	XrSessionBeginInfo session_begin_info = {
		.type = XR_TYPE_SESSION_BEGIN_INFO,
		.primaryViewConfigurationType = ViewType,
	};
	XR_CHECK(xrBeginSession, Session, &session_begin_info);
	return true;
}

bool COpenXRSession::setupSwapchains()
{
	uint32_t count;
	XR_CHECK(xrEnumerateSwapchainFormats, Session, 0, &count, NULL);

	SupportedFormats.resize(count);
	XR_CHECK(xrEnumerateSwapchainFormats, Session, count, &count, SupportedFormats.data());
	SupportedFormats.resize(count);

	// Choose the color and depth formats
	// TODO: Determine the range of formats that need to be supported here.
	int64_t preferred_format = GL_SRGB8_ALPHA8;
	int64_t preferred_depth_format = GL_DEPTH_COMPONENT32F;
	ColorFormat = SupportedFormats[0];
	DepthFormat = -1;
	for (const auto& format : SupportedFormats) {
		if (format == preferred_format) {
			ColorFormat = format;
		}
		if (format == preferred_depth_format) {
			DepthFormat = format;
		}
	}
	char buf[128];
	snprintf_irr(buf, sizeof(buf),
		"[XR] ColorFormat %" PRId64 " (%s)", ColorFormat,
		(ColorFormat == GL_SRGB8_ALPHA8) ? "GL_SRGB8_ALPHA8" : "unknown");
	os::Printer::log(buf, ELL_INFORMATION);
	snprintf_irr(buf, sizeof(buf),
		"[XR] DepthFormat %" PRId64 " (%s)", DepthFormat,
		(ColorFormat == GL_DEPTH_COMPONENT32F) ? "GL_DEPTH_COMPONENT32F" : "unknown");
	os::Printer::log(buf, ELL_INFORMATION);
	if (ColorFormat != preferred_format) {
		os::Printer::log("[XR] Using non-preferred color format", ELL_WARNING);
	}
	if (DepthFormat == -1) {
		os::Printer::log("[XR] Couldn't find valid depth buffer format", ELL_ERROR);
		return false;
	}

	// Make swapchain and depth swapchain for each view
	size_t viewCount = ViewConfigs.size();
	ViewState.resize(viewCount);
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		ViewState[viewIndex].Swapchain =
			createOpenXRSwapchain(
				Instance,
				Session,
				XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
				ColorFormat,
				ViewConfigs[viewIndex].recommendedSwapchainSampleCount,
				ViewConfigs[viewIndex].recommendedImageRectWidth,
				ViewConfigs[viewIndex].recommendedImageRectHeight);
		if (!ViewState[viewIndex].Swapchain)
			return false;
		ViewState[viewIndex].DepthSwapchain =
			createOpenXRSwapchain(
				Instance,
				Session,
				XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				DepthFormat,
				ViewConfigs[viewIndex].recommendedSwapchainSampleCount,
				ViewConfigs[viewIndex].recommendedImageRectWidth,
				ViewConfigs[viewIndex].recommendedImageRectHeight);
		if (!ViewState[viewIndex].DepthSwapchain)
			return false;
	}

	// Fill in layers

	return true;
}

bool COpenXRSession::setupCompositionLayers()
{
	size_t viewCount = ViewConfigs.size();
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		auto& depthInfo = ViewState[viewIndex].DepthInfo;
		depthInfo = XrCompositionLayerDepthInfoKHR{
			.type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
			.next = NULL,
			.minDepth = 0.f,
			.maxDepth = 1.f,
			.nearZ = NearZ,
			.farZ = FarZ,
		};
		depthInfo.subImage.swapchain = ViewState[viewIndex].DepthSwapchain->getHandle();
		depthInfo.subImage.imageArrayIndex = 0;
		depthInfo.subImage.imageRect.offset.x = 0;
		depthInfo.subImage.imageRect.offset.y = 0;
		depthInfo.subImage.imageRect.extent.width = ViewConfigs[viewIndex].recommendedImageRectWidth;
		depthInfo.subImage.imageRect.extent.height = ViewConfigs[viewIndex].recommendedImageRectHeight;
	}

	// Fill out projection views
	Layers.resize(viewCount);
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		auto& layerInfo = Layers[viewIndex];
		layerInfo = XrCompositionLayerProjectionView{
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			.next = &ViewState[viewIndex].DepthInfo,
		};
		layerInfo.subImage.swapchain = ViewState[viewIndex].Swapchain->getHandle();
		layerInfo.subImage.imageArrayIndex = 0;
		layerInfo.subImage.imageRect.offset.x = 0;
		layerInfo.subImage.imageRect.offset.y = 0;
		layerInfo.subImage.imageRect.extent.width = ViewConfigs[viewIndex].recommendedImageRectWidth;
		layerInfo.subImage.imageRect.extent.height = ViewConfigs[viewIndex].recommendedImageRectHeight;
		// pose and fov are filled in at the beginning of each frame
	}
	return true;
}

bool COpenXRSession::TryBeginFrame(int64_t *predicted_time_delta)
{

	XrFrameState frameState = {
		.type = XR_TYPE_FRAME_STATE,
	};
	XrFrameWaitInfo waitInfo = {
		.type = XR_TYPE_FRAME_WAIT_INFO,
	};
	XR_CHECK(xrWaitFrame, Session, &waitInfo, &frameState);

	// TODO: Calculate
	*predicted_time_delta = 0;

	// TODO: Do hand tracking calculations need to happen in between waiting and beginning the frame?
	// And xrLocateViews, xrSyncActions, xrGetActionStatePose, xrLocateSpace, xrGetActionStateFloat, xrApplyHapticFeedback, etc


	XrFrameBeginInfo beginInfo = {
		.type = XR_TYPE_FRAME_BEGIN_INFO,
	};
	XR_CHECK(xrBeginFrame, Session, &beginInfo);
	return true;
}

bool COpenXRSession::NextView(ViewRenderInfo *info)
{
	// TODO
	return true;
}

unique_ptr<IOpenXRSession> createOpenXRSession(
	XrInstance instance,
	video::IVideoDriver* driver,
	XrReferenceSpaceType playSpaceType)
{
	unique_ptr<COpenXRSession> obj(new COpenXRSession(instance, driver, playSpaceType));
	if (!obj->Init())
		return nullptr;
	return obj;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_

