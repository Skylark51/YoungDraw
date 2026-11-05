#include "win_editor_chrome.h"
#include "win_editor_images.h"
#include "win_editor_render.h"
#include "win_editor_state.h"
#include "win_editor_text.h"
#include "win_dialogs.h"
#include "win_theme.h"
#include "win_ui.h"

namespace yd::editor {

void render(Graphics& g,const Scene& s,bool animate,bool screenTheme){StringFormat fmt(StringFormat::GenericTypographic());for(auto& p:s.items){if(p.points.empty())continue;if(animate&&screenTheme&&inlineEdit&&p.type==Primitive::Text&&p.owner==inlineTarget.owner&&p.textRole==inlineTarget.role&&p.textIndex==inlineTarget.index)continue;Color color=rgb(p.color);if(screenTheme)color=appearance::documentColor(color,p.type==Primitive::Box&&p.filled&&p.width==0);if(p.opacity!=255)color=Color(p.opacity,color.GetR(),color.GetG(),color.GetB());Pen pen(color,float(p.width));pen.SetStartCap(p.flatStart?LineCapFlat:LineCapRound);pen.SetEndCap(p.flatEnd?LineCapFlat:LineCapRound);if(p.dash){REAL w=std::max(.1f,float(p.width));REAL pattern[]={p.dash==1?4.f/w:1.f/w,p.dash==1?3.f/w:2.f/w};pen.SetDashPattern(pattern,2);}SolidBrush brush(color);std::vector<PointF> ps;for(auto v:p.points)ps.push_back({float(v.x),float(v.y)});
 if(p.type==Primitive::Line&&ps.size()>=2)g.DrawLines(&pen,ps.data(),INT(ps.size()));else if(p.type==Primitive::Bezier&&ps.size()>=4){GraphicsPath path;path.AddBeziers(ps.data(),INT(ps.size()));if(p.closed)path.CloseFigure();if(p.filled)g.FillPath(&brush,&path);if(p.width>0)g.DrawPath(&pen,&path);}else if(p.type==Primitive::Polygon&&ps.size()>=3)g.FillPolygon(&brush,ps.data(),INT(ps.size()));else if(p.type==Primitive::Text){FontFamily ff(ui::wide(p.font).c_str());Font f(&ff,float(p.fontSize),(p.face&1?FontStyleBold:0)|(p.face&2?FontStyleItalic:0)|(p.face&4?FontStyleUnderline:0),UnitPixel);auto w=ui::wide(p.text);auto saved=g.Save();g.TranslateTransform(ps[0].X,ps[0].Y);g.RotateTransform(float(p.rotation));// Regular graph type must scale from outlines rather than hinted glyph bitmaps.
 // This also keeps rotated energy-axis captions smooth and EMF output vector-based.
 if(!(p.face&1)){
     GraphicsPath glyphs;
     if(glyphs.AddString(w.c_str(),-1,&ff,f.GetStyle(),float(p.fontSize),PointF(0,0),&fmt)==Ok){
         g.SetSmoothingMode(SmoothingModeHighQuality);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.FillPath(&brush,&glyphs);
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
void writePreview(const std::wstring& path,const Document& d,bool transparent){
    auto sc=makeScene(d,measure);auto b=sc.bounds;double scale=std::min(2.5,2000./std::max(b.w,b.h));
    Bitmap image(std::max(1,int(std::ceil(b.w*scale))),std::max(1,int(std::ceil(b.h*scale))),PixelFormat32bppARGB);
    {
        Graphics g(&image);g.Clear(transparent?Color(0,0,0,0):white);g.SetSmoothingMode(SmoothingModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.TranslateTransform(float(-b.x*scale),float(-b.y*scale));g.ScaleTransform(float(scale),float(scale));render(g,sc);
    }
    UINT n=0,bytes=0;GetImageEncodersSize(&n,&bytes);std::vector<BYTE> memory(bytes);auto* encoders=(ImageCodecInfo*)memory.data();GetImageEncoders(n,bytes,encoders);for(UINT i=0;i<n;i++)if(wcscmp(encoders[i].MimeType,L"image/png")==0){if(image.Save(path.c_str(),&encoders[i].Clsid,nullptr)!=Ok)throw std::runtime_error("Could not save preview");return;}throw std::runtime_error("PNG encoder unavailable");
}

} // namespace yd::editor
