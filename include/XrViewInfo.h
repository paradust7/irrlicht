#pragma once

#include "quaternion.h"
#include "vector3d.h"
#include "dimension2d.h"
#include "IRenderTarget.h"

namespace irr
{
namespace core
{

struct XrFrameConfig {
	core::dimension2du HudSize;
	struct {
		bool Enable;
		dimension2df Size;
		// Coordinates of the center (in the XR fixed frame)
		vector3df Position;
		quaternion Orientation;
	} FloatingHud;
};

enum XR_VIEW_KIND {
	XRVK_INVALID,
	XRVK_LEFT_EYE,
	XRVK_RIGHT_EYE,
	XRVK_HUD,
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

	// If this is an eye (left/right), this is the center point between
	// the two eyes. Used for IPD adjustment.
	core::vector3df PositionBase;

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
