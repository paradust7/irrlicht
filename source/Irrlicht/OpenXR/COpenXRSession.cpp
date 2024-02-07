#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRConnector.h"
#include "IOpenXRSession.h"
#include "IOpenXRSwapchain.h"
#include "Common.h"
#include "OpenXRMath.h"

#include <SDL_video.h>

using std::unique_ptr;

uint64_t XrFrameCounter = 0;

namespace irr {

class COpenXRSession : public IOpenXRSession {
public:
	COpenXRSession(
		XrInstance instance,
		video::IVideoDriver* driver,
		XrReferenceSpaceType playSpaceType)
		: Instance(instance), VideoDriver(driver), PlaySpaceType(playSpaceType)
	{
		VideoDriver->grab();
	}
	virtual ~COpenXRSession() {
		// Order is important!
		ViewLayers.clear();
		for (auto& viewChain : ViewChains) {
			for (auto target : viewChain.RenderTargets) {
				if (target)
					VideoDriver->removeRenderTarget(target);
			}
			viewChain.RenderTargets.clear();
		}
		ViewChains.clear();
		if (ViewSpace != XR_NULL_HANDLE)
			xrDestroySpace(ViewSpace);
		if (PlaySpace != XR_NULL_HANDLE)
			xrDestroySpace(PlaySpace);
		if (Session != XR_NULL_HANDLE)
			xrDestroySession(Session);
		VideoDriver->drop();
	}

	virtual void recenter() override;
	virtual bool internalTryBeginFrame(bool *didBegin, int64_t *predicted_time_delta) override;
	virtual bool internalNextView(bool *gotView, core::XrViewInfo* info) override;
	virtual bool handleStateChange(XrEventDataSessionStateChanged *ev) override;
	bool init();
	bool endFrame();
protected:
	bool getSystem();
	bool getViewConfigs();
	bool setupViews();
	bool verifyGraphics();
	bool createSession();
	bool setupSpaces();
	bool beginSession();
	bool setupSwapchains();
	bool setupCompositionLayers();

	bool recenterPlaySpace(XrTime ref);

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

	static constexpr XrPosef Identity = {
                .orientation = {.x = 0, .y = 0, .z = 0, .w = 1.0},
                .position = {.x = 0, .y = 0, .z = 0},
	};

	// Set by setupSpaces()
	XrSpace PlaySpace = XR_NULL_HANDLE;
	XrPosef PlaySpaceOffset = Identity;
	float YawOffset = 0.0f;
	XrSpace ViewSpace = XR_NULL_HANDLE;
	bool DoRecenter = false;

	// Initialized by getSwapchainFormats
	// Ordered by optimal performance/quality (best first)
	std::vector<int64_t> SupportedFormats;
	int64_t ColorFormat;
	int64_t DepthFormat;
	float ZNear = 1.0f;
	float ZFar = 20000.f;

	struct ViewChainData {
		// Initialized by setupSwapchains
                unique_ptr<IOpenXRSwapchain> Swapchain;
                unique_ptr<IOpenXRSwapchain> DepthSwapchain;

		// JANK ALERT
		// IRenderTarget groups together a framebuffer (FBO), texture, and depth/stencil texture.
		// But OpenXR acquires textures and depth textures independently. Their association is
		// not permanent.
		//
		// As a compromise, these render targets will always be bound to the same FBO and texture,
		// but their depth texture may be updated every frame.
		std::vector<video::IRenderTarget*> RenderTargets;

		// Initialized by setupCompositionLayers
		// `Layers` holds pointers to these structs
		XrCompositionLayerDepthInfoKHR DepthInfo;
	};
	std::vector<ViewChainData> ViewChains;

	// Initialized by setupCompositionLayers
	std::vector<XrCompositionLayerProjectionView> ViewLayers;

	// ----------------------------------------------
	// These are only valid when InFrame is true
	bool InFrame = false;
	uint32_t NextViewIndex = 0;
	XrFrameState FrameState;
	XrViewState ViewState;
	std::vector<XrView> ViewInfo;
	// ----------------------------------------------

