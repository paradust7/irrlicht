#pragma once

#include "quaternion.h"
#include "vector3d.h"
#include "IRenderTarget.h"

namespace irr
{
namespace core
{

enum XR_VIEW_KIND {
	XRVK_INVALID,
	XRVK_LEFT_EYE,
	XRVK_RIGHT_EYE,
	XRVK_GENERIC,
};


struct XrViewInfo {
	XR_VIEW_KIND Kind;
	video::IRenderTarget* Target;

	// Viewport
	u32 Width;
	u32 Height;

	// HMD translation/orientation of eye relative to playspace origin
	core::vector3df Position;
	core::quaternion Orientation;

	// FoV angles (in radians)
	// For symmetric FoV, left/down will be negative
	// Total angles are (angleRight - angleLeft) and (angleUp - angleDown)
	f32 AngleLeft;
	f32 AngleRight;
	f32 AngleUp;
	f32 AngleDown;

	f32 ZNear;
	f32 ZFar;
};

} // end namespace core
} // end namespace irr
