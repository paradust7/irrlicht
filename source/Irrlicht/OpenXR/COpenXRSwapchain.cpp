#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "os.h"
#include "IOpenXRSwapchain.h"
#include "OpenXRHeaders.h"
#include "Common.h"
#include "CNullDriver.h"

#ifdef XR_USE_GRAPHICS_API_OPENGL
#include "COpenGLCommon.h"
#endif

#include <vector>

using std::unique_ptr;

namespace irr {

class COpenXRSwapchain : public IOpenXRSwapchain
{
public:
	COpenXRSwapchain(
		video::IVideoDriver* driver,
		XrInstance instance,
		XrSession session,
		XrSwapchainUsageFlags usageFlags,
		int64_t format,
		uint32_t sampleCount,
		uint32_t width,
		uint32_t height)
		: VideoDriver(driver)
		, Instance(instance)
		, Session(session)
		, UsageFlags(usageFlags)
		, Format(format)
		, SampleCount(sampleCount)
		, Width(width)
		, Height(height)
	{
		VideoDriver->grab();
	}

	virtual ~COpenXRSwapchain()
	{
		if (Swapchain)
			xrDestroySwapchain(Swapchain);
		VideoDriver->drop();
	}

	virtual XrSwapchain getHandle() override
	{
		return Swapchain;
	}

	virtual size_t getLength() override
	{
		return Images.size();
	}


	virtual bool acquireAndWait() override;
        virtual size_t getAcquiredIndex() override;
        virtual video::ITexture* getAcquiredTexture() override;
	virtual bool release() override;

	bool init();

protected:
	video::IVideoDriver* VideoDriver;
	XrInstance Instance;
	XrSession Session;
	XrSwapchainUsageFlags UsageFlags;
	int64_t Format;
	uint32_t SampleCount;
	uint32_t Width;
	uint32_t Height;
	XrSwapchain Swapchain = XR_NULL_HANDLE;
	bool Acquired = false;
	uint32_t AcquiredIndex = 0;

	// These are parallel arrays
	std::vector<GLuint> Images;
	std::vector<video::ITexture*> Textures;

	bool check(XrResult result, const char* func)
	{
		return openxr_check(Instance, result, func);
	}
};


bool COpenXRSwapchain::init()
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

	// Print some debug info
	{
		char buf[64];
		snprintf_irr(buf, sizeof(buf), "[XR] Created swapchain of length %u", swapchainLength);
		os::Printer::log(buf, ELL_INFORMATION);
	}

	video::E_DRIVER_TYPE driverType = video::EDT_NULL;
#ifdef XR_USE_GRAPHICS_API_OPENGL
	std::vector<XrSwapchainImageOpenGLKHR> images(swapchainLength,
		XrSwapchainImageOpenGLKHR{ .type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
	driverType = video::EDT_OPENGL;
#endif
#ifdef XR_USE_GRAPHICS_API_OPENGL_ES
	std::vector<XrSwapchainImageOpenGLESKHR> images(swapchainLength,
		XrSwapchainImageOpenGLESKHR{ .type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
	driverType = video::EDT_OPENGLES2;
#endif
	XR_CHECK(xrEnumerateSwapchainImages,
		Swapchain,
		swapchainLength,
		&swapchainLength,
		(XrSwapchainImageBaseHeader*)images.data());

	Images.resize(swapchainLength);
	for (uint32_t i = 0; i < swapchainLength; ++i) {
		Images[i] = images[i].image;
	}

	Textures.resize(swapchainLength);
	for (uint32_t i = 0; i < swapchainLength; ++i) {
		Textures[i] = VideoDriver->useDeviceDependentTexture(
			"openxr_swapchain",
			driverType,
			&Images[i],
			(UsageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? video::ECF_D32F : video::ECF_A8R8G8B8,
			Width, Height);
		XR_ASSERT(Textures[i] != nullptr);
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
		.timeout = 100000000, // 100 million nanoseconds = 100 ms
	};
	// If timeout occurs, this will fail and destroy the session.
	// Swapchains should almost never have contention
	// So such a situation is likely fatal anyway.
	XR_CHECK(xrWaitSwapchainImage, Swapchain, &waitInfo);
	return true;
}

size_t COpenXRSwapchain::getAcquiredIndex()
{
	XR_ASSERT(Acquired);
	return AcquiredIndex;
}

video::ITexture* COpenXRSwapchain::getAcquiredTexture()
{
	XR_ASSERT(Acquired);
	return Textures[AcquiredIndex];
}


bool COpenXRSwapchain::release()
{
	XR_ASSERT(Acquired);
	//XR_ASSERT(Textures[AcquiredIndex]->getReferenceCount() == 1);

	glFinish();

	XrSwapchainImageReleaseInfo releaseInfo = {
		.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
	};
	XR_CHECK(xrReleaseSwapchainImage, Swapchain, &releaseInfo);
	Acquired = false;
	return true;
}

unique_ptr<IOpenXRSwapchain> createOpenXRSwapchain(
	video::IVideoDriver* driver,
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
			driver,
			instance,
			session,
			usageFlags,
			format,
			sampleCount,
			width,
			height));
	if (!obj->init())
		return nullptr;
	return obj;
}

} // end namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
