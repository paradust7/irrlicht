#ifdef _IRR_COMPILE_WITH_XR_DEVICE_

#include <cassert>
#include <unordered_set>

#include "Common.h"
#include "mt_opengl.h"
#include "os.h"
#include "IOpenXRConnector.h"
#include "IOpenXRInstance.h"
#include "OpenXRHeaders.h"

using std::unique_ptr;

namespace irr
{

class COpenXRConnector : public IOpenXRConnector {
	public:
		COpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags);
		bool Init();
		virtual ~COpenXRConnector();
		virtual void HandleEvents() override;
		virtual bool TryBeginFrame(int64_t *predicted_time_delta) override;
		virtual bool NextView(ViewRenderInfo *info) override;
	protected:
		video::IVideoDriver* VideoDriver;
		uint32_t ModeFlags;
		XrReferenceSpaceType PlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		unique_ptr<IOpenXRInstance> Instance;
		// Retry every 10 seconds
		u32 InstanceRetryInterval = 10 * 1000;
		u32 InstanceRetryTime = 0;
};

COpenXRConnector::COpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags)
	: VideoDriver(driver), ModeFlags(mode_flags)
{
	if (mode_flags & XRMF_ROOM_SCALE)
		PlaySpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	VideoDriver->grab();
}

bool COpenXRConnector::Init() {
	Instance = createOpenXRInstance(VideoDriver, PlaySpaceType);
	if (!Instance)
		return false;
	return true;
}

COpenXRConnector::~COpenXRConnector()
{
	// Order matters here
	Instance = nullptr;
	VideoDriver->drop();
}

void COpenXRConnector::HandleEvents()
{
	if (Instance) {
		if (!Instance->HandleEvents()) {
			// Instance is dead
			Instance = nullptr;
			InstanceRetryTime = os::Timer::getTime() + InstanceRetryInterval;
		}
	} else {
		u32 now = os::Timer::getTime();
		if (now > InstanceRetryTime) {
			Instance = createOpenXRInstance(VideoDriver, PlaySpaceType);
			InstanceRetryTime = now + InstanceRetryInterval;
		}
	}
}

bool COpenXRConnector::TryBeginFrame(int64_t *predicted_time_delta)
{
	if (!Instance)
		return false;
	return Instance->TryBeginFrame(predicted_time_delta);
}

bool COpenXRConnector::NextView(ViewRenderInfo *info)
{
	if (!Instance)
		return false;
	return Instance->NextView(info);
}

unique_ptr<IOpenXRConnector> createOpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags)
{
	unique_ptr<COpenXRConnector> conn(new COpenXRConnector(driver, mode_flags));
	if (!conn->Init())
		return nullptr;
	return conn;
}

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