	bool check(XrResult result, const char* func)
	{
		return openxr_check(Instance, result, func);
	}
};

bool COpenXRSession::init()
{
	if (!getSystem()) return false;
	if (!getViewConfigs()) return false;
	if (!setupViews()) return false;
	if (!verifyGraphics()) return false;
	if (!createSession()) return false;
	// TODO: Initialize hand tracking
	if (!setupSpaces()) return false;
	if (!beginSession()) return false;
	if (!setupSwapchains()) return false;
	if (!setupCompositionLayers()) return false;
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

		XrGraphicsRequirementsOpenGLKHR reqs = {
			.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
		};
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

		XrGraphicsRequirementsOpenGLESKHR reqs = {
			.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
		};
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

bool COpenXRSession::setupSpaces()
{
	XR_ASSERT(PlaySpace == XR_NULL_HANDLE);
	XrReferenceSpaceCreateInfo createInfo = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = PlaySpaceType,
		.poseInReferenceSpace = PlaySpaceOffset,
	};
	XR_CHECK(xrCreateReferenceSpace, Session, &createInfo, &PlaySpace);

	XR_ASSERT(ViewSpace == XR_NULL_HANDLE);
	XrReferenceSpaceCreateInfo viewCreateInfo = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
		.poseInReferenceSpace = Identity,
	};
	XR_CHECK(xrCreateReferenceSpace, Session, &viewCreateInfo, &ViewSpace);
	return true;
}

bool COpenXRSession::recenterPlaySpace(XrTime ref)
{
	XrSpaceLocation location = {
		.type = XR_TYPE_SPACE_LOCATION,
	};
	XR_CHECK(xrLocateSpace, ViewSpace, PlaySpace, ref, &location);
	bool validPosition = location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT;
	bool validOrientation = location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

	// Quietly do nothing if there's incomplete data
	if (!validPosition || !validOrientation)
		return true;

	// For recentering, only the 'yaw' matters, because the runtime guarantees
	// that the XZ plane is parallel with the floor.
	XrVector3f forward = quatApply(location.pose.orientation, XrVector3f{0, 0, 1});
	float yaw = atan2f(forward.x, forward.z);
	YawOffset = fmodf(YawOffset + yaw, 2 * M_PI);
	PlaySpaceOffset.orientation = XrQuaternionf{0, sinf(YawOffset/2), 0, cosf(YawOffset/2)};
	xrDestroySpace(PlaySpace);
	PlaySpace = XR_NULL_HANDLE;
	xrDestroySpace(ViewSpace);
	ViewSpace = XR_NULL_HANDLE;
	if (!setupSpaces())
		return false;
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
		"[XR] ColorFormat %d (%s)", (int32_t)ColorFormat,
		(ColorFormat == GL_SRGB8_ALPHA8) ? "GL_SRGB8_ALPHA8" : "unknown");
	os::Printer::log(buf, ELL_INFORMATION);
	snprintf_irr(buf, sizeof(buf),
		"[XR] DepthFormat %d (%s)", (int32_t)DepthFormat,
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
	ViewChains.resize(viewCount);
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		auto& viewChain = ViewChains[viewIndex];
		viewChain.Swapchain =
			createOpenXRSwapchain(
				VideoDriver,
				Instance,
				Session,
				XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
				ColorFormat,
				ViewConfigs[viewIndex].recommendedSwapchainSampleCount,
				ViewConfigs[viewIndex].recommendedImageRectWidth,
				ViewConfigs[viewIndex].recommendedImageRectHeight);
		if (!viewChain.Swapchain)
			return false;
		viewChain.DepthSwapchain =
			createOpenXRSwapchain(
				VideoDriver,
				Instance,
				Session,
				XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				DepthFormat,
				ViewConfigs[viewIndex].recommendedSwapchainSampleCount,
				ViewConfigs[viewIndex].recommendedImageRectWidth,
				ViewConfigs[viewIndex].recommendedImageRectHeight);
		if (!viewChain.DepthSwapchain)
			return false;
		size_t swapchainLength = viewChain.Swapchain->getLength();
		// These are added as needed
		viewChain.RenderTargets.resize(swapchainLength, nullptr);
	}

	// Fill in layers

	return true;
}

