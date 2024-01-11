// TODO: License

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "CIrrDeviceXR.h"
#include "OpenXR/IOpenXRConnector.h"

namespace irr
{

//! constructor
CIrrDeviceXR::CIrrDeviceXR(const SIrrlichtCreationParameters& param)
	: CIrrDeviceSDL(param), XRConnector(nullptr), DeviceMotionActive(false)

{
	if (!VideoDriver)
		// SDL was unable to initialize
		return;

	XRConnector = createOpenXRConnector(VideoDriver);
	if (!XRConnector->Init()) {
		delete XRConnector;
		XRConnector = nullptr;
		// Signal failure to createDeviceEx
		VideoDriver->drop();
		VideoDriver = 0;
		return;
	}
}


//! destructor
CIrrDeviceXR::~CIrrDeviceXR()
{
	if (XRConnector)
		delete XRConnector;
}

//! Activate device motion.
bool CIrrDeviceXR::activateDeviceMotion(float updateInterval)
{
	return true;
}

//! Deactivate device motion.
bool CIrrDeviceXR::deactivateDeviceMotion()
{
	return true;
}

//! Is device motion active.
bool CIrrDeviceXR::isDeviceMotionActive()
{
	return true;
}

//! Is device motion available.
bool CIrrDeviceXR::isDeviceMotionAvailable()
{
	return true;
}

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
