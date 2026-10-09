#include <iiCameraIntergration/ImageProcessing.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
using namespace iiCameraIntergration;
namespace {
#define CHECK(x) do {if(!(x)) throw std::runtime_error(std::string("Failed: ")+#x+" line="+std::to_string(__LINE__));} while(false)
bool near(double a,double b,double tolerance=2e-5){return std::abs(a-b)<tolerance;}
RgbFrame fixture(){
    RgbFrame f; f.dimensions={2,2}; f.rowStride=8;
    f.pixels={255,0,0,0,255,0,99,88,0,0,255,128,128,128};
    f.colorSpace="gbr";f.colorRange="pc";f.colorTransfer="iec61966-2-1";f.colorPrimaries="bt709";
    f.metadata={{"vendor.camera.serial","test-001"},{"Orientation","6"}};
    f.presentationTimestamp=17; f.timeBase={1,1000};return f;
}
void import(){
    auto f=fixture(); const auto original=f.pixels;
    auto p=processPhoto(f);CHECK(p);CHECK(p->image.width==2&&p->image.height==2);
    CHECK(near(p->image.pixels[0].r,1)&&near(p->image.pixels[0].g,0));
    CHECK(near(p->image.pixels[1].g,1)&&near(p->image.pixels[2].b,1));
    CHECK(near(p->image.pixels[3].r,128/255.0));
    CHECK(p->sourceMetadata==f.metadata&&p->presentationTimestamp==17&&p->timeBase.denominator==1000);
    CHECK(p->histogram.pixelCount==4&&near(p->histogram.totalWeight,4));CHECK(f.pixels==original);
    auto output=photoFrame(*p);CHECK(output);CHECK(output->rowStride==6&&output->pixels.size()==12);
    CHECK(!output->metadata.contains("Orientation"));
    CHECK(output->metadata.at("iiCamera.photo.source.Orientation")=="6");
    CHECK(output->metadata.at("iiCamera.photo.orientation.output")=="1");
    CHECK(output->colorTransfer=="iec61966-2-1");
    for(float alpha:{0.F,.5F,.999F,.9999F}) {p->image.pixels[0].a=alpha;auto failed=photoFrame(*p);CHECK(!failed&&failed.error().code==ErrorCode::Unsupported);}
}
double linear(double e){return e<=.04045?e/12.92:std::pow((e+.055)/1.055,2.4);}
double encoded(double l){return l<=.0031308?12.92*l:1.055*std::pow(l,1/2.4)-.055;}
void pipeline(){
    RgbFrame f;f.dimensions={3,2};f.rowStride=9;f.colorSpace="gbr";f.colorRange="pc";f.colorTransfer="iec61966-2-1";f.colorPrimaries="bt709";
    for(unsigned v=1;v<=6;++v)for(unsigned c=0;c<3;++c)f.pixels.push_back(static_cast<std::uint8_t>(v*24));
    PhotoProcessingOptions o; o.orientation=iiGraphicAdjust::ExifOrientation::RightTop;
    o.crop=iiGraphicAdjust::PixelRect{0,1,2,2};o.development.light.exposure=1;
    auto p=processPhoto(f,o);CHECK(p);CHECK(p->image.width==2&&p->image.height==2);
    unsigned expected[]{5,2,6,3};
    double luminance=0;
    for(unsigned i=0;i<4;++i){auto l=std::min(1.0,2*linear(expected[i]*24/255.0));luminance+=l;CHECK(near(p->image.pixels[i].r,encoded(l),2e-4));}
    for(auto resampler:{PhotoResampler::Box,PhotoResampler::Bilinear,PhotoResampler::Mitchell,PhotoResampler::Lanczos3}) {
        o.resize=PhotoResize{1,1,resampler};auto resized=processPhoto(f,o);CHECK(resized);CHECK(resized->image.width==1&&resized->histogram.pixelCount==1);
        if(resampler==PhotoResampler::Box)CHECK(near(resized->image.pixels[0].r,encoded(luminance/4),2e-4));
    }
}
void encoding(){
    auto f=fixture();f.colorTransfer="unknown";f.colorPrimaries="unknown";
    CHECK(!processPhoto(f));PhotoProcessingOptions o;o.encoding=FrameEncoding::Srgb;CHECK(processPhoto(f,o));
    f.colorTransfer="bt709";CHECK(!processPhoto(f,o));o.encoding=FrameEncoding::Rec709;
    auto p=processPhoto(f,o);CHECK(p);double e=128/255.0;double l=std::pow((e+.099)/1.099,1/.45);CHECK(near(p->image.pixels[3].r,encoded(l),2e-4));
    f.colorTransfer="linear";o.encoding=FrameEncoding::LinearSrgb;p=processPhoto(f,o);CHECK(p&&near(p->image.pixels[3].r,encoded(e),2e-4));
    f.colorTransfer="smpte2084";CHECK(!processPhoto(f,o));f.colorTransfer="arib-std-b67";CHECK(!processPhoto(f,o));
    f=fixture();f.colorPrimaries="bt2020";CHECK(!processPhoto(f));f=fixture();f.metadata["iiCamera.color.iccProfilePresent"]="true";CHECK(!processPhoto(f,o));
    f=fixture();f.colorRange="tv";CHECK(!processPhoto(f));
}
void validation(){
    auto f=fixture();f.dimensions.width=0;CHECK(!processPhoto(f));
    f=fixture();f.rowStride=5;CHECK(!processPhoto(f));f=fixture();f.pixels.resize(7);CHECK(!processPhoto(f));
    f=fixture();f.rowStride=std::numeric_limits<std::size_t>::max();CHECK(!processPhoto(f));
    PhotoProcessingOptions o;o.orientation=static_cast<iiGraphicAdjust::ExifOrientation>(0);CHECK(!processPhoto(fixture(),o));
    o={};o.crop=iiGraphicAdjust::PixelRect{1,1,2,2};CHECK(!processPhoto(fixture(),o));
    o={};o.development.light.exposure=std::numeric_limits<double>::quiet_NaN();CHECK(!processPhoto(fixture(),o));
    o={};o.resize=PhotoResize{0,1};CHECK(!processPhoto(fixture(),o));
    o={};o.encoding=static_cast<FrameEncoding>(99);CHECK(!processPhoto(fixture(),o));
    o={};o.histogram.bins=1;CHECK(!processPhoto(fixture(),o));
}
void capture(){
    MediaSource source{.location="color=c=red:size=16x16:rate=10:duration=0.2",.inputFormat="lavfi"};
    PhotoProcessingOptions o;o.encoding=FrameEncoding::Srgb;o.resize=PhotoResize{8,8};
    auto p=captureAndProcessPhoto(source,o);
    if(!p&&p.error().code==ErrorCode::Unavailable) {std::cout<<"Media backend explicitly unavailable; capture capability branch verified\n";return;}
    CHECK(p);CHECK(p->image.width==8&&p->image.height==8);
    CHECK(p->image.pixels[0].r>.9F&&p->image.pixels[0].g<.1F);
    CHECK(p->histogram.pixelCount==64&&near(p->histogram.totalWeight,64));
    auto strict=captureAndProcessPhoto(source);CHECK(!strict&&strict.error().code==ErrorCode::Unsupported);
}
}
int main(int argc,char** argv){try{CHECK(argc==2);std::string_view c=argv[1];
    if(c=="import")import();else if(c=="pipeline")pipeline();else if(c=="encoding")encoding();else if(c=="validation")validation();else if(c=="capture")capture();else throw std::runtime_error("Unknown case");
    std::cout<<c<<" passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
