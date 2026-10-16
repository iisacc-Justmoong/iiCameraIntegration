#include "detail/V4L2.h"
#include <linux/videodev2.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace iiCameraIntergration::detail {
namespace {
Error failure(const char* operation) {
    const int code=errno;
    const auto category=code==EACCES || code==EPERM ? ErrorCode::PermissionDenied :
        code==ENODEV || code==ENOENT ? ErrorCode::NotFound : code==EBUSY ? ErrorCode::Busy :
        code==EINVAL || code==ENOTTY ? ErrorCode::Unsupported : ErrorCode::Io;
    return {category,std::string(operation)+": "+std::strerror(code),code};
}
template<class T,std::size_t N> std::string string(const T (&value)[N]) {
    const auto* bytes=reinterpret_cast<const char*>(value);
    return {bytes,strnlen(bytes,N)};
}
std::string fourcc(std::uint32_t value) {
    std::string result;
    for(unsigned i=0;i<4;++i) {const auto c=static_cast<char>(value>>(i*8));result+=c>=32&&c<=126?c:'?';}
    return result;
}
struct Mapping { ControlId id; std::uint32_t native; double scale; const char* unit; bool mode; };
constexpr Mapping mappings[] = {
    {ControlId::Brightness,V4L2_CID_BRIGHTNESS,1,"driver units",false},
    {ControlId::Contrast,V4L2_CID_CONTRAST,1,"driver units",false},
    {ControlId::Saturation,V4L2_CID_SATURATION,1,"driver units",false},
    {ControlId::Hue,V4L2_CID_HUE,1,"driver units",false},
    {ControlId::Sharpness,V4L2_CID_SHARPNESS,1,"driver units",false},
    {ControlId::Gamma,V4L2_CID_GAMMA,1,"driver units",false},
    {ControlId::Gain,V4L2_CID_GAIN,1,"driver units",false},
    {ControlId::BacklightCompensation,V4L2_CID_BACKLIGHT_COMPENSATION,1,"driver units",false},
    {ControlId::WhiteBalanceKelvin,V4L2_CID_WHITE_BALANCE_TEMPERATURE,1,"kelvin",false},
    {ControlId::ExposureSeconds,V4L2_CID_EXPOSURE_ABSOLUTE,0.0001,"seconds",false},
    {ControlId::PanDegrees,V4L2_CID_PAN_ABSOLUTE,1.0/3600,"degrees",false},
    {ControlId::TiltDegrees,V4L2_CID_TILT_ABSOLUTE,1.0/3600,"degrees",false},
    {ControlId::FocusMode,V4L2_CID_FOCUS_AUTO,1,"",true},
    {ControlId::WhiteBalanceMode,V4L2_CID_AUTO_WHITE_BALANCE,1,"",true},
    {ControlId::ExposureMode,V4L2_CID_EXPOSURE_AUTO,1,"",true},
    {ControlId::StabilizationMode,V4L2_CID_IMAGE_STABILIZATION,1,"",true}
};
const Mapping* mapping(ControlId id) {
    const auto it=std::find_if(std::begin(mappings),std::end(mappings),[&](const auto& item){return item.id==id;});
    return it==std::end(mappings)?nullptr:it;
}
Result<v4l2_query_ext_ctrl> query(const V4L2Ioctl& io,std::uint32_t id) {
    v4l2_query_ext_ctrl ext{};ext.id=id;
    if(io(VIDIOC_QUERY_EXT_CTRL,&ext)==0) return ext;
    const auto error=errno;
    if(error!=EINVAL && error!=ENOTTY) return std::unexpected(failure("VIDIOC_QUERY_EXT_CTRL"));
    v4l2_queryctrl old{};old.id=id & ~V4L2_CTRL_FLAG_NEXT_COMPOUND;
    if(io(VIDIOC_QUERYCTRL,&old)<0) return std::unexpected(failure("VIDIOC_QUERYCTRL"));
    ext.id=old.id;ext.type=old.type;ext.flags=old.flags;
    std::memcpy(ext.name,old.name,sizeof(ext.name));
    ext.minimum=old.minimum;ext.maximum=old.maximum;ext.step=old.step;ext.default_value=old.default_value;
    ext.elem_size=old.type==V4L2_CTRL_TYPE_INTEGER64?8:4;ext.elems=1;
    return ext;
}
std::vector<FeatureChoice> choices(const V4L2Ioctl& io,const v4l2_query_ext_ctrl& c) {
    std::vector<FeatureChoice> result;
    if(c.type!=V4L2_CTRL_TYPE_MENU && c.type!=V4L2_CTRL_TYPE_INTEGER_MENU) return result;
    if(c.minimum<0 || c.maximum<c.minimum || c.maximum>std::numeric_limits<std::uint32_t>::max() ||
       static_cast<std::uint64_t>(c.maximum)-static_cast<std::uint64_t>(c.minimum)>65536) return result;
    for(auto index=c.minimum;index<=c.maximum;++index) {
        v4l2_querymenu menu{};menu.id=c.id;menu.index=static_cast<std::uint32_t>(index);
        if(io(VIDIOC_QUERYMENU,&menu)==0)
            result.push_back({std::to_string(index),c.type==V4L2_CTRL_TYPE_MENU?string(menu.name):std::to_string(menu.value)});
    }
    return result;
}
bool readable(const v4l2_query_ext_ctrl& c) {
    return !(c.flags&(V4L2_CTRL_FLAG_DISABLED|V4L2_CTRL_FLAG_WRITE_ONLY)) &&
        c.type!=V4L2_CTRL_TYPE_BUTTON && c.type!=V4L2_CTRL_TYPE_CTRL_CLASS;
}
bool writable(const v4l2_query_ext_ctrl& c) {
    return !(c.flags&(V4L2_CTRL_FLAG_DISABLED|V4L2_CTRL_FLAG_READ_ONLY|V4L2_CTRL_FLAG_INACTIVE|V4L2_CTRL_FLAG_GRABBED)) &&
        c.type!=V4L2_CTRL_TYPE_CTRL_CLASS;
}
Result<FeatureValue> readNative(const V4L2Ioctl& io,const v4l2_query_ext_ctrl& c) {
    if(!readable(c)) return std::unexpected(Error{ErrorCode::Unsupported,"Native control is not readable"});
    v4l2_ext_control value{};value.id=c.id;
    std::vector<std::byte> buffer;
    const bool payload=c.type==V4L2_CTRL_TYPE_STRING || (c.flags&V4L2_CTRL_FLAG_HAS_PAYLOAD);
    if(payload) {
        const auto bytes=c.type==V4L2_CTRL_TYPE_STRING?static_cast<std::uint64_t>(c.maximum)+1:
            static_cast<std::uint64_t>(c.elem_size)*c.elems;
        if(bytes>16*1024*1024) return std::unexpected(Error{ErrorCode::Unsupported,"Native payload exceeds the 16 MiB query limit"});
        buffer.resize(static_cast<std::size_t>(bytes));value.size=static_cast<std::uint32_t>(bytes);value.ptr=buffer.data();
    }
    v4l2_ext_controls values{};values.which=V4L2_CTRL_WHICH_CUR_VAL;values.count=1;values.controls=&value;
    if(io(VIDIOC_G_EXT_CTRLS,&values)<0) {
        const int extError=errno;
        if(payload || c.type==V4L2_CTRL_TYPE_INTEGER64 || (extError!=ENOTTY && extError!=EINVAL))
            return std::unexpected(failure("VIDIOC_G_EXT_CTRLS"));
        v4l2_control legacy{};legacy.id=c.id;
        if(io(VIDIOC_G_CTRL,&legacy)<0) return std::unexpected(failure("VIDIOC_G_CTRL"));
        value.value=legacy.value;
    }
    if(c.type==V4L2_CTRL_TYPE_STRING)
        return FeatureValue{std::string(reinterpret_cast<const char*>(buffer.data()),strnlen(reinterpret_cast<const char*>(buffer.data()),buffer.size()))};
    if(payload) return FeatureValue{std::move(buffer)};
    if(c.type==V4L2_CTRL_TYPE_BOOLEAN) return FeatureValue{value.value!=0};
    return FeatureValue{c.type==V4L2_CTRL_TYPE_INTEGER64?static_cast<std::int64_t>(value.value64):static_cast<std::int64_t>(value.value)};
}
FeatureType type(std::uint32_t value) {
    switch(value) {
    case V4L2_CTRL_TYPE_BOOLEAN:return FeatureType::Boolean;
    case V4L2_CTRL_TYPE_MENU:case V4L2_CTRL_TYPE_INTEGER_MENU:return FeatureType::Menu;
    case V4L2_CTRL_TYPE_STRING:return FeatureType::String;
    case V4L2_CTRL_TYPE_BUTTON:return FeatureType::Action;
    case V4L2_CTRL_TYPE_CTRL_CLASS:return FeatureType::Group;
    default:return value>=V4L2_CTRL_COMPOUND_TYPES?FeatureType::Compound:FeatureType::Integer;
    }
}
void appendControl(CameraFeatures& result,const V4L2Ioctl& io,const v4l2_query_ext_ctrl& c,bool writeAccess,bool values) {
    if(c.flags&V4L2_CTRL_FLAG_DISABLED) return;
    NativeFeature feature;
    feature.id="V4L2:"+std::to_string(c.id);feature.name=string(c.name);
    if(feature.name.empty()) feature.name=feature.id;
    feature.type=type(c.type);feature.readable=readable(c);feature.writable=writeAccess&&writable(c);
    feature.additional["V4L2.type"]=std::to_string(c.type);feature.additional["V4L2.flags"]=std::to_string(c.flags);
    feature.additional["V4L2.minimum"]=std::to_string(c.minimum);feature.additional["V4L2.maximum"]=std::to_string(c.maximum);
    feature.additional["V4L2.default"]=std::to_string(c.default_value);feature.additional["V4L2.step"]=std::to_string(c.step);
    feature.additional["V4L2.elementSize"]=std::to_string(c.elem_size);feature.additional["V4L2.elements"]=std::to_string(c.elems);
    for(unsigned i=0;i<c.nr_of_dims && i<V4L2_CTRL_MAX_DIMS;++i)
        feature.additional["V4L2.dimension."+std::to_string(i)]=std::to_string(c.dims[i]);
    if(c.minimum<=c.maximum) feature.range=NumericRange{static_cast<double>(c.minimum),static_cast<double>(c.maximum),static_cast<double>(c.step),{}};
    feature.choices=choices(io,c);
    if(values) feature.value=readNative(io,c);
    result.nativeFeatures.push_back(std::move(feature));
    const auto it=std::find_if(std::begin(mappings),std::end(mappings),[&](const auto& item){return item.native==c.id;});
    if(it==std::end(mappings)) return;
    ControlCapability standard;
    standard.id=it->id;standard.unit=it->unit;standard.readable=readable(c);standard.writable=writeAccess&&writable(c);
    standard.additional["V4L2.id"]=std::to_string(c.id);
    if(it->mode) {
        standard.kind=ControlKind::Mode;
        if(c.id==V4L2_CID_EXPOSURE_AUTO) {
            const auto contains=[&](unsigned value){return std::any_of(result.nativeFeatures.back().choices.begin(),result.nativeFeatures.back().choices.end(),[&](const auto& choice){return choice.id==std::to_string(value);});};
            if(contains(V4L2_EXPOSURE_MANUAL)) standard.modes.push_back(ControlMode::Manual);
            if(contains(V4L2_EXPOSURE_AUTO)) standard.modes.push_back(ControlMode::Automatic);
        } else standard.modes=c.id==V4L2_CID_IMAGE_STABILIZATION?std::vector{ControlMode::Off,ControlMode::On}:
            std::vector{ControlMode::Manual,ControlMode::Continuous};
        if(standard.modes.empty()) return;
    } else {
        standard.range=NumericRange{c.minimum*it->scale,c.maximum*it->scale,c.step*it->scale,{}};
    }
    result.camera.controls.push_back(std::move(standard));
}
void controls(CameraFeatures& result,const V4L2Ioctl& io,bool writeAccess,bool values) {
    std::set<std::uint32_t> seen;
    std::uint32_t previous=0;
    for(unsigned index=0;index<65536;++index) {
        auto c=query(io,previous|V4L2_CTRL_FLAG_NEXT_CTRL|V4L2_CTRL_FLAG_NEXT_COMPOUND);
        if(!c) {
            if(index==0 && c.error().code==ErrorCode::Unsupported) {
                // Legacy drivers without NEXT_CTRL: user and camera classes,
                // plus their contiguous private controls.
                for(auto id=V4L2_CID_BASE;id<V4L2_CID_LASTP1;++id)
                    if(auto old=query(io,id);old && seen.insert(old->id).second) appendControl(result,io,*old,writeAccess,values);
                for(auto id=V4L2_CID_CAMERA_CLASS_BASE;id<V4L2_CID_CAMERA_CLASS_BASE+64;++id)
                    if(auto old=query(io,id);old && seen.insert(old->id).second) appendControl(result,io,*old,writeAccess,values);
                for(auto id=V4L2_CID_PRIVATE_BASE;id<V4L2_CID_PRIVATE_BASE+65536;++id) {
                    auto old=query(io,id);if(!old) break;
                    if(seen.insert(old->id).second) appendControl(result,io,*old,writeAccess,values);
                }
            } else if(c.error().code!=ErrorCode::Unsupported)
                result.camera.additional["V4L2.controlEnumerationError"]=c.error().message;
            return;
        }
        if(c->id<=previous || !seen.insert(c->id).second) {
            result.camera.additional["V4L2.controlEnumerationError"]="Driver returned a non-increasing control ID";return;
        }
        previous=c->id;appendControl(result,io,*c,writeAccess,values);
    }
    result.camera.additional["V4L2.controlEnumerationError"]="Control enumeration exceeded the safety limit";
}
void formats(CameraInfo& camera,const V4L2Ioctl& io,v4l2_buf_type typeValue) {
    for(unsigned fi=0;fi<4096;++fi) {
        v4l2_fmtdesc description{};description.index=fi;description.type=typeValue;
        if(io(VIDIOC_ENUM_FMT,&description)<0) {
            if(errno!=EINVAL && errno!=ENOTTY) camera.additional["V4L2.formatEnumerationError"]=failure("VIDIOC_ENUM_FMT").message;
            break;
        }
        bool sizes=false;
        for(unsigned si=0;si<4096;++si) {
            v4l2_frmsizeenum size{};size.index=si;size.pixel_format=description.pixelformat;
            if(io(VIDIOC_ENUM_FRAMESIZES,&size)<0) break;
            CameraFormat format;
            format.id="V4L2:"+std::to_string(typeValue)+":"+std::to_string(fi)+":"+std::to_string(si);
            format.additional["V4L2.description"]=string(description.description);
            format.additional["V4L2.fourcc"]=fourcc(description.pixelformat);
            if(description.flags&V4L2_FMT_FLAG_COMPRESSED) format.codec=fourcc(description.pixelformat);
            else format.pixelFormat=fourcc(description.pixelformat);
            if(size.type==V4L2_FRMSIZE_TYPE_DISCRETE) format.resolution={size.discrete.width,size.discrete.height};
            else {
                format.resolution={size.stepwise.min_width,size.stepwise.min_height};
                format.additional["V4L2.widthMaximum"]=std::to_string(size.stepwise.max_width);
                format.additional["V4L2.heightMaximum"]=std::to_string(size.stepwise.max_height);
                format.additional["V4L2.widthStep"]=std::to_string(size.stepwise.step_width);
                format.additional["V4L2.heightStep"]=std::to_string(size.stepwise.step_height);
                format.additional["V4L2.sizeType"]=size.type==V4L2_FRMSIZE_TYPE_CONTINUOUS?"continuous":"stepwise";
            }
            if(!format.resolution.width || !format.resolution.height) continue;
            for(unsigned ri=0;ri<4096;++ri) {
                v4l2_frmivalenum interval{};interval.index=ri;interval.pixel_format=description.pixelformat;
                interval.width=format.resolution.width;interval.height=format.resolution.height;
                if(io(VIDIOC_ENUM_FRAMEINTERVALS,&interval)<0) break;
                if(interval.type==V4L2_FRMIVAL_TYPE_DISCRETE) {
                    if(interval.discrete.numerator && interval.discrete.denominator) {
                        const double fps=static_cast<double>(interval.discrete.denominator)/interval.discrete.numerator;
                        format.frameRates.push_back({fps,fps,0,{fps}});
                    }
                } else {
                    const auto& step=interval.stepwise;
                    if(step.min.numerator && step.min.denominator && step.max.numerator && step.max.denominator) {
                        const double minimum=static_cast<double>(step.max.denominator)/step.max.numerator;
                        const double maximum=static_cast<double>(step.min.denominator)/step.min.numerator;
                        if(minimum<=maximum) format.frameRates.push_back({minimum,maximum,0,{}});
                        format.additional["V4L2.frameIntervalStep"]=std::to_string(step.step.numerator)+"/"+std::to_string(step.step.denominator);
                    }
                    break;
                }
            }
            sizes=true;camera.formats.push_back(std::move(format));
            if(size.type!=V4L2_FRMSIZE_TYPE_DISCRETE) break;
        }
        if(!sizes) camera.additional["V4L2.format."+std::to_string(typeValue)+"."+std::to_string(fi)]=fourcc(description.pixelformat)+":"+string(description.description)+":size not reported";
    }
}
}
Result<CameraFeatures> describeV4L2(const std::string& id,const V4L2Ioctl& io,bool writeAccess,bool values) {
    v4l2_capability capability{};
    if(io(VIDIOC_QUERYCAP,&capability)<0) return std::unexpected(failure("VIDIOC_QUERYCAP"));
    const auto bits=capability.capabilities&V4L2_CAP_DEVICE_CAPS?capability.device_caps:capability.capabilities;
    if(!(bits&(V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_VIDEO_CAPTURE_MPLANE)))
        return std::unexpected(Error{ErrorCode::Unsupported,"The V4L2 node does not provide video capture"});
    CameraFeatures result;auto& camera=result.camera;
    camera.id=id;camera.name=string(capability.card);if(camera.name.empty()) camera.name=id;
    camera.backend="V4L2";camera.additional["V4L2.driver"]=string(capability.driver);
    camera.additional["V4L2.bus"]=string(capability.bus_info);camera.additional["V4L2.driverVersion"]=std::to_string(capability.version);
    camera.additional["V4L2.capabilities"]=std::to_string(bits);
    camera.additional["V4L2.writableFlagsScope"]="driver support; control-port creation checks descriptor permissions";
    if(camera.additional["V4L2.bus"].starts_with("usb")) camera.transport=Transport::Usb;
    if(camera.additional["V4L2.driver"]=="v4l2loopback") camera.transport=Transport::Virtual;
    if(bits&V4L2_CAP_VIDEO_CAPTURE) formats(camera,io,V4L2_BUF_TYPE_VIDEO_CAPTURE);
    if(bits&V4L2_CAP_VIDEO_CAPTURE_MPLANE) formats(camera,io,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    v4l2_format active{};active.type=bits&V4L2_CAP_VIDEO_CAPTURE?V4L2_BUF_TYPE_VIDEO_CAPTURE:V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if(io(VIDIOC_G_FMT,&active)==0) {
        const bool planar=active.type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        const auto width=planar?active.fmt.pix_mp.width:active.fmt.pix.width;
        const auto height=planar?active.fmt.pix_mp.height:active.fmt.pix.height;
        const auto pixel=planar?active.fmt.pix_mp.pixelformat:active.fmt.pix.pixelformat;
        camera.additional["V4L2.activeFourCC"]=fourcc(pixel);
        camera.additional["V4L2.activeResolution"]=std::to_string(width)+"x"+std::to_string(height);
        const auto current=std::find_if(camera.formats.begin(),camera.formats.end(),[&](const auto& item) {
            return item.resolution.width==width && item.resolution.height==height && item.additional.at("V4L2.fourcc")==fourcc(pixel);
        });
        if(current!=camera.formats.end()) camera.activeFormatId=current->id;
    }
    controls(result,io,writeAccess,values);
    if(values) for(const auto& control:camera.controls) result.controls.push_back({control,control.readable?readV4L2(io,control.id):
        Result<ControlValue>{std::unexpected(Error{ErrorCode::Unsupported,"Control is not readable"})}});
    return result;
}
Result<ControlValue> readV4L2(const V4L2Ioctl& io,ControlId id) {
    const auto* mapped=mapping(id);
    if(!mapped) return std::unexpected(Error{ErrorCode::Unsupported,"No standard mapping for this V4L2 control"});
    auto c=query(io,mapped->native);if(!c) return std::unexpected(c.error());
    auto value=readNative(io,*c);if(!value) return std::unexpected(value.error());
    const auto raw=std::get_if<std::int64_t>(&*value);
    const auto boolean=std::get_if<bool>(&*value);
    if(!raw && !boolean) return std::unexpected(Error{ErrorCode::Unsupported,"Driver changed the control type"});
    const auto number=raw?*raw:static_cast<std::int64_t>(*boolean);
    if(!mapped->mode) return ControlValue{number*mapped->scale};
    if(mapped->native==V4L2_CID_EXPOSURE_AUTO) {
        if(number==V4L2_EXPOSURE_MANUAL) return ControlValue{ControlMode::Manual};
        if(number==V4L2_EXPOSURE_AUTO) return ControlValue{ControlMode::Automatic};
        return std::unexpected(Error{ErrorCode::Unsupported,"Shutter/aperture priority is available in the native feature report"});
    }
    if(mapped->native==V4L2_CID_IMAGE_STABILIZATION) return ControlValue{number?ControlMode::On:ControlMode::Off};
    return ControlValue{number?ControlMode::Continuous:ControlMode::Manual};
}
Result<ControlValue> applyV4L2(const V4L2Ioctl& io,const ControlCommand& command) {
    const auto* mapped=mapping(command.id);if(!mapped) return std::unexpected(Error{ErrorCode::Unsupported,"No V4L2 control mapping"});
    auto c=query(io,mapped->native);if(!c) return std::unexpected(c.error());
    if(!writable(*c)) return std::unexpected(Error{ErrorCode::Unsupported,"The current driver control is not writable"});
    std::int64_t native{};
    if(mapped->mode) {
        const auto* mode=std::get_if<ControlMode>(&command.value);if(!mode) return std::unexpected(Error{ErrorCode::InvalidArgument,"Expected a control mode"});
        if(mapped->native==V4L2_CID_EXPOSURE_AUTO) {
            if(*mode!=ControlMode::Manual && *mode!=ControlMode::Automatic) return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported exposure mode"});
            native=*mode==ControlMode::Manual?V4L2_EXPOSURE_MANUAL:V4L2_EXPOSURE_AUTO;
        } else if(mapped->native==V4L2_CID_IMAGE_STABILIZATION) {
            if(*mode!=ControlMode::On && *mode!=ControlMode::Off) return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported stabilization mode"});
            native=*mode==ControlMode::On;
        } else {
            if(*mode!=ControlMode::Manual && *mode!=ControlMode::Continuous) return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported auto control mode"});
            native=*mode==ControlMode::Continuous;
        }
    } else {
        const auto* number=std::get_if<double>(&command.value);
        if(!number || !std::isfinite(*number)) return std::unexpected(Error{ErrorCode::InvalidArgument,"Expected a finite number"});
        const double scaled=*number/mapped->scale;
        if(!std::isfinite(scaled) || scaled<c->minimum || scaled>c->maximum)
            return std::unexpected(Error{ErrorCode::InvalidArgument,"The current driver range excludes this value"});
        native=static_cast<std::int64_t>(std::llround(scaled));
    }
    if(native<c->minimum || native>c->maximum || (c->step && (native-c->minimum)%c->step))
        return std::unexpected(Error{ErrorCode::InvalidArgument,"The current driver range/step excludes this value"});
    v4l2_ext_control value{};value.id=c->id;value.value=static_cast<std::int32_t>(native);
    v4l2_ext_controls values{};values.which=V4L2_CTRL_WHICH_CUR_VAL;values.count=1;values.controls=&value;
    if(io(VIDIOC_S_EXT_CTRLS,&values)<0) {
        if(errno!=ENOTTY && errno!=EINVAL) return std::unexpected(failure("VIDIOC_S_EXT_CTRLS"));
        v4l2_control old{};old.id=c->id;old.value=value.value;
        if(io(VIDIOC_S_CTRL,&old)<0) return std::unexpected(failure("VIDIOC_S_CTRL"));
    }
    return readV4L2(io,command.id);
}
} // namespace iiCameraIntergration::detail
