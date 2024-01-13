#pragma once

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "IVideoDriver.h"

#include <vector>
#include <unordered_set>

namespace irr
{

enum XR_MODE_FLAGS {
	XRMF_ROOM_SCALE = 0x1,
};


class IOpenXRConnector {
public:
	virtual bool Init() = 0;
	virtual ~IOpenXRConnector() { };
};


IOpenXRConnector* createOpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags);

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
