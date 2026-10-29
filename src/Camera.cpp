#include <iiCameraIntegration/Camera.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>
#include <limits>
#include <set>

namespace iiCameraIntegration {
namespace {
Error invalid(std::string message) { return {ErrorCode::InvalidArgument, std::move(message)}; }
bool positive(double x) { return std::isfinite(x) && x > 0; }
bool textValid(const std::string& text) { return !text.empty() && text.find('\0') == std::string::npos; }
bool rangeValid(const NumericRange& r) {
    if (!std::isfinite(r.minimum) || !std::isfinite(r.maximum) || r.minimum > r.maximum
        || !std::isfinite(r.step) || r.step < 0) return false;
    return std::all_of(r.discreteValues.begin(), r.discreteValues.end(), [&](double v) {
        return std::isfinite(v) && v >= r.minimum && v <= r.maximum;
    });
}
Result<void> validateValue(const ControlCapability& c, const ControlValue& v) {
    if (c.kind == ControlKind::Numeric) {
        const auto* number = std::get_if<double>(&v);
        if (!number || !std::isfinite(*number)) return std::unexpected(invalid("Expected a finite numeric control value"));
        if (c.range && !c.range->contains(*number)) return std::unexpected(invalid("Control value is outside the supported range/steps"));
    } else if (c.kind == ControlKind::Mode) {
        const auto* mode = std::get_if<ControlMode>(&v);
        if (!mode || std::find(c.modes.begin(), c.modes.end(), *mode) == c.modes.end())
            return std::unexpected(invalid("Control mode is not supported"));
    } else {
        const auto* id = std::get_if<std::string>(&v);
        if (!id || !textValid(*id) || std::find(c.identifiers.begin(), c.identifiers.end(), *id) == c.identifiers.end())
            return std::unexpected(invalid("Control identifier is not supported"));
    }
    return {};
}
}
bool NumericRange::contains(double v) const noexcept {
    if (!rangeValid(*this) || !std::isfinite(v) || v < minimum || v > maximum) return false;
    const auto close = [](double a, double b) { return std::abs(a-b) <= 8*std::numeric_limits<double>::epsilon()*std::max({1.0,std::abs(a),std::abs(b)}); };
    if (!discreteValues.empty()) return std::any_of(discreteValues.begin(), discreteValues.end(), [&](double item){return close(v,item);});
    if (step == 0) return true;
    const double count = (v-minimum)/step;
    return std::isfinite(count) && std::abs(count-std::round(count)) <= 1e-7;
}
ControlPort::ControlPort(std::vector<ControlCapability> capabilities, ControlCallbacks callbacks)
    : capabilities_(std::move(capabilities)), callbacks_(std::move(callbacks)) {}
const std::vector<ControlCapability>& ControlPort::capabilities() const noexcept { return capabilities_; }
Result<ControlValue> ControlPort::query(ControlId id) const {
    const auto c = std::find_if(capabilities_.begin(), capabilities_.end(), [=](const auto& v){return v.id==id;});
    if (c == capabilities_.end() || !c->readable) return std::unexpected(Error{ErrorCode::Unsupported,"Control is not readable"});
    if (!callbacks_.query) return std::unexpected(Error{ErrorCode::Unavailable,"No query provider"});
    try { return callbacks_.query(id); }
    catch (const std::exception& e) { return std::unexpected(Error{ErrorCode::BackendFailure,e.what()}); }
    catch (...) { return std::unexpected(Error{ErrorCode::BackendFailure,"Query provider threw an unknown exception"}); }
}
Result<ControlValue> ControlPort::apply(const ControlCommand& command) const {
    const auto c = std::find_if(capabilities_.begin(), capabilities_.end(), [&](const auto& v){return v.id==command.id;});
    if (c == capabilities_.end() || !c->writable) return std::unexpected(Error{ErrorCode::Unsupported,"Control is not writable"});
    if (auto valid = validateValue(*c,command.value); !valid) return std::unexpected(valid.error());
    if (!callbacks_.apply) return std::unexpected(Error{ErrorCode::Unavailable,"No command provider"});
    try { return callbacks_.apply(command); }
    catch (const std::exception& e) { return std::unexpected(Error{ErrorCode::BackendFailure,e.what()}); }
    catch (...) { return std::unexpected(Error{ErrorCode::BackendFailure,"Command provider threw an unknown exception"}); }
}
std::vector<ControlReading> ControlPort::queryAll() const {
    std::vector<ControlReading> result;
    result.reserve(capabilities_.size());
    for (const auto& capability : capabilities_) {
        auto value=query(capability.id);
        if (value) {
            if (auto valid=validateValue(capability,*value); !valid)
                value=std::unexpected(Error{ErrorCode::BackendFailure,
                    "Invalid queried control value: "+valid.error().message});
        }
        result.push_back({capability,std::move(value)});
    }
    return result;
}
Result<FieldOfView> rectilinearFieldOfView(double w, double h, double f) {
    if (!positive(w) || !positive(h) || !positive(f)) return std::unexpected(invalid("Sensor dimensions and focal length must be finite and positive"));
    const auto angle = [f](double d) { return 2*std::atan((d/f)/2)*180/std::numbers::pi; };
    const FieldOfView result{angle(w),angle(h),angle(std::hypot(w,h))};
    if (!(result.horizontalDegrees>0 && result.horizontalDegrees<180 && result.verticalDegrees>0 && result.verticalDegrees<180 && result.diagonalDegrees>0 && result.diagonalDegrees<180))
        return std::unexpected(invalid("Optical dimensions exceed representable field of view"));
    return result;
}
Result<double> focalLengthForHorizontalFieldOfView(double w, double degrees) {
    if (!positive(w) || !positive(degrees) || degrees>=180) return std::unexpected(invalid("Field of view must be between zero and 180 degrees"));
    const double f = (w/2)/std::tan(degrees*std::numbers::pi/360);
    if (!positive(f)) return std::unexpected(invalid("Optical dimensions exceed representable focal length"));
    return f;
}
Result<void> validateCameraInfo(const CameraInfo& c) {
    if (!textValid(c.id) || !textValid(c.name) || !textValid(c.backend)) return std::unexpected(invalid("Camera id, name and backend are required"));
    if (c.sensor) {
        for (auto v : {c.sensor->widthMm,c.sensor->heightMm,c.sensor->cropFactor})
            if (v && !positive(*v)) return std::unexpected(invalid("Invalid sensor dimensions or crop factor"));
        if (c.sensor->resolution && (!c.sensor->resolution->width || !c.sensor->resolution->height)) return std::unexpected(invalid("Invalid sensor resolution"));
        if (c.sensor->bitDepth && !*c.sensor->bitDepth) return std::unexpected(invalid("Invalid sensor bit depth"));
    }
    std::set<std::string> lensIds, formatIds;
    for (const auto& lens : c.lenses) {
        if (!textValid(lens.id) || !lensIds.insert(lens.id).second) return std::unexpected(invalid("Lens identifiers must be unique and nonempty"));
        for (const auto* r : {&lens.focalLengthMm,&lens.apertureFNumber})
            if (*r && (!rangeValid(**r) || (**r).minimum<=0)) return std::unexpected(invalid("Invalid lens optical range"));
        if (lens.focusDistanceMeters && (!rangeValid(*lens.focusDistanceMeters) || lens.focusDistanceMeters->minimum<0)) return std::unexpected(invalid("Invalid lens focus range"));
        for (auto v : {lens.currentFocalLengthMm,lens.currentApertureFNumber})
            if (v && !positive(*v)) return std::unexpected(invalid("Invalid lens optical value"));
        if (lens.currentFocalLengthMm && lens.focalLengthMm && !lens.focalLengthMm->contains(*lens.currentFocalLengthMm)) return std::unexpected(invalid("Current focal length is outside its reported range"));
        if (lens.currentApertureFNumber && lens.apertureFNumber && !lens.apertureFNumber->contains(*lens.currentApertureFNumber)) return std::unexpected(invalid("Current aperture is outside its reported range"));
        if (lens.horizontalFieldOfViewDegrees && (!positive(*lens.horizontalFieldOfViewDegrees) || *lens.horizontalFieldOfViewDegrees>=360)) return std::unexpected(invalid("Invalid lens field of view"));
    }
    for (const auto& f : c.formats) {
        if (!textValid(f.id) || !formatIds.insert(f.id).second || !f.resolution.width || !f.resolution.height)
            return std::unexpected(invalid("Formats require unique identifiers and nonzero dimensions"));
        for (const auto& r : f.frameRates) if (!rangeValid(r) || r.minimum<=0) return std::unexpected(invalid("Invalid frame-rate range"));
        if (f.horizontalFieldOfViewDegrees && (!positive(*f.horizontalFieldOfViewDegrees) || *f.horizontalFieldOfViewDegrees>=360)) return std::unexpected(invalid("Invalid format field of view"));
    }
    if (c.activeFormatId && !formatIds.contains(*c.activeFormatId)) return std::unexpected(invalid("Active format is not in the format list"));
    std::set<ControlId> controlIds;
    for (const auto& control : c.controls) {
        if (!controlIds.insert(control.id).second) return std::unexpected(invalid("Duplicate control"));
        if (control.kind==ControlKind::Numeric && control.range && !rangeValid(*control.range)) return std::unexpected(invalid("Invalid control range"));
        if (control.kind==ControlKind::Mode && control.modes.empty()) return std::unexpected(invalid("Mode control requires allowed modes"));
        if (control.kind==ControlKind::Identifier && (control.identifiers.empty() || !std::all_of(control.identifiers.begin(),control.identifiers.end(),textValid))) return std::unexpected(invalid("Identifier control requires valid allowed identifiers"));
    }
    return {};
}
}
