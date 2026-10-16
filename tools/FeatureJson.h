#pragma once
#include <iiCameraIntergration/Features.h>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <type_traits>

namespace iiCameraIntergration::cli {
inline void string(std::ostream& out,std::string_view text) {
    constexpr char hex[]="0123456789abcdef";out<<'"';
    for(unsigned char c:text) {
        if(c=='"'||c=='\\') out<<'\\'<<static_cast<char>(c);
        else if(c<32) out<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else out<<static_cast<char>(c);
    }
    out<<'"';
}
inline void number(std::ostream& out,double value) {if(std::isfinite(value)) out<<std::setprecision(17)<<value;else out<<"null";}
inline void boolean(std::ostream& out,bool value) {out<<(value?"true":"false");}
template<class T,class F> void array(std::ostream& out,const T& values,F emit) {
    out<<'[';bool comma=false;for(const auto& value:values) {if(comma) out<<',';comma=true;emit(value);}out<<']';
}
inline void metadata(std::ostream& out,const Metadata& value) {
    out<<'{';bool comma=false;for(const auto& [key,text]:value) {if(comma) out<<',';comma=true;string(out,key);out<<':';string(out,text);}out<<'}';
}
inline void error(std::ostream& out,const Error& value) {
    out<<"{\"code\":"<<static_cast<int>(value.code)<<",\"nativeCode\":"<<value.nativeCode<<",\"message\":";string(out,value.message);out<<'}';
}
inline void range(std::ostream& out,const std::optional<NumericRange>& value) {
    if(!value) {out<<"null";return;}
    out<<"{\"minimum\":";number(out,value->minimum);out<<",\"maximum\":";number(out,value->maximum);out<<",\"step\":";number(out,value->step);
    out<<",\"discreteValues\":";array(out,value->discreteValues,[&](double item){number(out,item);});out<<'}';
}
inline void optionalNumber(std::ostream& out,const std::optional<double>& value) {if(value) number(out,*value);else out<<"null";}
inline void size(std::ostream& out,const Size& value) {out<<"{\"width\":"<<value.width<<",\"height\":"<<value.height<<'}';}
inline void capability(std::ostream& out,const ControlCapability& value) {
    out<<"{\"id\":";string(out,controlName(value.id));out<<",\"kind\":"<<static_cast<int>(value.kind)<<",\"unit\":";string(out,value.unit);
    out<<",\"readable\":";boolean(out,value.readable);out<<",\"writable\":";boolean(out,value.writable);out<<",\"range\":";range(out,value.range);
    out<<",\"modes\":";array(out,value.modes,[&](auto item){out<<static_cast<int>(item);});
    out<<",\"identifiers\":";array(out,value.identifiers,[&](const auto& item){string(out,item);});out<<",\"additional\":";metadata(out,value.additional);out<<'}';
}
inline void camera(std::ostream& out,const CameraInfo& value) {
    out<<'{';
    const std::pair<const char*,const std::string*> strings[]={
        {"id",&value.id},{"name",&value.name},{"manufacturer",&value.manufacturer},{"model",&value.model},
        {"serialNumber",&value.serialNumber},{"firmwareVersion",&value.firmwareVersion},{"backend",&value.backend}};
    bool comma=false;for(const auto& [key,text]:strings) {if(comma) out<<',';comma=true;string(out,key);out<<':';string(out,*text);}
    out<<",\"position\":"<<static_cast<int>(value.position)<<",\"transport\":"<<static_cast<int>(value.transport)<<",\"connected\":";boolean(out,value.connected);
    out<<",\"sensor\":";
    if(!value.sensor) out<<"null";
    else {
        const auto& s=*value.sensor;out<<"{\"widthMm\":";optionalNumber(out,s.widthMm);out<<",\"heightMm\":";optionalNumber(out,s.heightMm);
        out<<",\"cropFactor\":";optionalNumber(out,s.cropFactor);out<<",\"resolution\":";if(s.resolution) size(out,*s.resolution);else out<<"null";
        out<<",\"bitDepth\":";if(s.bitDepth) out<<*s.bitDepth;else out<<"null";out<<",\"colorFilterArray\":";string(out,s.colorFilterArray);
        out<<",\"additional\":";metadata(out,s.additional);out<<'}';
    }
    out<<",\"lenses\":";array(out,value.lenses,[&](const LensInfo& lens) {
        out<<"{\"id\":";string(out,lens.id);out<<",\"manufacturer\":";string(out,lens.manufacturer);out<<",\"model\":";string(out,lens.model);
        out<<",\"mount\":";string(out,lens.mount);out<<",\"serialNumber\":";string(out,lens.serialNumber);out<<",\"projection\":"<<static_cast<int>(lens.projection);
        out<<",\"focalLengthMm\":";range(out,lens.focalLengthMm);out<<",\"apertureFNumber\":";range(out,lens.apertureFNumber);
        out<<",\"focusDistanceMeters\":";range(out,lens.focusDistanceMeters);out<<",\"currentFocalLengthMm\":";optionalNumber(out,lens.currentFocalLengthMm);
        out<<",\"currentApertureFNumber\":";optionalNumber(out,lens.currentApertureFNumber);out<<",\"horizontalFieldOfViewDegrees\":";optionalNumber(out,lens.horizontalFieldOfViewDegrees);
        out<<",\"interchangeable\":";boolean(out,lens.interchangeable);out<<",\"additional\":";metadata(out,lens.additional);out<<'}';
    });
    out<<",\"formats\":";array(out,value.formats,[&](const CameraFormat& format) {
        out<<"{\"id\":";string(out,format.id);out<<",\"pixelFormat\":";string(out,format.pixelFormat);out<<",\"codec\":";string(out,format.codec);
        out<<",\"resolution\":";size(out,format.resolution);out<<",\"frameRates\":";array(out,format.frameRates,[&](const auto& item){range(out,item);});
        out<<",\"horizontalFieldOfViewDegrees\":";optionalNumber(out,format.horizontalFieldOfViewDegrees);out<<",\"hdrSupported\":";boolean(out,format.hdrSupported);
        out<<",\"additional\":";metadata(out,format.additional);out<<'}';
    });
    out<<",\"activeFormatId\":";if(value.activeFormatId) string(out,*value.activeFormatId);else out<<"null";
    out<<",\"controls\":";array(out,value.controls,[&](const auto& item){capability(out,item);});out<<",\"additional\":";metadata(out,value.additional);out<<'}';
}
inline void controlValue(std::ostream& out,const ControlValue& value) {
    std::visit([&](const auto& item) {
        using T=std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T,double>) number(out,item);
        else if constexpr(std::is_same_v<T,std::string>) string(out,item);
        else out<<static_cast<int>(item);
    },value);
}
inline void featureValue(std::ostream& out,const FeatureValue& value) {
    std::visit([&](const auto& item) {
        using T=std::decay_t<decltype(item)>;
        if constexpr(std::is_same_v<T,std::string>) string(out,item);
        else if constexpr(std::is_same_v<T,double>) number(out,item);
        else if constexpr(std::is_same_v<T,bool>) boolean(out,item);
        else if constexpr(std::is_same_v<T,std::int64_t>) out<<item;
        else array(out,item,[&](const auto& element) {
            using E=std::decay_t<decltype(element)>;
            if constexpr(std::is_same_v<E,std::byte>) out<<std::to_integer<unsigned>(element);
            else if constexpr(std::is_same_v<E,double>) number(out,element);
            else out<<element;
        });
    },value);
}
inline void features(std::ostream& out,const CameraFeatures& value) {
    out<<"{\"camera\":";camera(out,value.camera);out<<",\"controls\":";array(out,value.controls,[&](const auto& item) {
        out<<"{\"capability\":";capability(out,item.capability);out<<",\"value\":";
        if(item.value) controlValue(out,*item.value);else out<<"null";
        out<<",\"error\":";if(item.value) out<<"null";else error(out,item.value.error());out<<'}';
    });
    out<<",\"nativeFeatures\":";array(out,value.nativeFeatures,[&](const NativeFeature& item) {
        out<<"{\"id\":";string(out,item.id);out<<",\"name\":";string(out,item.name);out<<",\"type\":"<<static_cast<int>(item.type);
        out<<",\"readable\":";boolean(out,item.readable);out<<",\"writable\":";boolean(out,item.writable);out<<",\"range\":";range(out,item.range);
        out<<",\"choices\":";array(out,item.choices,[&](const auto& choice){out<<"{\"id\":";string(out,choice.id);out<<",\"name\":";string(out,choice.name);out<<'}';});
        out<<",\"valueQueried\":";boolean(out,item.value.has_value());out<<",\"value\":";
        if(item.value && *item.value) featureValue(out,**item.value);else out<<"null";
        out<<",\"error\":";if(item.value && !*item.value) error(out,item.value->error());else out<<"null";
        out<<",\"additional\":";metadata(out,item.additional);out<<'}';
    });out<<'}';
}
inline void report(std::ostream& out,const std::vector<CameraFeatureResult>& values) {
    out<<"{\"schemaVersion\":1,\"devices\":";array(out,values,[&](const auto& item) {
        out<<"{\"deviceId\":";string(out,item.deviceId);out<<",\"features\":";
        if(item.features) features(out,*item.features);else out<<"null";
        out<<",\"error\":";if(item.features) out<<"null";else error(out,item.features.error());out<<'}';
    });out<<"}\n";
}
} // namespace iiCameraIntergration::cli
