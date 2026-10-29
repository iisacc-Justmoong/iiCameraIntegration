#include <iiCameraIntegration/ImageProcessing.h>
#include <iiGraphicAdjust/LightcraftFilters.h>
#include <iiGraphicAdjust/Image.h>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace iiCameraIntegration {
namespace {
Error invalid(std::string message) { return {ErrorCode::InvalidArgument,std::move(message)}; }
Error processingError(const iiGraphicAdjust::Error& e) {
    return {e.code==iiGraphicAdjust::ErrorCode::AllocationFailure ? ErrorCode::BackendFailure : ErrorCode::InvalidArgument, std::string(e.message)};
}
bool unknown(const std::string& tag) {return tag.empty() || tag=="unknown" || tag=="unspecified";}
Result<FrameEncoding> encodingOf(const RgbFrame& frame, FrameEncoding requested) {
    if (frame.metadata.contains("iiCamera.color.iccProfilePresent") && frame.metadata.at("iiCamera.color.iccProfilePresent")=="true")
        return std::unexpected(Error{ErrorCode::Unsupported,"ICC input requires host color normalization before photo processing"});
    if (!unknown(frame.colorSpace) && frame.colorSpace!="gbr" && frame.colorSpace!="rgb")
        return std::unexpected(Error{ErrorCode::Unsupported,"Photo processing requires RGB pixels"});
    if (!unknown(frame.colorRange) && frame.colorRange!="pc" && frame.colorRange!="jpeg" && frame.colorRange!="full")
        return std::unexpected(Error{ErrorCode::Unsupported,"Photo processing requires full-range RGB"});
    if (!unknown(frame.colorPrimaries) && frame.colorPrimaries!="bt709")
        return std::unexpected(Error{ErrorCode::Unsupported,"Photo processing requires Rec.709/sRGB primaries; wide-gamut input needs host conversion"});
    std::optional<FrameEncoding> tagged;
    if (frame.colorTransfer=="iec61966-2-1" || frame.colorTransfer=="srgb") tagged=FrameEncoding::Srgb;
    else if (frame.colorTransfer=="bt709") tagged=FrameEncoding::Rec709;
    else if (frame.colorTransfer=="linear") tagged=FrameEncoding::LinearSrgb;
    else if (!unknown(frame.colorTransfer))
        return std::unexpected(Error{ErrorCode::Unsupported,"Unsupported photo transfer function; HDR/other input needs host conversion"});
    if (requested==FrameEncoding::Metadata) {
        if (!tagged || frame.colorPrimaries!="bt709")
            return std::unexpected(Error{ErrorCode::Unsupported,"Missing photo color tags; supply a verified explicit encoding"});
        return *tagged;
    }
    if (requested!=FrameEncoding::Srgb && requested!=FrameEncoding::Rec709 && requested!=FrameEncoding::LinearSrgb)
        return std::unexpected(invalid("Invalid frame encoding"));
    if (tagged && *tagged!=requested) return std::unexpected(Error{ErrorCode::Unsupported,"Explicit encoding conflicts with the source transfer tag"});
    return requested;
}
float toSrgb(double value,FrameEncoding encoding) {
    if (encoding==FrameEncoding::Srgb) return static_cast<float>(value);
    const double linear=encoding==FrameEncoding::LinearSrgb ? value :
        (value<0.081 ? value/4.5 : std::pow((value+0.099)/1.099,1/0.45));
    return static_cast<float>(linear<=0.0031308 ? linear*12.92 : 1.055*std::pow(linear,1/2.4)-0.055);
}
Result<iiGraphicAdjust::Image> importFrame(const RgbFrame& f,FrameEncoding encoding) {
    const std::size_t w=f.dimensions.width,h=f.dimensions.height;
    if (!w || !h || w>std::numeric_limits<std::size_t>::max()/3 || w>std::numeric_limits<std::size_t>::max()/h)
        return std::unexpected(invalid("Empty or overflowing frame dimensions"));
    const auto row=w*3;
    if (f.rowStride<row || (h-1)> (std::numeric_limits<std::size_t>::max()-row)/f.rowStride)
        return std::unexpected(invalid("Invalid or overflowing RGB frame stride"));
    if (f.pixels.size()<(h-1)*f.rowStride+row) return std::unexpected(invalid("Truncated RGB frame storage"));
    iiGraphicAdjust::Image image{w,h,std::vector<iiGraphicAdjust::Rgba>(w*h)};
    for (std::size_t y=0;y<h;++y) for(std::size_t x=0;x<w;++x) {
        const auto offset=y*f.rowStride+x*3;
        image.pixels[y*w+x]={toSrgb(f.pixels[offset]/255.0,encoding),toSrgb(f.pixels[offset+1]/255.0,encoding),toSrgb(f.pixels[offset+2]/255.0,encoding),1};
    }
    return image;
}
}
Result<ProcessedPhoto> processPhoto(const RgbFrame& frame,const PhotoProcessingOptions& options) {
    auto encoding=encodingOf(frame,options.encoding); if(!encoding) return std::unexpected(encoding.error());
    try {
        auto imported=importFrame(frame,*encoding); if(!imported) return std::unexpected(imported.error());
        auto oriented=iiGraphicAdjust::applyOrientation(imported->view(),options.orientation);
        if(!oriented) return std::unexpected(processingError(oriented.error()));
        iiGraphicAdjust::Image image=std::move(*oriented);
        if(options.crop) {
            auto cropped=iiGraphicAdjust::crop(image.view(),*options.crop);
            if(!cropped) return std::unexpected(processingError(cropped.error()));
            image=std::move(*cropped);
        }
        auto developed=iiGraphicAdjust::develop(image.view(),options.development);
        if(!developed) return std::unexpected(processingError(developed.error()));
        image=std::move(*developed);
        if(options.resize) {
            const auto& r=*options.resize;
            auto resized=[&]()->iiGraphicAdjust::Result<iiGraphicAdjust::Image> {
                switch(r.resampler) {
                case PhotoResampler::Box:return iiGraphicAdjust::resizeBox(image.view(),r.width,r.height);
                case PhotoResampler::Bilinear:return iiGraphicAdjust::resizeBilinear(image.view(),r.width,r.height);
                case PhotoResampler::Mitchell:return iiGraphicAdjust::resizeMitchell(image.view(),r.width,r.height);
                case PhotoResampler::Lanczos3:return iiGraphicAdjust::resizeLanczos3(image.view(),r.width,r.height);
                }
                return std::unexpected(iiGraphicAdjust::Error{iiGraphicAdjust::ErrorCode::InvalidParameter,"Invalid photo resampler"});
            }();
            if(!resized) return std::unexpected(processingError(resized.error()));
            image=std::move(*resized);
        }
        auto histogram=iiGraphicAdjust::imageHistogram(image.view(),options.histogram);
        if(!histogram) return std::unexpected(processingError(histogram.error()));
        ProcessedPhoto result;
        result.image=std::move(image); result.histogram=std::move(*histogram);
        result.sourceMetadata=frame.metadata;
        result.presentationTimestamp=frame.presentationTimestamp; result.timeBase=frame.timeBase;
        result.processingMetadata={{"output.encoding","sRGB"},{"output.alpha","straight, opaque"},
            {"source.interpretation",*encoding==FrameEncoding::Srgb?"sRGB":*encoding==FrameEncoding::Rec709?"Rec.709":"linear sRGB"},
            {"source.encodingExplicit",options.encoding==FrameEncoding::Metadata?"false":"true"},
            {"orientation.applied",std::to_string(static_cast<int>(options.orientation))},{"orientation.output","1"},
            {"pipeline","import, orientation, crop, develop, resize, histogram"}};
        return result;
    } catch(const std::bad_alloc&) {return std::unexpected(Error{ErrorCode::BackendFailure,"Photo processing allocation failed"});}
      catch(const std::length_error&) {return std::unexpected(Error{ErrorCode::BackendFailure,"Photo processing exceeds storage limits"});}
}
Result<ProcessedPhoto> captureAndProcessPhoto(const MediaSource& source,const PhotoProcessingOptions& options,const DecodeOptions& decode) {
    auto frame=decodeRgbFrame(source,decode); if(!frame) return std::unexpected(frame.error());
    return processPhoto(*frame,options);
}
Result<RgbFrame> photoFrame(const ProcessedPhoto& photo) {
    auto bytes=iiGraphicAdjust::toRgba8(photo.image.view()); if(!bytes) return std::unexpected(processingError(bytes.error()));
    if(photo.image.width>std::numeric_limits<std::uint32_t>::max() || photo.image.height>std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(invalid("Processed photo dimensions exceed frame representation"));
    for(const auto& pixel:photo.image.pixels) if(pixel.a!=1.F)
        return std::unexpected(Error{ErrorCode::Unsupported,"RGB photo export requires opaque alpha"});
    try {
        RgbFrame frame; frame.dimensions={static_cast<std::uint32_t>(photo.image.width),static_cast<std::uint32_t>(photo.image.height)};
        frame.rowStride=photo.image.width*3; frame.pixels.resize(photo.image.width*photo.image.height*3);
        for(std::size_t i=0;i<photo.image.pixels.size();++i) for(std::size_t c=0;c<3;++c) frame.pixels[i*3+c]=(*bytes)[i*4+c];
        frame.colorSpace="gbr"; frame.colorRange="pc"; frame.colorTransfer="iec61966-2-1"; frame.colorPrimaries="bt709";
        for(const auto& [key,value]:photo.sourceMetadata) frame.metadata["iiCamera.photo.source."+key]=value;
        for(const auto& [key,value]:photo.processingMetadata) frame.metadata["iiCamera.photo."+key]=value;
        frame.metadata["iiCamera.color.iccProfilePresent"]="false";
        frame.presentationTimestamp=photo.presentationTimestamp; frame.timeBase=photo.timeBase;
        return frame;
    } catch(const std::bad_alloc&) {return std::unexpected(Error{ErrorCode::BackendFailure,"Photo export allocation failed"});}
      catch(const std::length_error&) {return std::unexpected(Error{ErrorCode::BackendFailure,"Photo export exceeds storage limits"});}
}
}
