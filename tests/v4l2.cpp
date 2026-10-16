#include "iiCameraIntergration/detail/V4L2.h"
#include <linux/videodev2.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace iiCameraIntergration;
namespace {
#define CHECK(x) do {if(!(x)) throw std::runtime_error(#x);} while(false)
struct Driver {
    bool legacy{},payloadFailure{},disconnected{};int writes{},reads{};int brightness{20};
    int call(unsigned long request,void* pointer) {
        if(disconnected){errno=ENODEV;return -1;}
        if(request==VIDIOC_QUERYCAP) {
            auto& cap=*static_cast<v4l2_capability*>(pointer);cap.capabilities=V4L2_CAP_DEVICE_CAPS;cap.device_caps=V4L2_CAP_VIDEO_CAPTURE;
            std::memcpy(cap.card,"Fixture camera",15);std::memcpy(cap.bus_info,"usb-fixture",12);return 0;
        }
        if(request==VIDIOC_ENUM_FMT) {
            auto& format=*static_cast<v4l2_fmtdesc*>(pointer);if(format.index) return missing();
            format.pixelformat=V4L2_PIX_FMT_YUYV;std::memcpy(format.description,"YUYV fixture",13);return 0;
        }
        if(request==VIDIOC_ENUM_FRAMESIZES) {
            auto& size=*static_cast<v4l2_frmsizeenum*>(pointer);if(size.index) return missing();
            size.type=V4L2_FRMSIZE_TYPE_DISCRETE;size.discrete={640,480};return 0;
        }
        if(request==VIDIOC_ENUM_FRAMEINTERVALS) {
            auto& rate=*static_cast<v4l2_frmivalenum*>(pointer);if(rate.index) return missing();
            rate.type=V4L2_FRMIVAL_TYPE_DISCRETE;rate.discrete={1,30};return 0;
        }
        if(request==VIDIOC_QUERY_EXT_CTRL) {
            if(legacy) {errno=ENOTTY;return -1;}
            auto& c=*static_cast<v4l2_query_ext_ctrl*>(pointer);
            constexpr std::uint32_t ids[]={V4L2_CID_BRIGHTNESS,V4L2_CID_PRIVATE_BASE,V4L2_CID_PRIVATE_BASE+1};
            const bool next=c.id&V4L2_CTRL_FLAG_NEXT_CTRL;const auto after=c.id&~(V4L2_CTRL_FLAG_NEXT_CTRL|V4L2_CTRL_FLAG_NEXT_COMPOUND);
            std::uint32_t id=after;
            if(next) {id=0;for(auto item:ids) if(item>after) {id=item;break;}}
            if(id==V4L2_CID_BRIGHTNESS) {c={};c.id=id;c.type=V4L2_CTRL_TYPE_INTEGER;c.minimum=0;c.maximum=100;c.step=5;std::memcpy(c.name,"Brightness",11);return 0;}
            if(id==V4L2_CID_PRIVATE_BASE) {c={};c.id=id;c.type=V4L2_CTRL_TYPE_INTEGER64;c.minimum=0;c.maximum=INT64_MAX;c.step=1;std::memcpy(c.name,"Vendor counter",15);return 0;}
            if(id==V4L2_CID_PRIVATE_BASE+1) {c={};c.id=id;c.type=V4L2_CTRL_TYPE_U8;c.flags=V4L2_CTRL_FLAG_HAS_PAYLOAD|V4L2_CTRL_FLAG_READ_ONLY;c.elem_size=1;c.elems=4;std::memcpy(c.name,"Vendor bytes",13);return 0;}
            return missing();
        }
        if(request==VIDIOC_QUERYCTRL) {
            auto& c=*static_cast<v4l2_queryctrl*>(pointer);
            if(c.id!=V4L2_CID_BRIGHTNESS) return missing();
            c.type=V4L2_CTRL_TYPE_INTEGER;c.minimum=0;c.maximum=100;c.step=5;std::memcpy(c.name,"Brightness",11);return 0;
        }
        if(request==VIDIOC_G_EXT_CTRLS) {
            if(legacy) {errno=ENOTTY;return -1;}++reads;
            auto& c=*static_cast<v4l2_ext_controls*>(pointer)->controls;
            if(c.id==V4L2_CID_BRIGHTNESS) c.value=brightness;
            else if(c.id==V4L2_CID_PRIVATE_BASE) c.value64=INT64_C(9007199254740993);
            else if(c.id==V4L2_CID_PRIVATE_BASE+1) {
                if(payloadFailure) {errno=EBUSY;return -1;}
                CHECK(c.size==4);std::memcpy(c.ptr,"DATA",4);
            } else return missing();return 0;
        }
        if(request==VIDIOC_G_CTRL) {++reads;auto& c=*static_cast<v4l2_control*>(pointer);if(c.id!=V4L2_CID_BRIGHTNESS) return missing();c.value=brightness;return 0;}
        if(request==VIDIOC_S_EXT_CTRLS) {++writes;brightness=static_cast<v4l2_ext_controls*>(pointer)->controls->value;return 0;}
        return missing();
    }
    static int missing(){errno=EINVAL;return -1;}
    detail::V4L2Ioctl io(){return [this](unsigned long request,void* pointer){return call(request,pointer);};}
};
}
int main() {
    try {
        Driver driver;auto report=detail::describeV4L2("/dev/video0",driver.io(),true,true);
        CHECK(report && validateCameraInfo(report->camera));
        CHECK(report->camera.transport==Transport::Usb && report->camera.formats.size()==1);
        CHECK(report->camera.formats[0].frameRates[0].minimum==30);
        CHECK(report->nativeFeatures.size()==3 && report->camera.controls.size()==1);
        CHECK(std::get<std::int64_t>(**report->nativeFeatures[1].value)==INT64_C(9007199254740993));
        CHECK(std::get<std::vector<std::byte>>(**report->nativeFeatures[2].value).size()==4);
        CHECK(std::get<double>(*report->controls[0].value)==20);
        const auto reads=driver.reads;
        report=detail::describeV4L2("/dev/video0",driver.io(),true,false);CHECK(report && driver.reads==reads && report->controls.empty());
        CHECK(!report->nativeFeatures[0].value);
        driver.payloadFailure=true;report=detail::describeV4L2("/dev/video0",driver.io(),true,true);
        CHECK(report && report->nativeFeatures[2].value && !*report->nativeFeatures[2].value);
        CHECK(report->nativeFeatures[2].value->error().code==ErrorCode::Busy && report->controls[0].value);
        auto bad=detail::applyV4L2(driver.io(),{ControlId::Brightness,23.0});CHECK(!bad && driver.writes==0);
        auto good=detail::applyV4L2(driver.io(),{ControlId::Brightness,25.0});CHECK(good && std::get<double>(*good)==25 && driver.writes==1);
        driver.legacy=true;report=detail::describeV4L2("/dev/video0",driver.io(),false,true);
        CHECK(report && report->nativeFeatures.size()==1 && !report->camera.controls[0].writable && report->controls[0].value);
        driver.disconnected=true;report=detail::describeV4L2("/dev/video0",driver.io(),false,true);
        CHECK(!report && report.error().code==ErrorCode::NotFound);
        std::cout<<"V4L2 artificial driver enumeration, payload, legacy, partial failure and command contracts passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