bool COpenXRSession::setupCompositionLayers()
{
	size_t viewCount = ViewConfigs.size();
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		auto& depthInfo = ViewChains[viewIndex].DepthInfo;
		depthInfo = XrCompositionLayerDepthInfoKHR{
			.type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
			.next = NULL,
			.minDepth = 0.f,
			.maxDepth = 1.f,
			.nearZ = ZNear,
			.farZ = ZFar,
		};
		depthInfo.subImage.swapchain = ViewChains[viewIndex].DepthSwapchain->getHandle();
		depthInfo.subImage.imageArrayIndex = 0;
		depthInfo.subImage.imageRect.offset.x = 0;
		depthInfo.subImage.imageRect.offset.y = 0;
		depthInfo.subImage.imageRect.extent.width = ViewConfigs[viewIndex].recommendedImageRectWidth;
		depthInfo.subImage.imageRect.extent.height = ViewConfigs[viewIndex].recommendedImageRectHeight;
	}

	// Fill out projection views
	ViewLayers.resize(viewCount);
	for (size_t viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		auto& layerInfo = ViewLayers[viewIndex];
		layerInfo = XrCompositionLayerProjectionView{
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
			.next = NULL, // &ViewChains[viewIndex].DepthInfo,
			// TODO(paradust): Determine why this breaks SteamVR
		};
		layerInfo.subImage.swapchain = ViewChains[viewIndex].Swapchain->getHandle();
		layerInfo.subImage.imageArrayIndex = 0;
		layerInfo.subImage.imageRect.offset.x = 0;
		layerInfo.subImage.imageRect.offset.y = 0;
		layerInfo.subImage.imageRect.extent.width = ViewConfigs[viewIndex].recommendedImageRectWidth;
		layerInfo.subImage.imageRect.extent.height = ViewConfigs[viewIndex].recommendedImageRectHeight;
		// pose and fov are filled in at the beginning of each frame
	}
	return true;
}

void COpenXRSession::recenter()
{
	DoRecenter = true;
}

bool COpenXRSession::internalTryBeginFrame(bool *didBegin, int64_t *predicted_time_delta)
{
	XR_ASSERT(!InFrame);

	FrameState = XrFrameState{
		.type = XR_TYPE_FRAME_STATE,
	};
	XrFrameWaitInfo waitInfo = {
		.type = XR_TYPE_FRAME_WAIT_INFO,
	};
	XR_CHECK(xrWaitFrame, Session, &waitInfo, &FrameState);

	XrFrameBeginInfo beginInfo = {
		.type = XR_TYPE_FRAME_BEGIN_INFO,
	};
	XR_CHECK(xrBeginFrame, Session, &beginInfo);
	*didBegin = true;
	InFrame = true;
	NextViewIndex = 0;

	if (DoRecenter && FrameState.shouldRender) {
		DoRecenter = false;
		if (!recenterPlaySpace(FrameState.predictedDisplayTime)) {
			return false;
		}
	}


	// TODO: Calculate
	*predicted_time_delta = 0;

	// TODO: Do hand tracking calculations need to happen in between waiting and beginning the frame?
	// And xrLocateViews, xrSyncActions, xrGetActionStatePose, xrLocateSpace, xrGetActionStateFloat, xrApplyHapticFeedback, etc

	// Get view location info for this frame
	XrViewLocateInfo viewLocateInfo = {
		.type = XR_TYPE_VIEW_LOCATE_INFO,
		.viewConfigurationType = ViewType,
		.displayTime = FrameState.predictedDisplayTime,
		.space = PlaySpace,
	};
	uint32_t viewCount = ViewConfigs.size();
	ViewInfo.resize(viewCount);
	for (size_t i = 0; i < viewCount; i++) {
		ViewInfo[i].type = XR_TYPE_VIEW;
		ViewInfo[i].next = NULL;
	}
	ViewState = XrViewState{
		.type = XR_TYPE_VIEW_STATE,
		.next = NULL,
	};
	XR_CHECK(xrLocateViews, Session, &viewLocateInfo, &ViewState, viewCount, &viewCount, ViewInfo.data());
	XR_ASSERT(viewCount == ViewConfigs.size());

	bool validPositions = ViewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT;
	bool validOrientations = ViewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT;

	if (!validPositions || !validOrientations) {
		FrameState.shouldRender = false;
	}

	if (FrameState.shouldRender) {
		// Fill in pose/fov info
		for (uint32_t i = 0; i < viewCount; i++) {
			ViewLayers[i].pose = ViewInfo[i].pose;
			ViewLayers[i].fov = ViewInfo[i].fov;
		}
	}
	return true;
}

