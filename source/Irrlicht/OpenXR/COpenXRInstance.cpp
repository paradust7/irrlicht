#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRInstance.h"
#include "IOpenXRSession.h"
#include "Common.h"

#include <cassert>
#include <memory>


using std::unique_ptr;

namespace irr {

class COpenXRInstance : public IOpenXRInstance {
public:
	COpenXRInstance(video::IVideoDriver* driver, XrReferenceSpaceType playSpaceType)
		: VideoDriver(driver),
		  PlaySpaceType(playSpaceType)
	{
	}
	bool Init();
	virtual ~COpenXRInstance() override;
	virtual bool HandleEvents() override;
	virtual bool TryBeginFrame(int64_t *predicted_time_delta) override;
	virtual bool NextView(ViewRenderInfo *info) override;
protected:
	bool loadExtensions();
	bool createInstance();
	bool check(XrResult result, const char* func)
	{
		return openxr_check(Instance, result, func);
	}

	video::IVideoDriver* VideoDriver;
	XrReferenceSpaceType PlaySpaceType;

	// Supported extensions
	std::vector<XrExtensionProperties> Extensions;
	std::unordered_set<std::string> ExtensionNames;

	XrInstance Instance = XR_NULL_HANDLE;
	XrInstanceProperties InstanceProperties;

	unique_ptr<IOpenXRSession> Session;
};


bool COpenXRInstance::Init()
{
	if (!loadExtensions()) return false;
	if (!createInstance()) return false;
	XR_ASSERT(Instance != XR_NULL_HANDLE);
	Session = createOpenXRSession(Instance, VideoDriver, PlaySpaceType);
	if (!Session) return false;
	return true;
}

COpenXRInstance::~COpenXRInstance()
{
	// Destroy the session first
	Session = nullptr;
	if (Instance != XR_NULL_HANDLE)
		xrDestroyInstance(Instance);
}

bool COpenXRInstance::loadExtensions()
{

	os::Printer::log("[XR] Initializing OpenXR", ELL_INFORMATION);

	XrResult result;
	uint32_t extCount = 0;
	result = xrEnumerateInstanceExtensionProperties(NULL, 0, &extCount, NULL);
	check(result, "xrEnumerateInstanceExtensionProperties");

	Extensions.resize(extCount, { XR_TYPE_EXTENSION_PROPERTIES, nullptr });
	result = xrEnumerateInstanceExtensionProperties(NULL, extCount, &extCount, Extensions.data());
	check(result, "xrEnumerateInstanceExtensionProperties");

	os::Printer::log("[XR] Supported extensions:", ELL_INFORMATION);
	for (const auto& extension : Extensions) {
		char buf[32 + XR_MAX_EXTENSION_NAME_SIZE];
		snprintf_irr(buf, sizeof(buf), "[XR]   %s", extension.extensionName);
		os::Printer::log(buf, ELL_INFORMATION);
		ExtensionNames.emplace(extension.extensionName);
	}
	return true;
}

bool COpenXRInstance::createInstance()
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
	XR_CHECK(xrCreateInstance, &info, &Instance);

	XrInstanceProperties instanceProperties = {
		.type = XR_TYPE_INSTANCE_PROPERTIES,
	};
	XR_CHECK(xrGetInstanceProperties, Instance, &instanceProperties);

	// Print out some info
	char buf[128 + XR_MAX_RUNTIME_NAME_SIZE];
	snprintf_irr(buf, sizeof(buf), "[XR] OpenXR Runtime: %s", instanceProperties.runtimeName);
	os::Printer::log(buf, ELL_INFORMATION);
	snprintf_irr(buf, sizeof(buf), "[XR] OpenXR Version: %d.%d.%d",
		XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
		XR_VERSION_MINOR(instanceProperties.runtimeVersion),
		XR_VERSION_PATCH(instanceProperties.runtimeVersion));
	os::Printer::log(buf, ELL_INFORMATION);
	return true;
}

bool COpenXRInstance::HandleEvents()
{
	for (;;) {
		XrEventDataBuffer event = {
			.type = XR_TYPE_EVENT_DATA_BUFFER,
		};
		XrResult result = xrPollEvent(Instance, &event);
		if (result == XR_EVENT_UNAVAILABLE) {
			// No more events
			break;
		} else if (result != XR_SUCCESS) {
			// Logs an error
			check(result, "xrPollEvent");
			return false;
		}
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_EVENTS_LOST:
			os::Printer::log("[XR] OpenXR event queue overflowed, lost events", ELL_ERROR);
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			os::Printer::log("[XR] Disconnected (lost instance)", ELL_ERROR);
			return false;
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			// TODO
			break;
		default: break;
		}
	}
	return true;
}

bool COpenXRInstance::TryBeginFrame(int64_t *predicted_time_delta)
{
	if (!Session)
		return false;
	return Session->TryBeginFrame(predicted_time_delta);
}

bool COpenXRInstance::NextView(ViewRenderInfo *info)
{
	if (!Session)
		return false;
	return Session->NextView(info);
}

unique_ptr<IOpenXRInstance> createOpenXRInstance(
	video::IVideoDriver* driver,
	XrReferenceSpaceType playSpaceType)
{
	unique_ptr<COpenXRInstance> obj(new COpenXRInstance(driver, playSpaceType));
	if (!obj->Init())
		return nullptr;
	return obj;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
