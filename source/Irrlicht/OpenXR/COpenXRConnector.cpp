

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRConnector.h"
#include <unordered_set>
#include <SDL_video.h>

// See COpenXRConnector::createSession() for why this is needed.
#if defined(WIN32)
#	define XR_USE_PLATFORM_WIN32
#	define XR_USE_GRAPHICS_API_OPENGL
#elif defined(_IRR_COMPILE_WITH_OGLES1_) || defined(_IRR_COMPILE_WITH_OGLES2_)
#	define XR_USE_PLATFORM_EGL
#	define XR_USE_GRAPHICS_API_OPENGL_ES
#elif defined(__ANDROID__)
#	error "Irrlicht XR driver does not support Android"
#elif defined(__APPLE__)
#	error "Irrlicht XR driver does not support MacOSX / iOS"
#else
#	define XR_USE_PLATFORM_XLIB
#	define XR_USE_GRAPHICS_API_OPENGL
#endif

// Headers required for openxr_platform.h

#ifdef XR_USE_GRAPHICS_API_OPENGL
#	include <GL/gl.h>
#	include <GL/glext.h>
#endif

#ifdef XR_USE_PLATFORM_EGL
#	error "TODO: EGL headers"
#endif

#ifdef XR_USE_PLATFORM_WIN32
#	error "TODO: Win32 headers"
#endif

#ifdef XR_USE_PLATFORM_XLIB
#	include <X11/Xlib.h>
#	include <GL/glx.h>
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace irr
{

class COpenXRConnector : public IOpenXRConnector {
	public:
		COpenXRConnector(video::IVideoDriver* driver);
		virtual ~COpenXRConnector();
		virtual bool Init() override;
	protected:
		// Initialization steps in order
		bool loadExtensions();
		bool createInstance();
		bool getSystem();
		bool getViewConfigs();
		bool setupStereoView();
		bool verifyGraphics();
		bool createSession();

		bool check(XrResult result, const char* func);

		video::IVideoDriver* VideoDriver;

		// Supported extensions
		std::vector<XrExtensionProperties> Extensions;
		std::unordered_set<std::string> ExtensionNames;

		// Instance
		XrInstance Instance = XR_NULL_HANDLE;
		XrInstanceProperties InstanceProperties;

		// System
		XrSystemId SystemId = XR_NULL_SYSTEM_ID;
		XrSystemProperties SystemProps;

		// Supported View Configurations (mono, stereo, etc)
		std::vector<XrViewConfigurationType> ViewConfigs;
		std::vector<XrViewConfigurationProperties> ViewConfigProperties;

		// Parameters for the view config we're using
		// For stereo, this has left and right eyes
		std::vector<XrViewConfigurationView> ActiveViews;

		XrSession Session = XR_NULL_HANDLE;

		XrViewConfigurationType ViewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		XrReferenceSpaceType PlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
};

COpenXRConnector::COpenXRConnector(video::IVideoDriver* driver)
	: VideoDriver(driver)
{
	VideoDriver->grab();
}

COpenXRConnector::~COpenXRConnector()
{
	if (Instance)
		xrDestroyInstance(Instance);
	VideoDriver->drop();
}

bool COpenXRConnector::Init() {
	if (!loadExtensions()) return false;
	if (!createInstance()) return false;
	if (!getSystem()) return false;
	if (!getViewConfigs()) return false;
	if (!setupStereoView()) return false;
	if (!verifyGraphics()) return false;

	//if (!createSession())
	//	return false;
	return true;
}

bool COpenXRConnector::loadExtensions()
{
	XrResult result;
	uint32_t ext_count = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &ext_count, NULL);
	if (!check(result, "xrEnumerateInstanceExtensionProperties"))
		return false;

	Extensions.resize(ext_count, { XR_TYPE_EXTENSION_PROPERTIES, nullptr });
	result = xrEnumerateInstanceExtensionProperties(NULL, ext_count, &ext_count, Extensions.data());
	if (!check(result, "xrEnumerateInstanceExtensionProperties"))
		return false;

	for (const auto &extension : Extensions) {
		ExtensionNames.emplace(extension.extensionName);
	}
	return true;
}

