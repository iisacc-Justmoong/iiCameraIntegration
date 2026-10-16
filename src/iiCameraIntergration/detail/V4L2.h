#pragma once
#include <iiCameraIntergration/Features.h>
#include <functional>

namespace iiCameraIntergration::detail {
// A syscall seam for deterministic driver-contract tests, never public hardware.
using V4L2Ioctl = std::function<int(unsigned long,void*)>;
Result<CameraFeatures> describeV4L2(const std::string&,const V4L2Ioctl&,bool,bool);
Result<ControlValue> readV4L2(const V4L2Ioctl&,ControlId);
Result<ControlValue> applyV4L2(const V4L2Ioctl&,const ControlCommand&);
} // namespace iiCameraIntergration::detail
