#include "win_editor_chrome.h"
#include "win_editor_images.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_text.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"
#include "win_storage.h"
#include <wrl/client.h>
#include <ole2.h>

namespace yd::editor {

void render(Graphics& g,const Scene& s,bool animate,bool screenTheme){StringFormat fmt(StringFormat::GenericTypographic());for(auto& p:s.items){if(p.points.empty())continue;if(animate&&screenTheme&&inlineEdit&&p.type==Primitive::Text&&p.owner==inlineTarget.owner&&p.textRole==inlineTarget.role&&p.textIndex==inlineTarget.index)continue;Color color=rgb(p.color);if(screenTheme)color=appearance::documentColor(color,p.type==Primitive::Box&&p.filled&&p.width==0);if(p.opacity!=255)color=Color(p.opacity,color.GetR(),color.GetG(),color.GetB());Pen pen(color,float(p.width));pen.SetStartCap(p.flatStart?LineCapFlat:LineCapRound);pen.SetEndCap(p.flatEnd?LineCapFlat:LineCapRound);if(p.dash){REAL w=std::max(.1f,float(p.width));REAL pattern[]={p.dash==1?4.f/w:1.f/w,p.dash==1?3.f/w:2.f/w};pen.SetDashPattern(pattern,2);}SolidBrush brush(color);std::vector<PointF> ps;for(auto v:p.points)ps.push_back({float(v.x),float(v.y)});
 if(p.type==Primitive::Line&&ps.size()>=2)g.DrawLines(&pen,ps.data(),INT(ps.size()));else if(p.type==Primitive::Bezier&&ps.size()>=4){GraphicsPath path;path.AddBeziers(ps.data(),INT(ps.size()));if(p.closed)path.CloseFigure();if(p.filled)g.FillPath(&brush,&path);if(p.width>0)g.DrawPath(&pen,&path);}else if(p.type==Primitive::Polygon&&ps.size()>=3)g.FillPolygon(&brush,ps.data(),INT(ps.size()));else if(p.type==Primitive::Text){FontFamily ff(ui::wide(p.font).c_str());Font f(&ff,float(p.fontSize),(p.face&1?FontStyleBold:0)|(p.face&2?FontStyleItalic:0)|(p.face&4?FontStyleUnderline:0),UnitPixel);auto w=ui::wide(p.text);auto saved=g.Save();g.TranslateTransform(ps[0].X,ps[0].Y);g.RotateTransform(float(p.rotation));// Regular graph type must scale from outlines rather than hinted glyph bitmaps.
 // This also keeps rotated energy-axis captions smooth and EMF output vector-based.
 if(!(p.face&1)||p.textStrokeWidth!=0){
     GraphicsPath glyphs;
     if(glyphs.AddString(w.c_str(),-1,&ff,f.GetStyle(),float(p.fontSize),PointF(0,0),&fmt)==Ok){
         g.SetSmoothingMode(SmoothingModeHighQuality);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
         if(p.textStrokeWidth<0){
             GraphicsPath* rim=glyphs.Clone();Pen erosion(color,float(-p.textStrokeWidth));erosion.SetLineJoin(LineJoinRound);
             if(rim&&rim->Widen(&erosion)==Ok){
                 // XOR the outline band inside the original glyph. FillPath keeps
                 // antialiased vector edges; Region filling would quantize thin type.
                 GraphicsPath interior(FillModeAlternate);interior.AddPath(&glyphs,FALSE);interior.AddPath(rim,FALSE);
                 g.SetClip(&glyphs,CombineModeIntersect);g.FillPath(&brush,&interior);
             }else g.FillPath(&brush,&glyphs);
             delete rim;
         }else{g.FillPath(&brush,&glyphs);if(p.textStrokeWidth>0){Pen ink(color,float(p.textStrokeWidth));ink.SetLineJoin(LineJoinRound);g.DrawPath(&ink,&glyphs);}}
     }else g.DrawString(w.c_str(),-1,&f,PointF(0,0),&fmt,&brush);
 }else g.DrawString(w.c_str(),-1,&f,PointF(0,0),&fmt,&brush);
 g.Restore(saved);}else if(ps.size()>=2){RectF box(std::min(ps[0].X,ps[1].X),std::min(ps[0].Y,ps[1].Y),std::abs(ps[1].X-ps[0].X),std::abs(ps[1].Y-ps[0].Y));if(p.type==Primitive::Ellipse){if(p.filled)g.FillEllipse(&brush,box);else g.DrawEllipse(&pen,box);}else if(p.type==Primitive::Box){if(p.filled)g.FillRectangle(&brush,box);else g.DrawRectangle(&pen,box);}else if(p.type==Primitive::Image){if(auto img=decode(p.imageHex,animate))g.DrawImage(img,box);}}}}
HENHMETAFILE officePicture(const Document& d){
    auto sc=makeScene(d,measure);auto b=sc.bounds;HDC dc=GetDC(nullptr);
    RectF frame(0,0,float(b.w+12),float(b.h+12));
    Metafile metafile(dc,frame,MetafileFrameUnitPoint,EmfTypeEmfPlusDual,L"YoungDraw\0Drawing\0");
    {Graphics g(&metafile);g.SetPageUnit(UnitPixel);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    // EMF physical frames use device millimetres, which can differ from logical DPI.
    float dx=GetDeviceCaps(dc,HORZSIZE)>0?float(GetDeviceCaps(dc,HORZRES)*25.4/GetDeviceCaps(dc,HORZSIZE)):g.GetDpiX();
    float dy=GetDeviceCaps(dc,VERTSIZE)>0?float(GetDeviceCaps(dc,VERTRES)*25.4/GetDeviceCaps(dc,VERTSIZE)):g.GetDpiY();
    g.ScaleTransform(dx/72.f,dy/72.f);g.TranslateTransform(float(6-b.x),float(6-b.y));render(g,sc);}
    ReleaseDC(nullptr,dc);return metafile.GetHENHMETAFILE();
}
std::unique_ptr<Bitmap> drawingBitmap(const Document& d,bool transparent,float dpi){
    auto sc=makeScene(d,measure);if(sc.items.empty())throw std::runtime_error("내보낼 그림이 없습니다.");
    auto b=sc.bounds;
    const double scale=std::min(dpi>0?dpi/72.:2.5,(dpi>0?6000.:2000.)/std::max(b.w,b.h));
    const auto format=dpi>0&&!transparent?PixelFormat24bppRGB:PixelFormat32bppARGB;
    auto image=std::make_unique<Bitmap>(std::max(1,int(std::ceil(b.w*scale))),std::max(1,int(std::ceil(b.h*scale))),format);
    if(image->GetLastStatus()!=Ok)throw std::runtime_error("그림을 만들 수 없습니다.");
    const float outputDpi=float(scale*72);
    if(dpi>0&&image->SetResolution(outputDpi,outputDpi)!=Ok)throw std::runtime_error("그림 해상도를 설정할 수 없습니다.");
    {
        Graphics g(image.get());g.SetPageUnit(UnitPixel);g.Clear(transparent?Color(0,0,0,0):Color(255,255,255,255));g.SetSmoothingMode(SmoothingModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        if(dpi>0)g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.TranslateTransform(float(-b.x*scale),float(-b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);
    }
    return image;
}
namespace {
std::string encodeBitmap(Bitmap& image,const wchar_t* mime,const EncoderParameters* parameters=nullptr){
    UINT count=0,size=0;GetImageEncodersSize(&count,&size);std::vector<BYTE> memory(size);auto* encoders=reinterpret_cast<ImageCodecInfo*>(memory.data());
    if(!size||GetImageEncoders(count,size,encoders)!=Ok)throw std::runtime_error("그림 인코더를 찾을 수 없습니다.");
    const CLSID* encoder=nullptr;for(UINT i=0;i<count;i++)if(wcscmp(encoders[i].MimeType,mime)==0){encoder=&encoders[i].Clsid;break;}
    if(!encoder)throw std::runtime_error("그림 인코더를 찾을 수 없습니다.");
    Microsoft::WRL::ComPtr<IStream> stream;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||image.Save(stream.Get(),encoder,parameters)!=Ok)throw std::runtime_error("그림 인코딩에 실패했습니다.");
    STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart>128*1024*1024)throw std::runtime_error("그림 데이터가 너무 큽니다.");
    LARGE_INTEGER start{};if(FAILED(stream->Seek(start,STREAM_SEEK_SET,nullptr)))throw std::runtime_error("그림 데이터를 읽을 수 없습니다.");
    std::string bytes(size_t(stat.cbSize.QuadPart),'\0');ULONG read=0;
    if(FAILED(stream->Read(bytes.data(),ULONG(bytes.size()),&read))||read!=bytes.size())throw std::runtime_error("그림 데이터를 읽을 수 없습니다.");
    return bytes;
}
}
std::string encodePng(Bitmap& image){return encodeBitmap(image,L"image/png");}
void writePreview(const std::wstring& path,const Document& d,bool transparent){
    auto image=drawingBitmap(d,transparent);storage::writeAtomic(path,encodePng(*image));
}
void writeTiff(const std::wstring& path,const Document& d){
    auto image=drawingBitmap(d,false,highResolutionDpi);
    ULONG compression=EncoderValueCompressionLZW;
    EncoderParameters parameters{1, {{EncoderCompression,1,EncoderParameterValueTypeLong,&compression}}};
    storage::writeAtomic(path,encodeBitmap(*image,L"image/tiff",&parameters));
}

} // namespace yd::editor