bool COpenXRConnector::createInstance()
{
	std::vector<const char*> extensionsToEnable;

#ifdef XR_USE_GRAPHICS_API_OPENGL
	if (!ExtensionNames.count(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
		os::Printer::log("OpenXR runtime does not support OpenGL", ELL_ERROR);
		return false;
	}
	extensionsToEnable.push_back(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
	if (!ExtensionNames.count(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME)) {
		os::Printer::log("OpenXR runtime does not support OpenGL ES", ELL_ERROR);
		return false;
	}
	extensionsToEnable.push_back(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME);
#endif

	XrInstanceCreateInfo info = {
		XR_TYPE_INSTANCE_CREATE_INFO,
		nullptr,
		0,
		{
			"Minetest", 1, "", 0, XR_CURRENT_API_VERSION,
		},
		0,
		NULL,
		(uint32_t)extensionsToEnable.size(),
		extensionsToEnable.data()
	};
	XrResult result;
	result = xrCreateInstance(&info, &Instance);
	if (!check(result, "xrCreateInstance"))
		return false;

	InstanceProperties = XrInstanceProperties{
		.type = XR_TYPE_INSTANCE_PROPERTIES,
	};

	result = xrGetInstanceProperties(Instance, &InstanceProperties);
	if (!check(result, "xrGetInstanceProperties"))
		return false;

	// Print out some info
	char buf[128 + XR_MAX_RUNTIME_NAME_SIZE];
	snprintf_irr(buf, sizeof(buf), "[XR] OpenXR Runtime: %s", InstanceProperties.runtimeName);
	os::Printer::log(buf, ELL_INFORMATION);
	snprintf_irr(buf, sizeof(buf), "[XR] OpenXR Version: %d.%d.%d",
		XR_VERSION_MAJOR(InstanceProperties.runtimeVersion),
		XR_VERSION_MINOR(InstanceProperties.runtimeVersion),
		XR_VERSION_PATCH(InstanceProperties.runtimeVersion));
	os::Printer::log(buf, ELL_INFORMATION);
	return true;
}

bool COpenXRConnector::getSystem()
{
	XrFormFactor form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemGetInfo get_info = {
		.type = XR_TYPE_SYSTEM_GET_INFO,
		.formFactor = form_factor,
	};
	XrResult result = xrGetSystem(Instance, &get_info, &SystemId);
	if (!check(result, "xrGetSystem"))
		return false;

	SystemProps = XrSystemProperties{
		.type = XR_TYPE_SYSTEM_PROPERTIES,
	};
	result = xrGetSystemProperties(Instance, SystemId, &SystemProps);
	if (!check(result, "xrGetSystemProperties"))
		return false;

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

bool COpenXRConnector::getViewConfigs()
{
	XrResult result;
	uint32_t count = 0;
	result = xrEnumerateViewConfigurations(Instance, SystemId, 0, &count, NULL);
	if (!check(result, "xrEnumerateViewConfigurations"))
		return false;

	ViewConfigs.clear();
	ViewConfigs.resize(count);
	result = xrEnumerateViewConfigurations(Instance, SystemId, count, &count, ViewConfigs.data());
	if (!check(result, "xrEnumerateViewConfigurations"))
		return false;
	ViewConfigs.resize(count);

	// Fetch viewconfig properties
	ViewConfigProperties.clear();
	ViewConfigProperties.resize(count, {
		.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES,
	});
	for (uint32_t i = 0; i < count; i++) {
		result = xrGetViewConfigurationProperties(Instance, SystemId, ViewConfigs[i], &ViewConfigProperties[i]);
		if (!check(result, "xrGetViewConfigurationProperties"))
			return false;
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

bool COpenXRConnector::setupStereoView()
{
	XrViewConfigurationType chosen = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	uint32_t count = 0;
	XrResult result;
	result = xrEnumerateViewConfigurationViews(Instance, SystemId, chosen, 0, &count, NULL);
	if (!check(result, "xrEnumerateViewConfigurationViews"))
		return false;

	ActiveViews.clear();
	ActiveViews.resize(count, { .type = XR_TYPE_VIEW_CONFIGURATION_VIEW});

	result = xrEnumerateViewConfigurationViews(Instance, SystemId, chosen, count, &count, ActiveViews.data());
	if (!check(result, "xrEnumerateViewConfigurationViews"))
		return false;
	ActiveViews.resize(count);

	// Print out info
	os::Printer::log("[XR] Using stereo view", ELL_INFORMATION);
	for (uint32_t i = 0; i < ActiveViews.size(); i++) {
		const auto &av = ActiveViews[i];
		char buf[256];
		snprintf_irr(buf, sizeof(buf),
			"[XR] Index %d: Recommended/Max Resolution %dx%d/%dx%d, Swapchain samples %d/%d",
			i,
			av.recommendedImageRectWidth,
			av.recommendedImageRectHeight,
			av.maxImageRectWidth,
			av.maxImageRectHeight,
			av.recommendedSwapchainSampleCount,
			av.maxSwapchainSampleCount);
		os::Printer::log(buf, ELL_INFORMATION);
	}
	return true;

}

bool COpenXRConnector::verifyGraphics()
{
	XrResult result;

	// OpenXR requires checking graphics compatibility before creating a session.
	// xrGetInstanceProcAddr must be used, since these methods might load in dynamically.
	XrVersion minApiVersionSupported = 0;
	XrVersion maxApiVersionSupported = 0;
	int compatibility = 0;

#ifdef XR_USE_GRAPHICS_API_OPENGL
	{
		PFN_xrGetOpenGLGraphicsRequirementsKHR pfn_xrGetOpenGLGraphicsRequirementsKHR = nullptr;
		result = xrGetInstanceProcAddr(Instance, "xrGetOpenGLGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)&pfn_xrGetOpenGLGraphicsRequirementsKHR);
		if (!check(result, "xrGetInstanceProcAddr"))
			return false;

		XrGraphicsRequirementsOpenGLKHR reqs;
		result = pfn_xrGetOpenGLGraphicsRequirementsKHR(Instance, SystemId, &reqs);
		if (!check(result, "xrGetOpenGLGraphicsRequirementsKHR"))
			return false;
		minApiVersionSupported = reqs.minApiVersionSupported;
		maxApiVersionSupported = reqs.maxApiVersionSupported;
		compatibility = 0;
	}
#endif

#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
	{
		PFN_xrGetOpenGLESGraphicsRequirementsKHR pfn_xrGetOpenGLESGraphicsRequirementsKHR = nullptr;
		result = xrGetInstanceProcAddr(Instance, "xrGetOpenGLESGraphicsRequirementsKHR",
			(PFN_xrVoidFunction*)&pfn_xrGetOpenGLESGraphicsRequirementsKHR);
		if (!check(result, "xrGetInstanceProcAddr"))
			return false;

		XrGraphicsRequirementsOpenGLESKHR reqs;
		result = pfn_xrGetOpenGLESGraphicsRequirementsKHR(Instance, SystemId, &reqs);
		if (!check(result, "xrGetOpenGLESGraphicsRequirementsKHR"))
			return false;
		minApiVersionSupported = reqs.minApiVersionSupported;
		maxApiVersionSupported = reqs.maxApiVersionSupported;
		compatibility = SDL_GL_CONTEXT_PROFILE_ES;
	}
#endif

	char buf[128];
	snprintf_irr(buf, sizeof(buf),
		"[XR] OpenXR supports OpenGL%s version range (%d.%d.%d, %d.%d.%d)",
		compatibility == SDL_GL_CONTEXT_PROFILE_ES ? "ES" : "",
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

	snprintf_irr(buf, sizeof(buf),
		"[XR] SDL is configured for OpenGL%s %d.%d.%d",
		glmask == SDL_GL_CONTEXT_PROFILE_ES ? "ES" : "",
		glmajor,
		glminor,
		glmask);
	os::Printer::log(buf, ELL_INFORMATION);

	if (glmask != compatibility) {
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
bool COpenXRConnector::createSession()
{
/*
	bool using_egl = false;
	switch (VideoDriver->getDriverType())
	{
	case video::EDT_OPENGL:
		break;
	case video::EDT_OPENGL3:
		break;
	case video::EDT_OGLES2:
		using_egl = true;
		break;
	default:
		os::Printer::log("Unhandled video driver type in XR device", ELL_ERROR);
		return false;
	}

	XrSessionCreateInfo session_create_info = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.next = nullptr, // to be filled in
		.systemId = SystemId,
	};

	const char* raw_sdl_driver = SDL_GetCurrentVideoDriver();
	std::string sdl_driver = raw_sdl_driver ? raw_sdl_driver : "";
#ifdef XR_USE_PLATFORM_WIN32
	if (sdl_driver == "windows") {
		XrGraphicsBindingOpenGLWin32KHR binding{
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
		};
		binding.hDC = wglGetCurrentDC();
		binding.hGLRC = wglGetCurrentContext();
	}
#endif
#ifdef XR_USE_PLATFORM_XLIB
	if (sdl_driver == "x11") {
		XrGraphicsBindingOpenGLXlibKHR binding{
			.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
		};
		binding.xDisplay = XOpenDisplay(NULL);
		binding.glxContext = glXGetCurrentContext();
		binding.glxDrawable = glXGetCurrentDrawable();
	}
#endif
#ifdef XR_USE_PLATFORM_EGL

#endif
*/
	return true;
}

bool COpenXRConnector::check(XrResult result, const char* func)
{
	if (XR_SUCCEEDED(result))
		return true;

	if (!Instance && result == XR_ERROR_RUNTIME_FAILURE)
	{
		os::Printer::log(
			"Failed to connect to OpenXR runtime!\n"
			"Ensure that your XR provider (e.g. SteamVR)\n"
			"is running and has OpenXR enabled.",
			ELL_ERROR);
		return false;
	}

	char buf[XR_MAX_RESULT_STRING_SIZE];
	if (Instance && xrResultToString(Instance, result, buf) == XR_SUCCESS) {
		// buf was written
	} else {
		snprintf_irr(buf, sizeof(buf), "XR_ERROR(%d)", (int)result);
	}

	std::string text = func;
	text += " error: ";
	text += buf;
	os::Printer::log(text.c_str(), ELL_ERROR);
        return false;
}

IOpenXRConnector* createOpenXRConnector(video::IVideoDriver* driver)
{
	return new COpenXRConnector(driver);
}

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
