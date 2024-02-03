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
		bool init();
		virtual ~COpenXRConnector();
		virtual void handleEvents() override;
		virtual void recenter() override;
		virtual bool tryBeginFrame(int64_t *predicted_time_delta) override;
		virtual bool nextView(core::XrViewInfo* info) override;
	protected:
		video::IVideoDriver* VideoDriver;
		uint32_t ModeFlags;
		XrReferenceSpaceType PlaySpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		unique_ptr<IOpenXRInstance> Instance;
		// Retry every 10 seconds
		u32 InstanceRetryInterval = 10 * 1000;
		u32 InstanceRetryTime = 0;
		void invalidateInstance();
};

COpenXRConnector::COpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags)
	: VideoDriver(driver), ModeFlags(mode_flags)
{
	if (mode_flags & XRMF_ROOM_SCALE)
		PlaySpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	VideoDriver->grab();
}

bool COpenXRConnector::init() {
	Instance = createOpenXRInstance(VideoDriver, PlaySpaceType);
	if (!Instance)
		return false;
	return true;
}

COpenXRConnector::~COpenXRConnector()
{
	Instance = nullptr;
	VideoDriver->drop();
}

void COpenXRConnector::invalidateInstance()
{
	os::Printer::log("[XR] Instance lost", ELL_ERROR);
	Instance = nullptr;
	InstanceRetryTime = os::Timer::getTime() + InstanceRetryInterval;
}

void COpenXRConnector::handleEvents()
{
	if (Instance) {
		if (!Instance->handleEvents()) {
			invalidateInstance();
		}
	} else {
		u32 now = os::Timer::getTime();
		if (now > InstanceRetryTime) {
			Instance = createOpenXRInstance(VideoDriver, PlaySpaceType);
			InstanceRetryTime = now + InstanceRetryInterval;
		}
	}
}

void COpenXRConnector::recenter()
{
	if (Instance)
		Instance->recenter();
}

bool COpenXRConnector::tryBeginFrame(int64_t *predicted_time_delta)
{
	if (!Instance)
		return false;
	bool didBegin = false;
	if (!Instance->internalTryBeginFrame(&didBegin, predicted_time_delta)) {
		invalidateInstance();
		return false;
	}
	return didBegin;
}

bool COpenXRConnector::nextView(core::XrViewInfo* info)
{
	if (!Instance)
		return false;
	bool gotView = false;
	if (!Instance->internalNextView(&gotView, info)) {
		invalidateInstance();
		return false;
	}
	return gotView;
}

unique_ptr<IOpenXRConnector> createOpenXRConnector(video::IVideoDriver* driver, uint32_t mode_flags)
{
	unique_ptr<COpenXRConnector> conn(new COpenXRConnector(driver, mode_flags));
	if (!conn->init())
		return nullptr;
	return conn;
}

} // namespace irr

#endif // _IRR_COMPILE_WITH_XR_DEVICE_
