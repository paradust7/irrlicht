#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRSwapchain.h"
#include "OpenXRHeaders.h"
#include "Common.h"

#include <vector>

using std::unique_ptr;

namespace irr {

class COpenXRSwapchain : public IOpenXRSwapchain
{
public:
	COpenXRSwapchain(
		XrInstance instance,
		XrSession session,
		XrSwapchainUsageFlags usageFlags,
		int64_t format,
		uint32_t sampleCount,
		uint32_t width,
		uint32_t height)
		: Instance(instance)
		, Session(session)
		, UsageFlags(usageFlags)
		, Format(format)
		, SampleCount(sampleCount)
		, Width(width)
		, Height(height) {}

	virtual ~COpenXRSwapchain()
	{
		if (Swapchain)
			xrDestroySwapchain(Swapchain);
	}

	virtual XrSwapchain getHandle() override
	{
		return Swapchain;
	}

	virtual bool acquireAndWait() override;
	virtual bool release() override;

	bool Init();

protected:
	XrInstance Instance;
	XrSession Session;
	XrSwapchainUsageFlags UsageFlags;
	int64_t Format;
	uint32_t SampleCount;
	uint32_t Width;
	uint32_t Height;
	XrSwapchain Swapchain;
	bool Acquired;
	uint32_t AcquiredIndex;


	// These are parallel arrays
	std::vector<GLuint> Textures;

	bool check(XrResult result, const char* func)
	{
		return openxr_check(Instance, result, func);
	}
};


bool COpenXRSwapchain::Init()
{
	XrSwapchainCreateInfo swapchain_create_info{
		.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		.next = NULL,
		.createFlags = 0,
		.usageFlags = UsageFlags,
		.format = Format,
		.sampleCount = SampleCount,
		.width = Width,
		.height = Height,
		.faceCount = 1,
		.arraySize = 1,
		.mipCount = 1,
	};
	XR_CHECK(xrCreateSwapchain, Session, &swapchain_create_info, &Swapchain);

	uint32_t swapchainLength = 0;
	XR_CHECK(xrEnumerateSwapchainImages, Swapchain, 0, &swapchainLength, NULL);

#ifdef XR_USE_GRAPHICS_API_OPENGL
	std::vector<XrSwapchainImageOpenGLKHR> images(swapchainLength,
		XrSwapchainImageOpenGLKHR{ .type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
	std::vector<XrSwapchainImageOpenGLESKHR> images(swapchainLength,
		XrSwapchainImageOpenGLESKHR{ .type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
#endif
	XR_CHECK(xrEnumerateSwapchainImages,
		Swapchain,
		swapchainLength,
		&swapchainLength,
		(XrSwapchainImageBaseHeader*)images.data());

	Textures.resize(swapchainLength);
	for (uint32_t i = 0; i < swapchainLength; ++i) {
		Textures[i] = images[i].image;
	}
	return true;
}

bool COpenXRSwapchain::acquireAndWait()
{
	XR_ASSERT(!Acquired);
	XrSwapchainImageAcquireInfo acquireInfo = {
		.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
	};
	XR_CHECK(xrAcquireSwapchainImage, Swapchain, &acquireInfo, &AcquiredIndex);
	Acquired = true;

	XrSwapchainImageWaitInfo waitInfo = {
		.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
		.timeout = 100000000, // 100 ms
	};
	// This will fail if timeout occurs.
	// Swapchains should almost never have any contention.
	// So such a situation is likely fatal anyway.
	XR_CHECK(xrWaitSwapchainImage, Swapchain, &waitInfo);

	return true;
}

bool COpenXRSwapchain::release()
{
	XrSwapchainImageReleaseInfo releaseInfo = {
		.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
	};
	XR_CHECK(xrReleaseSwapchainImage, Swapchain, &releaseInfo);
	return true;
}

unique_ptr<IOpenXRSwapchain> createOpenXRSwapchain(
	XrInstance instance,
	XrSession session,
	XrSwapchainUsageFlags usageFlags,
	int64_t format,
	uint32_t sampleCount,
	uint32_t width,
	uint32_t height)
{
	unique_ptr<COpenXRSwapchain> obj(
		new COpenXRSwapchain(
			instance,
			session,
			usageFlags,
			format,
			sampleCount,
			width,
			height));
	if (!obj->Init())
		return nullptr;
	return obj;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
