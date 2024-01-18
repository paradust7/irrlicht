#pragma once

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "OpenXRHeaders.h"

#include <memory>

namespace irr {

class IOpenXRSwapchain {
public:
	virtual XrSwapchain getHandle() = 0;

	// Acquire a swapchain index and wait for it to become ready.
	// Must be called after frame has begun.
	//
	// Returns true on success.
	//
	// Returns false on fatal error
	// (session and instance should be destroyed)
	virtual bool acquireAndWait() = 0;

	// Release the swapchain.
	// glFinish() must be called before this,
	// or else there will be chaos!
	virtual bool release() = 0;
};

std::unique_ptr<IOpenXRSwapchain> createOpenXRSwapchain(
	XrInstance instance,
	XrSession session,
	XrSwapchainUsageFlags usageFlags,
	int64_t format,
	uint32_t sampleCount,
	uint32_t width,
	uint32_t height);

} // end namespace irr
#endif
