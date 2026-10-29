#include <iiFileProviderStorage.h>
#include <iiCameraIntegration/iiCameraIntegration.h>
#include "FeatureJson.h"
#include "FileProviderPort.h"
#include <algorithm>
#include <iostream>
#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <string>

using namespace iiCameraIntegration;
namespace {
int failure(const Error& e) {
    std::cerr << "error=" << static_cast<int>(e.code) << " native=" << e.nativeCode << " " << e.message << '\n';
    return 1;
}
void describe(const MediaInfo& m) {
    std::cout << "format=" << m.formatName << " streams=" << m.streams.size() << '\n';
    for (const auto& stream : m.streams) {
        std::cout << "stream=" << stream.index << " codec=" << stream.codecName
                  << " dimensions=" << stream.dimensions.width << 'x' << stream.dimensions.height
                  << " rate=" << stream.frameRate.value() << " sample_rate=" << stream.sampleRate
                  << " channels=" << stream.channels << '\n';
    }
}
}
int main(int argc, char** argv) {
    const std::string mode = argc>1 ? argv[1] : "--help";
    if (mode=="--features") {
        FeatureQueryOptions options;std::string id;
        for(int i=2;i<argc;++i) {
            const std::string argument=argv[i];
            if(argument=="--metadata-only") options.includeCurrentValues=false;
            else if(argument.starts_with("--")) return failure(Error{ErrorCode::InvalidArgument,"Unknown feature query option"});
            else if(id.empty()) id=argument;
            else return failure(Error{ErrorCode::InvalidArgument,"Expected one device ID and optional --metadata-only"});
        }
        NativeCameraSystem system;
        Result<std::vector<CameraFeatureResult>> result=std::vector<CameraFeatureResult>{};
        if(id.empty()) result=system.features(options);
        else result->push_back({id,system.features(id,options)});
        if(!result) return failure(result.error());
        cli::report(std::cout,*result);
        return std::any_of(result->begin(),result->end(),[](const auto& item){return !item.features;})?1:0;
    }
    if (mode=="--catalog") {
        auto backend=mediaBackendInfo(); if (!backend) return failure(backend.error());
        auto codecs=codecCatalog(); if (!codecs) return failure(codecs.error());
        auto formats=formatCatalog(); if (!formats) return failure(formats.error());
        std::size_t decoders{},encoders{},inputs{},outputs{};
        for (const auto& codec : *codecs) { decoders+=!codec.decoders.empty(); encoders+=!codec.encoders.empty(); }
        for (const auto& format : *formats) { inputs+=format.inputAvailable; outputs+=format.outputAvailable; }
        std::cout << "backend=" << backend->name << " codec_identities=" << codecs->size()
                  << " decodable_identities=" << decoders << " encodable_identities=" << encoders
                  << " format_entries=" << formats->size() << " inputs=" << inputs << " outputs=" << outputs << '\n';
        for (const auto& [name,version] : backend->versions) std::cout << name << '=' << version << '\n';
        for (const auto& [name,license] : backend->licenses) std::cout << name << "_license=" << license << '\n';
        return 0;
    }
    if (mode=="--cameras") {
        NativeCameraSystem system;
        std::cout << "permission=" << static_cast<int>(system.permissionStatus()) << '\n';
        auto cameras=system.cameras(); if (!cameras) return failure(cameras.error());
        std::cout << "cameras=" << cameras->size() << '\n';
        for (const auto& camera : *cameras) {
            std::cout << "id=" << camera.id << " name=" << camera.name << " backend=" << camera.backend
                      << " formats=" << camera.formats.size() << " controls=" << camera.controls.size() << '\n';
        }
        return 0;
    }
    if (mode=="--probe" && argc==3) {
        auto info=probeMedia(MediaSource{argv[2]}); if (!info) return failure(info.error());
        describe(*info); return 0;
    }
    if (mode=="--frame" && argc==5) {
        MediaSource source; source.inputFormat=argv[2]; source.location=argv[3];
        auto frame=decodeRgbFrame(source); if(!frame) return failure(frame.error());
        auto* output=iiFileProvider::native::openStream(argv[4],"wbx");
        if (!output) return failure(Error{errno==EEXIST ? ErrorCode::Busy : ErrorCode::Io,"Cannot create exclusive PPM output",errno});
        bool ok=std::fprintf(output,"P6\n%u %u\n255\n",frame->dimensions.width,frame->dimensions.height)>0;
        ok=std::fwrite(frame->pixels.data(),1,frame->pixels.size(),output)==frame->pixels.size() && ok;
        ok=std::fclose(output)==0 && ok;
        if(!ok) {
            std::error_code ignored; std::filesystem::remove(argv[4],ignored);
            return failure(Error{ErrorCode::Io,"Cannot write PPM frame"});
        }
        iiFileProvider::native::Presentation::enqueue(std::filesystem::path(argv[4]));
        std::cout << "destination=" << argv[4] << " dimensions=" << frame->dimensions.width << 'x' << frame->dimensions.height << '\n';
        return 0;
    }
    if (mode=="--capture" && argc>=5 && argc<=6) {
        MediaSource source; source.inputFormat=argv[2]; source.location=argv[3];
        RecordingOptions options; if(argc==6) options.outputFormat=argv[5];
        auto report=recordMedia(source,argv[4],camera_media::fileProviderPort(),options); if(!report) return failure(report.error());
        std::cout << "destination=" << report->destination << " packets=" << report->packets
                  << " bytes=" << report->fileBytes << '\n'; return 0;
    }
    std::cout << "iicamera-inspect --catalog | --cameras | --probe FILE\n"
                 "iicamera-inspect --features [DEVICE_ID] [--metadata-only]\n"
                 "iicamera-inspect --capture INPUT_FORMAT SOURCE DESTINATION [OUTPUT_FORMAT]\n"
                 "iicamera-inspect --frame INPUT_FORMAT SOURCE DESTINATION.ppm\n";
    return mode=="--help" ? 0 : 2;
}