bool COpenXRSession::internalNextView(bool *gotView, core::XrViewInfo* info)
{
	XR_ASSERT(InFrame);
	if (FrameState.shouldRender == XR_TRUE) {
		if (NextViewIndex < ViewChains.size()) {
			uint32_t viewIndex = NextViewIndex++;
			auto& viewChain = ViewChains[viewIndex];
			auto& viewConfig = ViewConfigs[viewIndex];
			if (!viewChain.Swapchain->acquireAndWait())
				return false;
			if (!viewChain.DepthSwapchain->acquireAndWait())
				return false;
			auto& target = viewChain.RenderTargets[viewChain.Swapchain->getAcquiredIndex()];
			if (!target) {
				os::Printer::log("[XR] Adding render target", ELL_INFORMATION);
				target = VideoDriver->addRenderTarget();
			}
			target->setTexture(
				viewChain.Swapchain->getAcquiredTexture(),
				viewChain.DepthSwapchain->getAcquiredTexture());
			const auto& viewInfo = ViewInfo[viewIndex];
			const auto& fov = viewInfo.fov;
			const auto& position = viewInfo.pose.position;
			const auto& orientation = viewInfo.pose.orientation;
			info->Kind = (viewIndex == 0) ? core::XRVK_LEFT_EYE : core::XRVK_RIGHT_EYE;
			info->Target = target;
			info->Width = viewConfig.recommendedImageRectWidth;
			info->Height = viewConfig.recommendedImageRectHeight;
			// RH -> LH coordinates
			info->Position = core::vector3df(position.x, position.y, -position.z);
			// RH -> LH coordinates + invert
			info->Orientation = core::quaternion(-orientation.x, -orientation.y, orientation.z, orientation.w);
			info->AngleLeft = fov.angleLeft;
			info->AngleRight = fov.angleRight;
			info->AngleUp = fov.angleUp;
			info->AngleDown = fov.angleDown;
			info->ZNear = ZNear;
			info->ZFar = ZFar;
			*gotView = true;
			return true;
		}

		// If we're here, we're about to end frame. So release all the swapchains.
		for (uint32_t viewIndex = 0; viewIndex < ViewChains.size(); ++viewIndex) {
			auto& viewChain = ViewChains[viewIndex];
			auto& target = viewChain.RenderTargets[viewChain.Swapchain->getAcquiredIndex()];
			XR_ASSERT(target->getReferenceCount() == 1);
			if (!viewChain.Swapchain->release())
				return false;
			if (!viewChain.DepthSwapchain->release())
				return false;
		}

	}

	// End the frame and submit all layers for rendering
	if (!endFrame())
		return false;
	*gotView = false;
	NextViewIndex = 0;
	return true;
}

static const char* state_label(XrSessionState state)
{
	switch (state) {
	case XR_SESSION_STATE_IDLE: return "idle";
	case XR_SESSION_STATE_READY: return "ready";
	case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
	case XR_SESSION_STATE_VISIBLE: return "visible";
	case XR_SESSION_STATE_FOCUSED: return "focused";
	case XR_SESSION_STATE_STOPPING: return "stopping";
	case XR_SESSION_STATE_LOSS_PENDING: return "loss_pending";
	case XR_SESSION_STATE_EXITING: return "exiting";
	default: return "Unknown";
	}
}

bool COpenXRSession::handleStateChange(XrEventDataSessionStateChanged *ev)
{
	const char* label = state_label(ev->state);
	char buf[128];
	snprintf_irr(buf, sizeof(buf), "[XR] Session state changed to `%s`", label);
	os::Printer::log(buf, ELL_INFORMATION);
	return true;
}

bool COpenXRSession::endFrame()
{
	XR_ASSERT(InFrame);
	XrCompositionLayerProjection projectionLayer = {
		.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
		.next = NULL,
		.layerFlags = 0,
		.space = PlaySpace,
		.viewCount = (uint32_t)ViewLayers.size(),
		.views = ViewLayers.data(),
	};

	uint32_t layerCount = 0;
	const XrCompositionLayerBaseHeader* layers[5];
	if (FrameState.shouldRender) {
		layers[layerCount++] = (XrCompositionLayerBaseHeader*)&projectionLayer;
	}
	XrFrameEndInfo frameEndInfo = {
		.type = XR_TYPE_FRAME_END_INFO,
		.next = NULL,
		.displayTime = FrameState.predictedDisplayTime,
		.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		.layerCount = layerCount,
		.layers = layers,
	};
	XR_CHECK(xrEndFrame, Session, &frameEndInfo);
	InFrame = false;
	++XrFrameCounter;
	return true;
}

unique_ptr<IOpenXRSession> createOpenXRSession(
	XrInstance instance,
	video::IVideoDriver* driver,
	XrReferenceSpaceType playSpaceType)
{
	unique_ptr<COpenXRSession> obj(new COpenXRSession(instance, driver, playSpaceType));
	if (!obj->init())
		return nullptr;
	return obj;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_

