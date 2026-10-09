#include <iiCameraIntergration/ImageProcessing.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace iiCameraIntergration;
int main(int argc,char** argv) {
    try {
        if(argc!=5&&argc!=6) {std::cerr<<"Usage: iicamera-process INPUT_FORMAT SOURCE OUTPUT.ppm metadata|srgb|rec709|linear [EXPOSURE_EV]\n";return 2;}
        PhotoProcessingOptions options;
        const std::string encoding=argv[4];
        if(encoding=="metadata") options.encoding=FrameEncoding::Metadata;
        else if(encoding=="srgb") options.encoding=FrameEncoding::Srgb;
        else if(encoding=="rec709") options.encoding=FrameEncoding::Rec709;
        else if(encoding=="linear") options.encoding=FrameEncoding::LinearSrgb;
        else throw std::invalid_argument("Invalid color encoding");
        if(argc==6) {std::size_t used{};options.development.light.exposure=std::stod(argv[5],&used);if(used!=std::string(argv[5]).size())throw std::invalid_argument("Invalid exposure");}
        auto photo=captureAndProcessPhoto(MediaSource{.location=argv[2],.inputFormat=argv[1]},options);
        if(!photo) {std::cerr<<photo.error().message<<'\n';return 1;}
        auto frame=photoFrame(*photo);if(!frame){std::cerr<<frame.error().message<<'\n';return 1;}
        auto* output=std::fopen(argv[3],"wbx");if(!output){std::cerr<<"Cannot create exclusive output\n";return 1;}
        bool ok=std::fprintf(output,"P6\n%u %u\n255\n",frame->dimensions.width,frame->dimensions.height)>0;
        ok=std::fwrite(frame->pixels.data(),1,frame->pixels.size(),output)==frame->pixels.size()&&ok;
        ok=std::fclose(output)==0&&ok;
        if(!ok){std::error_code ignored;std::filesystem::remove(argv[3],ignored);std::cerr<<"Cannot write output frame\n";return 1;}
        std::cout<<"Processed "<<frame->dimensions.width<<'x'<<frame->dimensions.height<<" sRGB pixels; luminance="<<photo->histogram.luminance.mean<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
