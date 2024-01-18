#pragma once

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "IVideoDriver.h"
#include "IRenderTarget.h"

#include <vector>
#include <unordered_set>

namespace irr
{

enum XR_MODE_FLAGS {
	XRMF_ROOM_SCALE = 0x1,
};

enum XR_VIEW_KIND {
	XRVK_LEFT_EYE = 0,
	XRVK_RIGHT_EYE = 1,
	XRVK_OTHER = 2,
};


struct ViewRenderInfo {
	XR_VIEW_KIND kind;
	video::IRenderTarget* Target;
	core::matrix4 View;
	core::matrix4 Proj;
};


class IOpenXRConnector {
public:
	virtual ~IOpenXRConnector() {};

	// Handles all pending events. Returns when the event queue is empty.
	// This needs to be called at least once between frames (not during a frame).
	// If the event queue overflows, events are lost.
	virtual void HandleEvents() = 0;

	// TryBeginFrame
	//
	// Try to begin the next frame. This method blocks to achieve VSync with the
	// HMD display, so it should only be called when everything else has been processed.
	//
	// If it returns TRUE:
	//   The next frame has begun and `predicted_time_delta` is set to the
	//   predicted future display time of the frame. (nanoseconds from now)
	//   EndFrame() must be called after drawing is finished.
	//
	// If it returns FALSE:
	//   OpenXR rendering should be skipped for this frame. The render loop must be
	//   throttled using another method (e.g. sleep)
	//
	// If the system becomes idle (HMD is turned off), or the session is closed,
	// then TryBeginFrame() could return `false` for an extended period.
	//
	// HandleEvents() should continue to be called every frame. If the system
	// comes back online, it will re-initialize, and TryBeginFrame() will return
	// true again.
	virtual bool TryBeginFrame(int64_t *predicted_time_delta) = 0;

	// Once a frame has begun, call NextView until it returns false.
	//
	// For each view, render the appropriate image.
	//
	// Don't assume every view will appear. If the system crashes
	// during rendering, it may stop short.
	//
	// After NextView returns false, the frame is considered ended.
	virtual bool NextView(ViewRenderInfo *info) = 0;
};


std::unique_ptr<IOpenXRConnector> createOpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags);

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
