#pragma once

#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include "IVideoDriver.h"

#include <vector>
#include <unordered_set>

namespace irr
{

	class IOpenXRConnector {
	public:
		virtual bool Init() = 0;
		virtual ~IOpenXRConnector() { };
	};


	IOpenXRConnector* createOpenXRConnector(video::IVideoDriver* driver);

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
