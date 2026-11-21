#include "scene.h"
#include "plot_data.h"
#include "mechanism_data.h"
#include <iomanip>
#include <sstream>
#include <queue>
#include <map>
#include <limits>
#include <stdexcept>
namespace yd {
namespace {
double segmentDistance(Vec p,Vec a,Vec b){
    Vec v=b-a;double length=v.x*v.x+v.y*v.y;
    double t=length>1e-16?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/length,0.,1.):0;
    return (p-a-v*t).length();
}
bool hitCubic(Vec p,Vec a,Vec b,Vec c,Vec d,double tolerance,int depth=0){
    if(p.x<std::min({a.x,b.x,c.x,d.x})-tolerance||p.x>std::max({a.x,b.x,c.x,d.x})+tolerance||
       p.y<std::min({a.y,b.y,c.y,d.y})-tolerance||p.y>std::max({a.y,b.y,c.y,d.y})+tolerance)return false;
    if(depth>=12||std::max(segmentDistance(b,a,d),segmentDistance(c,a,d))<=tolerance*.2)
        return segmentDistance(p,a,d)<=tolerance;
    Vec ab=(a+b)*.5,bc=(b+c)*.5,cd=(c+d)*.5,abc=(ab+bc)*.5,bcd=(bc+cd)*.5,m=(abc+bcd)*.5;
    return hitCubic(p,a,ab,abc,m,tolerance,depth+1)||hitCubic(p,m,bcd,cd,d,tolerance,depth+1);
}
bool inPolygon(Vec p,const std::vector<Vec>& points){
    bool inside=false;if(points.size()<3)return false;
    for(size_t i=0,j=points.size()-1;i<points.size();j=i++){
        auto a=points[i],b=points[j];
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
    }return inside;
}
}
bool hitObject(const Scene& scene,int owner,Vec point,double tolerance){
    for(const auto& item:scene.items){
        if(item.owner!=owner||item.points.empty())continue;
        const auto& ps=item.points;double t=std::max(.001,tolerance+item.width/2);
        if(item.type==Primitive::Text){
            auto it=scene.objectBounds.find(owner);if(it==scene.objectBounds.end())continue;
            auto b=it->second;if(point.x>=b.x-t&&point.y>=b.y-t&&point.x<=b.x+b.w+t&&point.y<=b.y+b.h+t)return true;
        }else if(item.type==Primitive::Bezier){
            for(size_t i=0;i+3<ps.size();i+=3)if(hitCubic(point,ps[i],ps[i+1],ps[i+2],ps[i+3],t))return true;
            if(item.closed&&segmentDistance(point,ps.back(),ps.front())<=t)return true;
            if(item.filled){
                std::vector<Vec> outline{ps.front()};
                for(size_t i=0;i+3<ps.size();i+=3)for(int step=1;step<=32;++step){
                    double v=step/32.,u=1-v;outline.push_back(ps[i]*(u*u*u)+ps[i+1]*(3*u*u*v)+ps[i+2]*(3*u*v*v)+ps[i+3]*(v*v*v));
                }
                if(inPolygon(point,outline))return true;
            }
        }else if(item.type==Primitive::Line||item.type==Primitive::Polygon){
            for(size_t i=1;i<ps.size();++i)if(segmentDistance(point,ps[i-1],ps[i])<=t)return true;
            if((item.closed||item.type==Primitive::Polygon)&&segmentDistance(point,ps.back(),ps.front())<=t)return true;
            if((item.filled||item.type==Primitive::Polygon)&&inPolygon(point,ps))return true;
        }else if(ps.size()>=2){
            double l=std::min(ps[0].x,ps[1].x),r=std::max(ps[0].x,ps[1].x),top=std::min(ps[0].y,ps[1].y),b=std::max(ps[0].y,ps[1].y);
            if(item.type==Primitive::Ellipse){
                double rx=(r-l)/2,ry=(b-top)/2;
                if(rx<1e-9||ry<1e-9){if(segmentDistance(point,ps[0],ps[1])<=t)return true;continue;}
                double radius=std::hypot((point.x-(l+r)/2)/rx,(point.y-(top+b)/2)/ry);
                if((item.filled&&radius<=1)||std::abs(radius-1)*std::min(rx,ry)<=t)return true;
            }else if(point.x>=l-t&&point.x<=r+t&&point.y>=top-t&&point.y<=b+t){
                if(item.filled||item.type==Primitive::Image||point.x<=l+t||point.x>=r-t||point.y<=top+t||point.y>=b-t)return true;
            }
        }
    }return false;
}
std::vector<ScriptRun> scriptRuns(const std::string& input){
    const auto value=chemicalScripts(input);
    std::vector<ScriptRun> runs;
    auto parse=[&](auto&& self,size_t first,size_t last,int script,bool italic,int depth)->void{
        auto append=[&](size_t a,size_t b){
            if(a<b)runs.push_back({value.substr(a,b-a),script,italic});
        };
        size_t plain=first;
        for(size_t i=first;i+1<last&&depth<24;){
            const char marker=value[i];
            if((marker!='^'&&marker!='_'&&marker!='~')||value[i+1]!='{'){++i;continue;}
            size_t close=i+2;int braces=1;
            for(;close<last;close++){
                if(value[close]=='{')++braces;
                if(value[close]=='}'&&!--braces)break;
            }
            if(close==last)break;
            append(plain,i);
            const int nextScript=marker=='~'?script:marker=='^'?1:-1;
            self(self,i+2,close,nextScript,italic||marker=='~',depth+1);
            i=plain=close+1;
        }
        append(plain,last);
    };
    parse(parse,0,value.size(),0,false,0);
    return runs;
}
static double textWidth(const Measure& m,const std::string& s,double size,const std::string& font="Arial",int face=1){
    double width=0;std::istringstream lines(s);std::string line;while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();double row=0;for(auto& run:scriptRuns(line)){double fs=size*(run.script?(run.script==1?chemicalScriptScale(run.text):.7):1);size_t chars=0;for(unsigned char c:run.text)if((c&0xc0)!=0x80)chars++;row+=m?m(run.text,fs,font,run.italic?face|2:face):chars*fs*(face&1?.62:.56);}width=std::max(width,row);}return width;
}
static void emitText(std::vector<Primitive>& items,Primitive base,Vec at,bool center,const Measure& measure){
    auto value=base.text;std::istringstream rows(value);std::string row;size_t line=0;
    while(std::getline(rows,row)){
        if(!row.empty()&&row.back()=='\r')row.pop_back();double x=at.x-(center?textWidth(measure,row,base.fontSize,base.font,base.face)/2:0),y=at.y+line++*base.fontSize*1.3;
        for(auto& run:scriptRuns(row)){auto t=base;t.text=run.text;if(run.italic)t.face|=2;t.fontSize*=run.script?(run.script==1?chemicalScriptScale(run.text):.7):1;t.points={{x,y+(run.script==1?-base.fontSize*.35:run.script==-1?base.fontSize*.3:0)}};items.push_back(t);x+=textWidth(measure,run.text,t.fontSize,t.font,t.face);}
    }if(value.empty()){base.points={at};items.push_back(base);}
}
// Find a shortest alternate path. Its centroid chooses the inside of a ring.
static std::vector<int> ringPath(const Document& d,const Bond& skip){
    std::queue<int> q;q.push(skip.a);std::map<int,int> prev;prev[skip.a]=-1;
    while(!q.empty()){int a=q.front();q.pop();if(a==skip.b)break;for(auto& b:d.bonds){if(b.id==skip.id)continue;int n=b.a==a?b.b:b.b==a?b.a:0;if(n&&!prev.count(n)){prev[n]=a;q.push(n);}}}
    if(!prev.count(skip.b))return {};std::vector<int> path;for(int n=skip.b;n!=-1;n=prev[n])path.push_back(n);return path;
}
Scene makeScene(const Document& d,const Measure& measure){
    Scene s;std::map<int,Rect> boxes;std::vector<Primitive> labels;
    int owner=0;std::string strokeColor="#000000"; auto line=[&](Vec a,Vec b,double w,int dash=0){Primitive p;p.points={a,b};p.width=w;p.dash=dash;p.owner=owner;p.color=strokeColor;s.items.push_back(p);};
    for(auto& a:d.atoms){
        Style atomStyle=d.style;
        atomStyle.fontSize=a.labelSize.value_or(d.style.fontSize)*a.drawingScale;
        atomStyle.margin=a.labelMargin.value_or(d.style.margin)*a.drawingScale;
        atomStyle.labelFace=a.labelFace.value_or(d.style.labelFace);
        owner=a.id; bool isolated=true;Vec neighbors;for(auto& b:d.bonds)if(b.a==a.id||b.b==a.id){isolated=false;neighbors=neighbors+d.atom(b.a==a.id?b.b:b.a)->p-a.p;}
        if(!a.labelRuns.empty()&&!a.label.empty()){
            Vec cursor=a.p+a.labelOffset;double base=atomStyle.fontSize*.91;double l=cursor.x,t=cursor.y,r=cursor.x,b=cursor.y;
            for(auto& run:a.labelRuns){Primitive p;p.type=Primitive::Text;p.owner=a.id;p.text=run.text;p.font=run.font;p.fontSize=run.size;p.face=run.face;p.color=run.color;int script=run.face&96;double offset=script==32?run.size*.4:script==64?-run.size*.4:0;p.points={{cursor.x,cursor.y+base-run.size*.91+offset}};labels.push_back(p);r=std::max(r,cursor.x+textWidth(measure,run.text,run.size,run.font,run.face));t=std::min(t,p.points[0].y);b=std::max(b,p.points[0].y+run.size);cursor.x+=textWidth(measure,run.text,run.size,run.font,run.face);}
            boxes[a.id]={l-atomStyle.margin,t-atomStyle.margin,r-l+2*atomStyle.margin,b-t+2*atomStyle.margin};continue;
        }
        // A radical annotates the skeletal vertex; only an explicit label shows C.
        if(a.element==6&&a.label.empty()&&!a.isotope&&(a.radical||(!isolated&&!a.charge)))continue;
        std::string el=a.label.empty()?symbol(a.element):a.label;double fs=atomStyle.fontSize,w=textWidth(measure,el,fs,atomStyle.labelFont,atomStyle.labelFace);
        double anchor=w/2;
        if(!a.label.empty()){
            auto formatted=chemicalScripts(el);auto script=formatted.find("^{");auto sym=symbol(a.element);auto pos=formatted.find(sym);
            if(script!=std::string::npos&&pos!=std::string::npos&&pos<script)anchor=textWidth(measure,formatted.substr(0,pos),fs,atomStyle.labelFont,atomStyle.labelFace)+textWidth(measure,sym,fs,atomStyle.labelFont,atomStyle.labelFace)/2;
        }
        double x=a.p.x-anchor,y=a.p.y-fs/2;boxes[a.id]={x-atomStyle.margin,y-atomStyle.margin,w+2*atomStyle.margin,fs+2*atomStyle.margin};
        Primitive t;t.owner=a.id;t.type=Primitive::Text;t.points={{x,y}};t.fontSize=fs;t.font=atomStyle.labelFont;t.face=atomStyle.labelFace;t.color=a.color;t.text=el;emitText(labels,t,{x,y},false,measure);
        if(chemicalScripts(el).find("^{")!=std::string::npos){boxes[a.id].y-=fs*.35;boxes[a.id].h+=fs*.35;}
        // Only neutral common heteroatoms get automatic H labels in v0.1.
        int h=a.label.empty()&&(a.element==7||a.element==8||a.element==16)?d.hydrogens(a.id):0;
        if(h){double hw=textWidth(measure,"H",fs),nw=h>1?textWidth(measure,std::to_string(h),fs*.7):0;
            bool leftBond=false,rightBond=false;for(auto& bond:d.bonds)if(bond.a==a.id||bond.b==a.id){Vec v=d.atom(bond.a==a.id?bond.b:bond.a)->p-a.p;if(v.x>v.length()*.3)rightBond=true;if(v.x<-v.length()*.3)leftBond=true;}
            bool vertical=leftBond&&rightBond;double hx=vertical?a.p.x-(hw+nw)/2:neighbors.x>1?x-hw-nw:x+w;double hy=vertical?y+(neighbors.y>0?-fs-1:fs+1):y;Primitive ht=t;ht.text="H";ht.points={{hx,hy}};labels.push_back(ht);
            if(h>1){ht.text=std::to_string(h);ht.fontSize=fs*.7;ht.points={{hx+hw,hy+fs*.4}};labels.push_back(ht);}
            Rect& box=boxes[a.id];double left=std::min(box.x,hx-atomStyle.margin),right=std::max(box.x+box.w,hx+hw+nw+atomStyle.margin);box.x=left;box.w=right-left;if(vertical){double top=std::min(box.y,hy-atomStyle.margin),bottom=std::max(box.y+box.h,hy+fs+atomStyle.margin);box.y=top;box.h=bottom-top;}
        }
        if(a.label.empty()&&a.isotope){Primitive isotope=t;isotope.fontSize=fs*.65;isotope.text=std::to_string(a.isotope);double iw=textWidth(measure,isotope.text,isotope.fontSize);isotope.points={{x-iw,y-fs*.3}};labels.push_back(isotope);boxes[a.id].x-=iw;boxes[a.id].w+=iw;}
    }
    // Align the visible label to the fixed skeletal vertex before clipping bonds.
    // Apply to both generated labels and imported rich text without changing their runs.
    for(const auto& a:d.atoms){
        if(!a.labelAlignment&&a.labelAdjustment.length()==0)continue;
        double left=1e30,right=-1e30;
        for(const auto& t:labels)if(t.owner==a.id){
            left=std::min(left,t.points[0].x);
            right=std::max(right,t.points[0].x+textWidth(measure,t.text,t.fontSize,t.font,t.face));
        }
        if(right<left)continue;
        Vec shift=a.labelAdjustment;
        if(a.labelAlignment)shift.x+=a.p.x-left-(right-left)*(*a.labelAlignment);
        for(auto& t:labels)if(t.owner==a.id)t.points[0]=t.points[0]+shift;
        boxes[a.id].x+=shift.x;boxes[a.id].y+=shift.y;
    }
    // Place charges and radical dots independently of custom or imported atom labels.
    for(const auto& a:d.atoms)if((a.charge&&!atomLabelHasCharge(a))||a.radical){
        double fs=a.labelSize.value_or(d.style.fontSize)*a.drawingScale;
        auto box=boxes.count(a.id)?boxes.at(a.id):Rect{a.p.x,a.p.y,0,0};
        Primitive mark;mark.type=Primitive::Text;mark.owner=a.id;mark.textRole=13;mark.fontSize=fs*.7;mark.font=d.style.labelFont;mark.face=a.labelFace.value_or(d.style.labelFace);mark.color=a.color;
        int charge=atomLabelHasCharge(a)?0:a.charge;
        mark.text=(std::abs(charge)>1?std::to_string(std::abs(charge)):"")+(charge>0?"+":charge<0?"\xe2\x88\x92":"")+(a.radical==1?"\xe2\x80\xa2\xe2\x80\xa2":a.radical==3?"\xe2\x80\xa2\xe2\x80\xa2":a.radical?"\xe2\x80\xa2":"");
        double width=textWidth(measure,mark.text,mark.fontSize,mark.font,mark.face),height=mark.fontSize;
        Vec best=a.p+a.annotationOffset;
        if(!a.annotationManual){
            std::vector<Vec> candidates={{box.x+box.w,box.y-height*.65},{box.x-width,box.y-height*.65},{box.x+box.w,box.y+box.h-height*.25},{box.x-width,box.y+box.h-height*.25},{a.p.x-width/2,box.y-height-2},{a.p.x-width/2,box.y+box.h+2}};
            double bestScore=1e30;
            for(size_t i=0;i<candidates.size();i++){
                Vec pos=candidates[i];double score=double(i)*.1;
                Rect area{pos.x-2,pos.y-2,width+4,height+4};
                auto inside=[&](Vec p){return p.x>=area.x&&p.x<=area.x+area.w&&p.y>=area.y&&p.y<=area.y+area.h;};
                for(auto& b:d.bonds){Vec from=d.atom(b.a)->p,to=d.atom(b.b)->p;int steps=std::max(2,int((to-from).length()/1.5));for(int j=0;j<=steps;j++)if(inside(from+(to-from)*(double(j)/steps)))score+=20;}
                for(auto& label:labels){Vec p=label.points[0];double w=textWidth(measure,label.text,label.fontSize,label.font,label.face);if(p.x<area.x+area.w&&p.x+w>area.x&&p.y<area.y+area.h&&p.y+label.fontSize>area.y)score+=1000;}
                if(score<bestScore){bestScore=score;best=pos;}
            }
        }
        mark.points={best};labels.push_back(mark);
    }
    for(const auto& a:d.atoms)if(a.mullikenSpin){
        Primitive mark;mark.type=Primitive::Text;mark.owner=a.id;mark.textRole=mullikenSpinTextRole;
        mark.fontSize=std::max(6.,a.labelSize.value_or(d.style.fontSize)*.70)*a.drawingScale;
        mark.color=a.color;
        std::ostringstream value;
        value.imbue(std::locale::classic());
        const double rounded=std::round(*a.mullikenSpin*1000)/1000;
        value<<std::showpos<<std::fixed<<std::setprecision(3)<<(rounded==0?0.:rounded);
        mark.text=value.str();
        double y=a.p.y-3;
        for(const auto& label:labels)if(label.owner==a.id)y=std::min(y,label.points.front().y-2);
        const double width=textWidth(measure,mark.text,mark.fontSize,mark.font,mark.face);
        mark.points={{a.p.x-width/2,y-mark.fontSize}};
        labels.push_back(std::move(mark));
    }
    // Clip against ink spans, not the empty space underneath oxidation superscripts.
    std::map<int,std::vector<Rect>> inkBoxes;
    for(auto& t:labels)if(t.type==Primitive::Text&&!t.points.empty()&&t.textRole!=13&&t.textRole!=mullikenSpinTextRole){const auto& atom=*d.atom(t.owner);double m=atom.labelMargin.value_or(d.style.margin)*atom.drawingScale;inkBoxes[t.owner].push_back({t.points[0].x-m,t.points[0].y-m,textWidth(measure,t.text,t.fontSize,t.font,t.face)+2*m,t.fontSize+2*m});}
    auto clip=[&](int id,Vec from,Vec to){
        Vec v=to-from;double end=0;
        for(auto& r:inkBoxes[id]){
            double lo=0,hi=1;auto slab=[&](double p,double delta,double min,double max){if(std::abs(delta)<1e-9)return p>=min&&p<=max;double a=(min-p)/delta,b=(max-p)/delta;if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);return lo<=hi;};
            if(slab(from.x,v.x,r.x,r.x+r.w)&&slab(from.y,v.y,r.y,r.y+r.h))end=std::max(end,hi);
        }
        return from+v*std::clamp(end,0.,1.);
    };
    std::vector<const Bond*> orderedBonds;for(auto& b:d.bonds)orderedBonds.push_back(&b);std::stable_sort(orderedBonds.begin(),orderedBonds.end(),[](const Bond* a,const Bond* b){return (a->color!="#000000")>(b->color!="#000000");});
    for(const auto* bond:orderedBonds){const auto& b=*bond;
        Style bondStyle=d.style;
        bondStyle.lineWidth=b.lineWidth.value_or(d.style.lineWidth)*b.drawingScale;
        bondStyle.boldWidth=b.boldWidth.value_or(d.style.boldWidth)*b.drawingScale;
        bondStyle.hashSpacing=b.hashSpacing.value_or(d.style.hashSpacing)*b.drawingScale;
        owner=b.id;strokeColor=b.color;double bondWidth=b.customDisplay=="Bold"?bondStyle.boldWidth:bondStyle.lineWidth; Vec origA=d.atom(b.a)->p,origB=d.atom(b.b)->p,a=clip(b.a,origA,origB),z=clip(b.b,origB,origA),v=z-a;double len=v.length();
        if(len<.2 || (a-origA).length()+(z-origB).length()>=(origB-origA).length())continue;
        Vec u=v*(1/len),n={-u.y,u.x};double gap=b.doubleGap.value_or(d.style.bondLength*d.style.bondSpacing)*b.drawingScale;
        // BoldWidth is the full width, also at the broad end of a wedge.
        // Keep short, label-clipped coordination bonds from becoming squat triangles.
        double wedgeWidth=std::min(bondStyle.boldWidth,len*.35);
        if(b.kind==BondKind::Wedge){Primitive p;p.owner=owner;p.color=b.color;p.type=Primitive::Polygon;p.points={a,z+n*(wedgeWidth/2),z-n*(wedgeWidth/2)};s.items.push_back(p);}
        else if(b.kind==BondKind::Hash){
            double ink=std::min(bondStyle.lineWidth*.6,len/6);
            int count=std::max(2,int(len/std::max(bondStyle.hashSpacing,ink*2.2)));
            for(int i=0;i<=count;i++){double t=double(i)/count;Vec c=a+v*((ink/2+(len-ink)*t)/len);double w=std::max(ink*.4,wedgeWidth*t/2);line(c+n*w,c-n*w,ink);s.items.back().flatStart=s.items.back().flatEnd=true;}}
        else if(b.kind==BondKind::Double){auto path=ringPath(d,b);
            if(!path.empty()&&b.doubleSide==0){Vec center;for(int id:path)center=center+d.atom(id)->p;center=center*(1./path.size());double side=((center-a).x*n.x+(center-a).y*n.y)>0?1:-1;
                line(a,z,bondWidth);double inset=std::min(len*.25,gap*.65+std::max(0.,bondWidth-bondStyle.lineWidth)/2);line(a+u*inset+n*(side*gap),z-u*inset+n*(side*gap),bondStyle.lineWidth);}
            else if(b.doubleSide){line(a,z,bondWidth);line(a+n*(gap*b.doubleSide),z+n*(gap*b.doubleSide),bondStyle.lineWidth);}else{line(a+n*(gap/2),z+n*(gap/2),bondWidth);line(a-n*(gap/2),z-n*(gap/2),bondStyle.lineWidth);}}
        else if(b.kind==BondKind::Triple){line(a,z,bondWidth);line(a+n*gap,z+n*gap,bondWidth);line(a-n*gap,z-n*gap,bondWidth);}
        else if(b.kind==BondKind::Wavy){Vec last=a;for(int i=1;i<=24;i++){double t=double(i)/24;Vec q=a+v*t+n*(std::sin(t*pi*8)*.8*b.drawingScale);line(last,q,bondWidth);last=q;}}
        else if(b.kind==BondKind::Dative){line(a,z,bondWidth);line(z,z-u*(3*b.drawingScale)+n*(1.4*b.drawingScale),bondWidth);line(z,z-u*(3*b.drawingScale)-n*(1.4*b.drawingScale),bondWidth);}
        else if(b.kind==BondKind::Aromatic){
            // The solid stroke stays on the skeleton; put the partial second
            // stroke inside the ring or delocalized fragment, independent of
            // the stored direction of each bond.
            double side=b.doubleSide;auto path=ringPath(d,b);
            if(!side){Vec center;std::vector<int> ids=path;
                if(ids.empty()){ids={b.a,b.b};for(size_t i=0;i<ids.size();i++)for(auto& edge:d.bonds)if(edge.kind==BondKind::Aromatic){int next=edge.a==ids[i]?edge.b:edge.b==ids[i]?edge.a:0;if(next&&std::find(ids.begin(),ids.end(),next)==ids.end())ids.push_back(next);}}
                for(int id:ids)center=center+d.atom(id)->p;center=center*(1./ids.size());double dot=(center-origA).x*n.x+(center-origA).y*n.y;
                side=std::abs(dot)>.001?(dot>0?1:-1):(origA.x<origB.x||(origA.x==origB.x&&origA.y<origB.y)?1:-1);
            }
            line(a,z,bondWidth);double inset=std::min(len*.25,gap*.8);line(a+u*inset+n*(side*gap),z-u*inset+n*(side*gap),bondWidth,1);
        }
        else line(a,z,b.kind==BondKind::Bold?bondStyle.boldWidth:bondWidth,b.kind==BondKind::Dash?1:b.kind==BondKind::Dot?2:0);
    }
    strokeColor="#000000";s.items.insert(s.items.end(),labels.begin(),labels.end());
    int arrowIndex=-1;std::vector<int> hiddenArrowIndices;
    auto arrow=[&](Vec a,Vec z,const std::string& color,double width,int head,double headScale=1.,double headWidth=0.){
        if(arrowIndex>=0&&std::find(hiddenArrowIndices.begin(),hiddenArrowIndices.end(),arrowIndex)!=hiddenArrowIndices.end())return;
        const Vec v=z-a;const double len=v.length();if(len<1e-6)return;
        headScale=std::min(headScale,len/8);
        const Vec u=v*(1/len),n{-u.y,u.x};
        Primitive p;p.reactionArrow=arrowIndex;p.owner=owner;p.points={a,head?z-u*(4.8*headScale):z};
        p.width=width;p.color=color;s.items.push_back(p);
        if(head){
            const Vec base=z-u*(5*headScale),side=n*(headWidth>0?headWidth/2:2*headScale);
            p.type=Primitive::Polygon;p.points={z,base+side};
            if(head!=2)p.points.push_back(base-side);
            s.items.push_back(p);
        }
    };
    auto addText=[&](std::string value,Vec p,double size=9,std::string color="#000000",bool center=false,int role=0,int index=-1){Primitive t;t.type=Primitive::Text;t.owner=owner;t.textRole=role;t.textIndex=index;t.text=value;t.fontSize=size*d.style.fontSize/10.;t.face=d.style.labelFace;t.color=color;t.font=d.style.labelFont;if(t.font=="Arial")for(unsigned char c:value)if(c>=0xe3)t.font="Malgun Gothic";emitText(s.items,t,p,center,measure);};
    for(auto& o:d.objects){owner=o.id;Primitive p;p.owner=o.id;p.points=o.points;p.width=o.width;p.color=o.color;p.filled=o.filled;p.fontSize=o.fontSize;p.font=o.font;p.dash=o.dash;p.face=o.face;p.closed=o.closed;p.text=o.text;
        if(o.points.empty())continue;
        if(o.kind==ObjectKind::Line&&!o.head&&o.autoRoute&&o.points.size()==2){
            auto points=routeArrow(d,o);
            const auto a=clip(o.sourceAtom,points.front(),points.back());
            const auto b=clip(o.targetAtom,points.back(),points.front());
            if((a-points.front()).length()+(b-points.back()).length()>=(points.back()-points.front()).length())continue;
            p.points={a,b};
        }
        if(o.head&&(o.kind==ObjectKind::Line||o.kind==ObjectKind::Curve)){Object routed=o;routed.points=routeArrow(d,o,boxes);s.arrowPaths[o.id]=routed.points;auto pieces=arrowPrimitives(routed);s.items.insert(s.items.end(),pieces.begin(),pieces.end());appendArrowCross(s.items,pieces,o.cross,o.id);continue;}
        switch(o.kind){case ObjectKind::Line:p.type=Primitive::Line;break;case ObjectKind::Curve:p.type=Primitive::Bezier;break;case ObjectKind::Text:p.type=Primitive::Text;break;case ObjectKind::Ellipse:p.type=Primitive::Ellipse;break;case ObjectKind::Rectangle:p.type=Primitive::Box;break;case ObjectKind::Image:p.type=Primitive::Image;p.imageHex=o.imageHex;p.mime=o.mime;break;}
        if(o.kind==ObjectKind::Text&&!o.runs.empty()){Vec cursor=o.points[0];double baseline=cursor.y+o.fontSize*.91;for(auto& run:o.runs){Primitive t=p;t.text=run.text;t.fontSize=run.size;t.color=run.color;t.font=run.font;t.face=run.face;int script=run.face&96;double offset=script==32?run.size*.45:script==64?-run.size*.4:0;t.points={{cursor.x,baseline-run.size*.91+offset}};s.items.push_back(t);cursor.x+=textWidth(measure,run.text,run.size,run.font,run.face);}}
        else if(o.kind==ObjectKind::Text){emitText(s.items,p,o.points[0],o.centerText,measure);}
        else s.items.push_back(p);
        if(o.head&&o.points.size()>=2){Vec z=o.points.back(),a=o.points[o.points.size()-2];Vec v=z-a;double len=v.length();if(len>.001){Vec u=v*(1/len),n{-u.y,u.x};Primitive head;head.owner=o.id;head.type=Primitive::Polygon;head.color=o.color;head.points=o.head==2?std::vector<Vec>{z,z-u*5+n*2}:std::vector<Vec>{z,z-u*5+n*2,z-u*5-n*2};s.items.push_back(head);}}
    }
    auto spinColor=[](double spin){return spin==0?"#000000":spin==1?"#ff0000":spin==2?"#0000ff":"#555555";};
    auto addEnergy=[&](const std::string& value,Vec at,int role,int index,double available){
        std::vector<SpinEnergy> energies;std::vector<std::string> parts;
        try{energies=reactionEnergies(value);}catch(...){addText(value,at,8,"#000000",true,role,index);return;}
        for(auto& e:energies){std::ostringstream label;if(e.spin!=0&&e.spin!=1&&e.spin!=2)label<<"S="<<e.spin<<": ";label<<e.value;parts.push_back(e.value.empty()?"":label.str());}
        size_t count=parts.size();while(count&&parts[count-1].empty())count--;
        if(!count){addText("",at,8,"#000000",true,role,index);return;}
        double size=8;for(auto& reaction:d.reactions)if(reaction.id==owner)size=mechanismFontSize(reaction,d.style)*10/d.style.fontSize;
        double total=0;auto width=[&](const std::string& text){return textWidth(measure,text,size*d.style.fontSize/10.,d.style.labelFont,d.style.labelFace);};
        auto gap=[&](){return size*d.style.fontSize/10.*.4;};
        for(size_t i=0;i<count;i++)total+=width(parts[i])+(i?width("/")+2*gap():0);
        if(total>available&&available>0){size*=available/total;total=available;}
        double cursor=at.x-total/2;
        for(size_t i=0;i<count;i++){
            if(i){cursor+=gap();addText("/",{cursor,at.y},size,"#000000",false,role,index);cursor+=width("/")+gap();}
            if(!parts[i].empty()){addText(parts[i],{cursor,at.y},size,spinColor(energies[i].spin),false,role,index);cursor+=width(parts[i]);}
        }
    };
    auto transitionLabels=[&](const Reaction& reaction,size_t i,Vec mid,double aboveY,double belowY){
        if(std::find(reaction.hiddenArrows.begin(),reaction.hiddenArrows.end(),int(i))!=reaction.hiddenArrows.end())return;
        auto caption=transitionCaption(i<reaction.above.size()?reaction.above[i]:"");
        double font=mechanismFontSize(reaction,d.style),gap=reaction.spacing-reaction.slotWidth-2*mechanismArrowMargin(reaction,d.style);
        auto description=caption.description;
        if(i<reaction.above.size())addText(description,mid+Vec{0,aboveY-font*1.3*std::count(description.begin(),description.end(),'\n')},font*10/d.style.fontSize,"#000000",true,2,int(i));
        if(!caption.name.empty())addText(caption.name,mid+Vec{0,belowY},font*10/d.style.fontSize,"#000000",true,11,int(i));
        if(i<reaction.below.size())addEnergy(reaction.below[i],mid+Vec{0,belowY+(caption.name.empty()?0:font*1.5*(1+std::count(caption.name.begin(),caption.name.end(),'\n')))},3,int(i),gap);
    };
    for(auto& r:d.reactions){hiddenArrowIndices=r.hiddenArrows;owner=r.id;double font=mechanismFontSize(r,d.style),factor=font/d.style.fontSize;
        bool branched=false;
        std::map<int,int> incoming,outgoing;
        for(const auto& link:r.links){
            if(++incoming[link.to]>1)branched=true;
            if(link.from>=0&&++outgoing[link.from]>1)branched=true;
        }
        if(r.cyclic||!r.cycleStages.empty())addText(r.centerLabel,r.p+Vec{0,-font*.65},10*factor,"#000000",true,10);
        if(!branched&&!r.cyclic&&!r.unit.empty()&&!r.slots.empty()){double right=-1e20,top=1e20;for(size_t j=0;j<r.slots.size();j++){Vec pos=reactionSlot(r,j);right=std::max(right,pos.x);top=std::min(top,pos.y);}addText("("+r.unit+")",{right+r.slotWidth/2,top-r.slotHeight/2-font*2},10*factor,"#000000",true);}
        for(size_t i=0;i<r.slots.size();i++){arrowIndex=int(i);Vec c=reactionSlot(r,i);addText(r.slots[i].label,c+Vec{0,r.slotHeight/2+font*.6},10*factor,"#000000",true,1,int(i));
            if(r.cyclic||!r.slots[i].energy.empty())addEnergy(r.slots[i].energy,c+Vec{0,r.slotHeight/2+font*.6+font*1.5*(1+std::count(r.slots[i].label.begin(),r.slots[i].label.end(),'\n'))},9,int(i),r.spacing-20);
            if(r.cyclic){
                bool hidden=std::find(r.hiddenArrows.begin(),r.hiddenArrows.end(),int(i))!=r.hiddenArrows.end();
                double radius=(c-r.p).length(),step=2*pi/r.slots.size(),start=-pi/2+step*i,trim=std::asin(std::min(.95,(mechanismCycleNodeRadius(r,d.style)+font*.5)/radius));
                auto arc=[&](double rad,double begin,double end){double bend=4./3*std::tan((end-begin)/4);Vec a=r.p+Vec{std::cos(begin),std::sin(begin)}*rad,z=r.p+Vec{std::cos(end),std::sin(end)}*rad,t0{-std::sin(begin),std::cos(begin)},t1{-std::sin(end),std::cos(end)};double direction=end>begin?1:-1;Vec u=t1*direction,n{-u.y,u.x};Primitive curve;curve.reactionArrow=int(i);curve.owner=owner;curve.type=Primitive::Bezier;curve.width=mechanismArrowWeight(r,d.style);double headSize=5*mechanismArrowHead(r,d.style);curve.points={a,a+t0*(bend*rad),z-t1*(bend*rad),z-u*(headSize*.96)};s.items.push_back(curve);Primitive head;head.reactionArrow=int(i);head.owner=owner;head.type=Primitive::Polygon;head.points={z,z-u*headSize+n*(headSize*.4),z-u*headSize-n*(headSize*.4)};s.items.push_back(head);};
                if(!hidden){arc(radius-(mechanismArrowType(r,size_t(arrowIndex))==1?2:0),start+trim,start+step-trim);if(mechanismArrowType(r,size_t(arrowIndex))==1)arc(radius+2,start+step-trim,start+trim);}
                Vec mid=mechanismCycleTransitionPosition(r,i,d.style,radius);
                transitionLabels(r,i,mid,-font*1.7,font*.65);continue;
            }
            if(r.explicitConnections())continue;
            if(i+1<r.slots.size()){Vec next=reactionSlot(r,(i+1)%r.slots.size()),v=next-c;double len=v.length();if(len<1)continue;Vec u=v*(1/len);
                if(r.vertical){
                    double margin=mechanismArrowMargin(r,d.style),weight=mechanismArrowWeight(r,d.style),head=mechanismArrowHead(r,d.style);
                    Vec a=c+Vec{0,r.slotHeight/2+mechanismCaptionHeight(r,d.style)+margin},b=next-Vec{0,r.slotHeight/2+margin};
                    if(mechanismArrowType(r,size_t(arrowIndex))==1){arrow(a+Vec{-2,0},b+Vec{-2,0},"#000000",weight,1,head);arrow(b+Vec{2,0},a+Vec{2,0},"#000000",weight,1,head);}else arrow(a,b,"#000000",weight,1,head);
                    auto caption=transitionCaption(i<r.above.size()?r.above[i]:"");Vec mid=(a+b)*.5;
                    double w=textWidth(measure,caption.description,font);
                    addText(caption.description,mid-Vec{font+w/2,font*.65*(1+std::count(caption.description.begin(),caption.description.end(),'\n'))},10*factor,"#000000",true,2,int(i));
                    double right=mechanismArrowLength(r,d.style)/2+font;
                    if(!caption.name.empty())addText(caption.name,mid+Vec{right,-font*1.5},10*factor,"#000000",true,11,int(i));
                    if(i<r.below.size())addEnergy(r.below[i],mid+Vec{right,caption.name.empty()?-font*.65:font*1.5*std::count(caption.name.begin(),caption.name.end(),'\n')},3,int(i),mechanismArrowLength(r,d.style));
                    continue;
                }
                if(reactionRow(r,i)!=reactionRow(r,i+1)){
                    // Connect row ends around the outside of the structure and its captions.
                    double direction=reactionRow(r,i)%2?-1.:1.;
                    double margin=mechanismArrowMargin(r,d.style),edge=r.slotWidth/2+margin;
                    double outside=c.x+direction*(edge+font*2),weight=mechanismArrowWeight(r,d.style),head=mechanismArrowHead(r,d.style);
                    Vec a=c+Vec{direction*edge,0},b=next+Vec{direction*edge,0};
                    auto bent=[&](Vec from,Vec to,double x,double dy){from.y+=dy;to.y+=dy;Vec p{x,from.y},q{x,to.y};arrow(from,p,"#000000",weight,0);arrow(p,q,"#000000",weight,0);arrow(q,to,"#000000",weight,1,head);};
                    if(mechanismArrowType(r,size_t(arrowIndex))==1){bent(a,b,outside+direction*2,-2);bent(b,a,outside-direction*2,2);}else bent(a,b,outside,0);
                    auto caption=transitionCaption(i<r.above.size()?r.above[i]:"");
                    double captionWidth=std::max(textWidth(measure,caption.description,font),textWidth(measure,caption.name,font));
                    if(i<r.below.size())captionWidth=std::max(captionWidth,textWidth(measure,r.below[i],font));
                    Vec mid{outside+direction*(captionWidth/2+font),(c.y+next.y)/2};
                    transitionLabels(r,i,mid,-font*1.7,font*.65);continue;
                }
                double trim=std::min(len*.49,r.slotWidth/2+mechanismArrowMargin(r,d.style));Vec a=c+u*trim,b=next-u*trim,n{-u.y,u.x};
                double weight=mechanismArrowWeight(r,d.style),head=mechanismArrowHead(r,d.style);
                if(mechanismArrowType(r,size_t(arrowIndex))==1){arrow(a-n*2,b-n*2,"#000000",weight,1,head);arrow(b+n*2,a+n*2,"#000000",weight,1,head);}else arrow(a,b,"#000000",weight,1,head);
                Vec mid=(a+b)*.5;transitionLabels(r,i,mid,-font*1.7,font*.65);
            }
        }
        // External branches have no invented reactant frame: one shared vertical
        // spine joins their horizontal arrow tails.
        bool external=false;double stemX=0,top=1e30,bottom=-1e30;
        for(size_t e=0;e<r.links.size();e++)if(r.links[e].from<0){auto path=mechanismLinkPath(r,e,d.style);external=true;stemX=path[0].x;top=std::min(top,path[0].y);bottom=std::max(bottom,path[0].y);}
        if(external){arrowIndex=-1;arrow({stemX,top},{stemX,bottom},"#000000",mechanismArrowWeight(r,d.style),0);}
        for(size_t e=0;e<r.links.size();e++){
            arrowIndex=int(e);auto path=mechanismLinkPath(r,e,d.style);double weight=mechanismArrowWeight(r,d.style),head=mechanismArrowHead(r,d.style);
            if(auto arc=mechanismLinkArc(r,e,d.style)){
                const double captionRadius=arc->radius+(mechanismArrowType(r,size_t(arrowIndex))==1?2:0);
                auto drawArc=[&](MechanismArc shape){
                    const double sweep=shape.end-shape.start,direction=std::copysign(1.,sweep);
                    const double size=std::min(5*head,std::abs(sweep)*shape.radius*.35);
                    const Vec tip=shape.point(shape.end);
                    auto tangent=[](double angle){return Vec{-std::sin(angle),std::cos(angle)};};
                    const Vec u=tangent(shape.end)*direction,n{-u.y,u.x};
                    const double shaftEnd=shape.end-direction*size*.96/shape.radius;
                    const int segments=std::max(1,int(std::ceil(std::abs(shaftEnd-shape.start)/(pi/2))));
                    const double step=(shaftEnd-shape.start)/segments;
                    Primitive shaft;
                    shaft.owner=owner;shaft.reactionArrow=arrowIndex;shaft.type=Primitive::Bezier;shaft.width=weight;
                    shaft.points.push_back(shape.point(shape.start));
                    for(int i=0;i<segments;i++){
                        const double a=shape.start+i*step,b=a+step;
                        const double control=(4./3)*std::tan(step/4)*shape.radius;
                        const Vec end=shape.point(b);
                        shaft.points.push_back(shape.point(a)+tangent(a)*control);
                        shaft.points.push_back(end-tangent(b)*control);
                        shaft.points.push_back(end);
                    }
                    s.items.push_back(shaft);
                    Primitive arrowhead;
                    arrowhead.owner=owner;arrowhead.reactionArrow=arrowIndex;arrowhead.type=Primitive::Polygon;
                    arrowhead.points={tip,tip-u*size+n*(size*.4),tip-u*size-n*(size*.4)};
                    s.items.push_back(arrowhead);
                };
                if(std::find(r.hiddenArrows.begin(),r.hiddenArrows.end(),int(e))==r.hiddenArrows.end()){
                    if(mechanismArrowType(r,size_t(arrowIndex))==1){auto reverse=*arc;reverse.radius+=2;std::swap(reverse.start,reverse.end);arc->radius-=2;drawArc(*arc);drawArc(reverse);}
                    else drawArc(*arc);
                }
                const size_t firstLabel=s.items.size();
                transitionLabels(r,e,mechanismLinkCaption(r,e,d.style),-font*1.7,font*.65);
                const double angle=(arc->start+arc->end)/2;
                const Vec outward{std::cos(angle),std::sin(angle)};
                const double clearance=captionRadius+weight/2+font*.65;
                double shift=0;
                // Keep every rendered text run outside the tangent at the caption.
                for(size_t i=firstLabel;i<s.items.size();i++){
                    const auto& label=s.items[i];
                    if(label.text.empty())continue;
                    Vec near=label.points.front()-arc->center;
                    if(outward.x<0)near.x+=textWidth(measure,label.text,label.fontSize,label.font,label.face);
                    if(outward.y<0)near.y+=label.fontSize*1.3;
                    shift=std::max(shift,clearance-(near.x*outward.x+near.y*outward.y));
                }
                for(size_t i=firstLabel;i<s.items.size();i++)
                    s.items[i].points.front()=s.items[i].points.front()+outward*shift;
                continue;
            }
            auto draw=[&](const std::vector<Vec>& points,double offset){
                const Vec delta{offset,offset};
                if(points.size()>5){
                    if(std::find(r.hiddenArrows.begin(),r.hiddenArrows.end(),int(e))!=r.hiddenArrows.end())return;
                    // The clipped quadratic is determined by its ends and its sampled midpoint.
                    Vec a=points.front(),b=points.back(),control=points[points.size()/2]*2-(a+b)*.5;
                    Vec tangent=b-control;
                    double length=tangent.length();
                    if(length<1e-6)return;
                    Vec u=tangent*(1/length),normal{-u.y,u.x};
                    double size=std::min(5*head,length*.5);
                    Primitive shaft;
                    shaft.owner=owner;shaft.reactionArrow=arrowIndex;shaft.type=Primitive::Bezier;shaft.width=weight;
                    shaft.points={a+delta,a+(control-a)*(2./3)+delta,b+(control-b)*(2./3)+delta,b-u*(size*.96)+delta};
                    s.items.push_back(shaft);
                    Primitive tip;
                    tip.owner=owner;tip.reactionArrow=arrowIndex;tip.type=Primitive::Polygon;
                    tip.points={b+delta,b-u*size+normal*(size*.4)+delta,b-u*size-normal*(size*.4)+delta};
                    s.items.push_back(tip);
                    return;
                }
                for(size_t j=1;j<points.size();j++)arrow(points[j-1]+delta,points[j]+delta,"#000000",weight,j+1==points.size()?1:0,head);
            };
            draw(path,mechanismArrowType(r,size_t(arrowIndex))==1?-2:0);if(mechanismArrowType(r,size_t(arrowIndex))==1){std::reverse(path.begin(),path.end());draw(path,2);}
            transitionLabels(r,e,mechanismLinkCaption(r,e,d.style),-font*1.7,font*.65);
        }
        arrowIndex=-1;
        for(auto& mark:r.crosses){std::vector<Primitive> shaft;for(auto& p:s.items)if(p.owner==r.id&&p.reactionArrow==mark.index)shaft.push_back(p);if(mechanismArrowType(r,size_t(mark.index))==1){auto firstHead=std::find_if(shaft.begin(),shaft.end(),[](const Primitive& p){return p.type==Primitive::Polygon;});if(firstHead!=shaft.end())shaft.erase(firstHead+1,shaft.end());}appendArrowCross(s.items,shaft,mark,r.id);}
        if(branched&&!r.cyclic&&!r.slots.empty()){
            std::set<int> members{r.id};
            std::set<double> spins;
            auto collectSpins=[&](const std::string& value){
                try{
                    for(const auto& entry:reactionEnergies(value))
                        if(!entry.value.empty())spins.insert(entry.spin);
                }catch(const std::exception&){}
            };
            double right=-1e20,top=1e20;
            for(size_t i=0;i<r.slots.size();++i){
                const auto pos=reactionSlot(r,i);
                right=std::max(right,pos.x+r.slotWidth/2);
                top=std::min(top,pos.y-r.slotHeight/2);
                const auto& slot=r.slots[i];
                members.insert(slot.members.begin(),slot.members.end());
                collectSpins(slot.energy);
            }
            for(size_t i=0;i<r.links.size();++i){
                const auto& link=r.links[i];
                members.insert(link.members.begin(),link.members.end());
                if(i<r.below.size())collectSpins(r.below[i]);
            }
            for(const auto& item:s.items)if(members.count(item.owner)){
                const bool text=item.type==Primitive::Text;
                if(text&&item.text.empty())continue;
                const double pad=text?0:item.width/2;
                const double width=text?textWidth(measure,item.text,item.fontSize,item.font,item.face):pad;
                for(auto p:item.points){
                    right=std::max(right,p.x+width);
                    top=std::min(top,p.y-pad);
                }
            }
            auto legendWidth=[&](const std::string& text){
                return textWidth(measure,text,font,d.style.labelFont,d.style.labelFace);
            };
            double y=top-font*3;
            if(!r.unit.empty()&&!spins.empty())y-=font*1.5;
            if(!r.unit.empty()){
                const std::string unit="Energy ("+r.unit+")";
                addText(unit,{right-legendWidth(unit),y},10*factor);
                y+=font*1.5;
            }
            if(!spins.empty()){
                const std::string title="Multiplicity";
                double width=legendWidth(title);
                std::vector<std::pair<std::string,double>> entries;
                for(double spin:spins){
                    std::string label="M = "+std::to_string(std::lround(2*spin+1));
                    if(spin!=0&&spin!=1&&spin!=2){
                        std::ostringstream value;value<<" (S = "<<spin<<")";label+=value.str();
                    }
                    width+=font+legendWidth(label);
                    entries.emplace_back(std::move(label),spin);
                }
                double x=right-width;
                addText(title,{x,y},10*factor);
                x+=legendWidth(title)+font;
                for(const auto& [label,spin]:entries){
                    addText(label,{x,y},10*factor,spinColor(spin));
                    x+=legendWidth(label)+font;
                }
            }
        }
        if(r.cyclic&&!r.unit.empty()&&!r.slots.empty()){
            double right=r.p.x,top=r.p.y;
            for(auto& item:s.items)if(item.owner==r.id&&item.type==Primitive::Text&&!item.points.empty()){right=std::max(right,item.points[0].x+textWidth(measure,item.text,item.fontSize,item.font,item.face));top=std::min(top,item.points[0].y);}
            addText("("+r.unit+")",{right+font,top-font*2},10*factor,"#000000",false);
        }
    }
    for(auto& plot:d.plots){
        // Use an en dash in inverse-mole exponents, including older saved graphs.
        std::string displayUnit=plot.unit;
        for(const std::string from:{"mol^{-1}","mol^{−1}","mol⁻¹","mol-1","mol–1"}){
            for(size_t at=0;(at=displayUnit.find(from,at))!=std::string::npos;){
                const auto end=at+from.size();
                if(end<displayUnit.size()&&((displayUnit[end]>='0'&&displayUnit[end]<='9')||(static_cast<unsigned char>(displayUnit[end])>=0x80))){at=end;continue;}
                const std::string to="mol^{–1}";
                displayUnit.replace(at,from.size(),to);at+=to.size();
            }
        }
        size_t firstPlotItem=s.items.size();
        int plotFace=plot.fontFace>=0?plot.fontFace:plot.thinLines?(d.style.labelFace&~1):d.style.labelFace;
        auto graphFont=[&](const std::string& value,bool legend=false){
            const auto& family=legend&&!plot.legendFontFamily.empty()?plot.legendFontFamily:plot.fontFamily;
            if(family=="Arial")for(unsigned char c:value)if(c>=0xe3)return std::string("Malgun Gothic");
            return family;
        };
        auto faceFor=[&](const std::string& value){
            int face=plotFace;
            for(size_t i=0;i<value.size();i++)if(value[i]=='S'&&(i==0||value[i-1]==' '||value[i-1]=='('||value[i-1]=='\n')){
                size_t at=value.find_first_not_of(" \t",i+1);
                if(at!=std::string::npos&&value[at]=='='){
                    at=value.find_first_not_of(" \t",at+1);
                    if(at!=std::string::npos&&((value[at]>='0'&&value[at]<='9')||value[at]=='X'||value[at]=='x'||value[at]=='?'))face|=2;
                }
            }return face;
        };
        // Axis captions accept x^{2}, H_{2} and Unicode scripts. Each run stays vector text.
        auto axisText=[&](const std::string& value,Vec anchor,double size,const std::string& color,bool vertical,int role=0,int index=-1){
            const auto runs=scriptRuns(value);
            double total=0;for(auto& r:runs)total+=textWidth(measure,r.text,size*(r.script?(r.script==1?chemicalScriptScale(r.text):.7):1),graphFont(r.text),r.italic?faceFor(value)|2:faceFor(value));double cursor=-total/2;
            for(auto& r:runs){double fs=size*(r.script?(r.script==1?chemicalScriptScale(r.text):.7):1),offset=r.script==1?-size*.4:r.script==-1?size*.35:0;Primitive t;t.type=Primitive::Text;t.owner=owner;t.textRole=role;t.textIndex=index;t.text=r.text;t.font=graphFont(r.text);t.fontSize=fs;t.face=r.italic?faceFor(value)|2:faceFor(value);t.color=color;t.rotation=vertical?-90:0;t.points={vertical?anchor+Vec{offset,-cursor}:anchor+Vec{cursor,offset}};s.items.push_back(t);cursor+=textWidth(measure,r.text,fs,t.font,t.face);}
        };
        auto scriptWidth=[&](const std::string& text,double size){
            double width=0;
            for(const auto& run:scriptRuns(text)){
                const double scale=run.script?(run.script==1?chemicalScriptScale(run.text):.7):1;
                width+=textWidth(measure,run.text,size*scale,graphFont(run.text),run.italic?faceFor(text)|2:faceFor(text));
            }
            return width;
        };
        owner=plot.id;auto axis=energyAxis(plot);double low=axis.low,high=axis.high;
        const auto displayBands=energyDisplayBands(plot);const double displayLow=displayEnergy(displayBands,low),displayHigh=displayEnergy(displayBands,high);
        const bool independent=plot.mode==independentEnergyLevels;
        double scale=std::clamp(std::sqrt(plot.width*plot.height/(460.*240.)),.8,2.);
        double tickFont=plot.tickFontSize>0?plot.tickFontSize:11.5*scale*std::pow(plot.axisScale,.2);
        tickFont=std::min(tickFont,std::max(4.,plot.height*.14));
        double stateFont=plot.stateFontSize>0?plot.stateFontSize:tickFont;
        double axisFont=plot.axisFontSize>0?plot.axisFontSize:tickFont;
        double energyFont=plot.energyLabelFontSize>0?plot.energyLabelFontSize:tickFont;
        double valueFont=plot.valueFontSize>0?plot.valueFontSize:12.5*scale;
        auto widthOf=[&](const std::string& text,double size){return textWidth(measure,text,size,graphFont(text),faceFor(text));};
        const std::string unitCaption=displayUnit.empty()?"":
            (plot.yUnitParenthetical?" (":" / (")+displayUnit+")";
        const std::string yCaption=plot.yLabel+unitCaption;
        // Requested sizes are upper bounds: shrink only when the physical graph cannot fit them.
        axisFont=std::min(axisFont,std::max(4.,(plot.width-114)/std::max(1.,widthOf(plot.xLabel,1))));
        tickFont=std::min(tickFont,std::max(4.,plot.height*.14));
        if(!plot.fixedValueFontSize||plot.valueFontSize==0)valueFont=std::min(valueFont,std::max(4.,plot.height*.18));
        double titleFont=axisFont*1.05,legendFont=plot.legendFontSize>0?plot.legendFontSize:tickFont;
        double x0=plot.p.x+50,w=std::max(100.,plot.width-54);
        double y0=plot.p.y+plot.height-std::max(40.,tickFont*1.3+axisFont*1.4+16);
        double axisY=y0;s.plotBaselines[owner]=axisY;
        struct StageLayout {std::string text;double font=0,height=0;int lane=0;};
        std::vector<StageLayout> stageLayouts(plot.stages.size());
        std::vector<std::vector<size_t>> lanes;
        std::vector<double> laneAbove,laneBelow,laneOffsets;
        auto wrapStage=[&](const std::string& label,double font,double available){
            std::string wrapped,line;std::istringstream paragraphs(label);std::string paragraph;
            auto flush=[&](){if(!wrapped.empty())wrapped+='\n';wrapped+=line;line.clear();};
            while(std::getline(paragraphs,paragraph)){
                std::istringstream words(paragraph);std::string word;
                while(words>>word){
                    if(!line.empty()&&widthOf(line+" "+word,font)>available)flush();
                    if(widthOf(word,font)<=available){if(!line.empty())line+=' ';line+=word;continue;}
                    // Balance oversized words over lines, preserving UTF-8 and script groups.
                    std::vector<std::string> units;
                    for(size_t pos=0;pos<word.size();){
                        size_t end=pos+1;
                        if((word[pos]=='_'||word[pos]=='^')&&end<word.size()&&word[end]=='{'){auto close=word.find('}',end);if(close!=std::string::npos)end=close+1;}
                        else while(end<word.size()&&(static_cast<unsigned char>(word[end])&0xc0)==0x80)end++;
                        units.push_back(word.substr(pos,end-pos));pos=end;
                    }
                    size_t parts=std::max(size_t(2),size_t(std::ceil(widthOf(word,font)/std::max(1.,available-widthOf("-",font)))));
                    size_t cursor=0;
                    while(cursor<units.size()){
                        size_t count=std::max(size_t(1),(units.size()-cursor+parts-1)/parts);std::string piece;
                        while(count>1){piece.clear();for(size_t j=0;j<count;j++)piece+=units[cursor+j];if(widthOf(piece+"-",font)<=available)break;count--;}
                        piece.clear();for(size_t j=0;j<count;j++)piece+=units[cursor+j];cursor+=count;
                        if(cursor<units.size()&&word.find_first_of("_^{}")==std::string::npos)piece+='-';
                        line=piece;flush();if(parts>1)parts--;
                    }

                }if(!line.empty())flush();
            }return wrapped;
        };
        const auto stateX=[&](size_t i){return x0+50+(w-100)*plotStateFraction(plot,i);};
        double materialStart=x0,materialEnd=x0+w;
        if(plot.states.size()>1){
            materialStart=stateX(0);
            materialEnd=stateX(plot.states.size()-1);
            if(materialEnd<=materialStart){materialStart=x0;materialEnd=x0+w;}
        }
        const double materialSpan=materialEnd-materialStart;
        const size_t stageCount=plot.stages.size();
        bool uniformlySpaced=stageCount>0;
        for(size_t i=0;i<stageCount&&uniformlySpaced;++i){
            const auto& stage=plot.stages[i];
            uniformlySpaced=stage.levelRange&&stage.row==1&&
                std::abs(stage.start-100.*i/stageCount)<1e-5&&
                std::abs(stage.end-100.*(i+1)/stageCount)<1e-5;
        }
        auto stageRange=[&](const MechanismStage& stage){
            if(stage.levelRange){
                if(uniformlySpaced){
                    const double gap=std::min(6.,materialSpan/stageCount*.12);
                    const double length=(materialSpan-gap*(stageCount-1))/stageCount;
                    const double a=materialStart+(length+gap)*(&stage-plot.stages.data());
                    return std::pair{a,a+length};
                }
                return std::pair{materialStart+materialSpan*stage.start/100,
                                 materialStart+materialSpan*stage.end/100};
            }
            double a=x0+w*stage.start/100,b=x0+w*stage.end/100;
            if(stage.row==0&&plot.states.size()>1){
                // Axis-edge ranges terminate at the outer level centers when they fit.
                double left=stage.start==0?stateX(0):a;
                double right=stage.end==100?stateX(plot.states.size()-1):b;
                if(right-left>=w/100){a=left;b=right;}
            }
            if(stage.row>0){
                const double margin=std::min(3.,(b-a)*.1);
                a+=margin;b-=margin;
            }
            return std::pair{a,b};
        };
        // Reserve requested rows first, then pack the remaining captions by their visible ranges.
        for(bool automatic:{false,true})for(size_t i=0;i<plot.stages.size();++i){
            const auto& stage=plot.stages[i];
            if((stage.row==0)!=automatic)continue;
            const auto [a,b]=stageRange(stage);
            const double span=b-a;
            const double font=std::min(stage.fontSize>0?stage.fontSize:tickFont,span*.65);
            const double available=stage.captionBelow?span:std::max(1.,span-2*std::min(font*1.5,span*.16));
            auto text=wrapStage(stage.label,font,available);
            const double height=font*1.3*(1+std::count(text.begin(),text.end(),'\n'));
            size_t lane=automatic?0:size_t(stage.row-1);
            if(automatic){
                for(;lane<lanes.size();++lane){
                    const bool overlap=std::any_of(lanes[lane].begin(),lanes[lane].end(),[&](size_t j){
                        const auto& peer=plot.stages[j];
                        const auto [left,right]=stageRange(peer);
                        return a<right+4&&b+4>left;
                    });
                    if(!overlap)break;
                }
            }
            if(lane>=lanes.size()){
                lanes.resize(lane+1);
                laneAbove.resize(lane+1);
                laneBelow.resize(lane+1);
            }
            lanes[lane].push_back(i);
            const double textGap=std::max(4.,font*.25);
            const double headRoom=std::max({6.,font*.4,stage.arrowWidth/2,stage.arrowHeadSize*.4});
            laneAbove[lane]=std::max(laneAbove[lane],stage.captionBelow?headRoom:height/2+headRoom);
            laneBelow[lane]=std::max(laneBelow[lane],stage.captionBelow?textGap+height+headRoom:height/2+headRoom);
            stageLayouts[i]={std::move(text),font,height,int(lane)};
        }
        double stageBand=0;
        for(size_t i=0;i<lanes.size();++i){
            laneOffsets.push_back(stageBand+laneAbove[i]);
            stageBand+=lanes[i].empty()?18:laneAbove[i]+laneBelow[i];
        }
        if(!plot.stages.empty())y0-=stageBand+8;
        double topY=plot.p.y+std::max(25.,valueFont*1.3+12),h=std::max(25.,y0-topY);topY=y0-h;s.plotDragScales.emplace(owner,PlotDragScale{(displayHigh-displayLow)/h,low,high});
        auto yAt=[&](double value){return y0-h*(displayEnergy(displayBands,value)-displayLow)/(displayHigh-displayLow);};
        energyFont=std::min(energyFont,std::max(4.,(plot.energyAtTop?w*.95:h*.95)/std::max(1.,scriptWidth(plot.energyAtTop?(plot.energySymbol.empty()?"E":plot.energySymbol)+unitCaption:yCaption,1))));
        auto plotText=[&](const std::string& text,Vec at,double size,const std::string& color="#000000",bool centered=false,int role=0,int index=-1){
            Primitive item;item.type=Primitive::Text;item.owner=owner;item.textRole=role;item.textIndex=index;item.text=text;item.fontSize=size;item.face=role==7&&plot.legendFace>=0?plot.legendFace:faceFor(text);item.color=color;
            item.font=graphFont(text,role==7);
            emitText(s.items,item,at,centered,measure);
        };
        auto overlaps=[](Rect a,Rect b,double pad=4.){return a.x<b.x+b.w+pad&&a.x+a.w+pad>b.x&&a.y<b.y+b.h+pad&&a.y+a.h+pad>b.y;};
        std::vector<Rect> external;
        for(auto& item:s.items)if(item.owner!=owner&&!item.points.empty()){
            double l=1e20,t=1e20,r=-1e20,b=-1e20;
            for(auto pt:item.points){l=std::min(l,pt.x);t=std::min(t,pt.y);r=std::max(r,pt.x);b=std::max(b,pt.y);}
            if(item.type==Primitive::Text){r=l+textWidth(measure,item.text,item.fontSize,item.font,item.face);b=t+item.fontSize*1.3;}
            external.push_back({l,t,std::max(1.,r-l),std::max(1.,b-t)});
        }
        const PlotAxes axes{{x0,axisY},{x0+w+12*plot.axisScale,axisY},{x0,topY-14*plot.axisScale},axisFont,tickFont,valueFont,energyFont,h};
        s.plotAxes[owner]=axes;
        const double inkFactor=plot.inkScale*(plot.thinLines?.5:1.);
        const double automaticHeadScale=plot.axisScale*(3.5/3.0);
        if(plot.showXAxis)arrow(axes.origin,axes.xEnd,"#000000",
            plot.xAxisWidth>0?plot.xAxisWidth/inkFactor:std::max(.8,d.style.lineWidth*.6)*plot.axisScale,1,
            plot.xAxisHeadLength>0?plot.xAxisHeadLength/5:automaticHeadScale,plot.xAxisHeadWidth);
        const double yAxisStroke=plot.yAxisWidth>0?plot.yAxisWidth/inkFactor:plot.series.empty()?2.4:plot.series.front().width;
        if(plot.showYAxis)arrow(axes.origin,axes.yEnd,"#000000",yAxisStroke,1,
            plot.yAxisHeadLength>0?plot.yAxisHeadLength/5:std::max(automaticHeadScale,yAxisStroke*inkFactor*.6),plot.yAxisHeadWidth);
        for(size_t i=0;i<plot.stages.size();++i){
            const auto& stage=plot.stages[i];
            auto [a,b]=stageRange(stage);
            a+=stage.offset.x;
            b+=stage.offset.x;
            const double center=(a+b)/2;
            const auto& layout=stageLayouts[i];
            const double font=layout.font;
            const double y=y0+8+laneOffsets[layout.lane]+stage.offset.y;
            const double gap=stage.captionBelow?0:widthOf(layout.text,font)/2+std::min(font*.35,(b-a)*.04);
            const double shaft=std::max(.01,(b-a)/2-gap);
            const double weight=stage.arrowWidth>0?stage.arrowWidth/inkFactor:std::max(.6,std::min(font*.085,shaft*.08));
            const double automaticHead=std::min(font*.7,std::max({font*.20,shaft*.18,weight*inkFactor*3}));
            const double headLength=std::min(stage.arrowHeadSize>0?stage.arrowHeadSize:automaticHead,shaft*.625);
            if(stage.captionBelow){
                Primitive line;
                line.owner=owner;line.color=plot.xColor;line.width=weight;
                line.points={{a+headLength*.96,y},{b-headLength*.96,y}};
                s.items.push_back(line);
                line.type=Primitive::Polygon;
                const double side=headLength*.4;
                line.points={{a,y},{a+headLength,y+side},{a+headLength,y-side}};
                s.items.push_back(line);
                line.points={{b,y},{b-headLength,y-side},{b-headLength,y+side}};
                s.items.push_back(line);
            }else{
                arrow({center-gap,y},{a,y},plot.xColor,weight,1,headLength/5);
                arrow({center+gap,y},{b,y},plot.xColor,weight,1,headLength/5);
            }
            const double captionY=stage.captionBelow?y+std::max({4.,font*.25,headLength*.4+2,weight*inkFactor/2+2}):y-layout.height/2;
            plotText(layout.text,{center,captionY},font,plot.xColor,true,12,int(i));
        }
        std::vector<double> ticks;for(int i=0;plot.showYAxis&&plot.showYTicks&&i<=axis.intervals;i++)ticks.push_back(axis.firstTick+axis.step*i);
        std::sort(ticks.begin(),ticks.end());ticks.erase(std::unique(ticks.begin(),ticks.end(),[&](double a,double b){return std::abs(a-b)<axis.step*1e-7;}),ticks.end());
        double tickWidth=0;for(double v:ticks)tickWidth=std::max(tickWidth,widthOf(energyTickLabel(v,axis.step),tickFont));
        const double titleGap=plot.yTitleGap>0?plot.yTitleGap:std::max(24.,energyFont*.8);
        const double titleInkPad=energyFont*.025*std::max(0.,plot.inkScale-1);
        const double axisLeft=std::min(x0-std::max(yAxisStroke*inkFactor,plot.yAxisHeadWidth)/2,ticks.empty()?x0:x0-10-tickWidth);
        // A rotated subscript extends right of the normal glyphs. Include both
        // script offsets and added text ink before measuring the visible gap.
        double yAxisX=axisLeft-titleGap-energyFont*1.65-titleInkPad;
        Rect yAxisBox{yAxisX-energyFont*.4-titleInkPad,y0-h/2-widthOf(yCaption,energyFont)/2,energyFont*2.05+2*titleInkPad,widthOf(yCaption,energyFont)};
        for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto other:external)if(overlaps(yAxisBox,other)){double shift=other.x-yAxisBox.w-8-yAxisBox.x;yAxisBox.x+=shift;yAxisX+=shift;moved=true;}if(!moved)break;}
        if(plot.showYAxis&&plot.energyAtTop){
            // Keep the quantity and unit on one baseline above the arrowhead. Reserve
            // the whole header, including superscripts, before laying out legends.
            const double headerY=topY-14*plot.axisScale-energyFont*1.7-titleGap;
            const auto header=plot.energySymbol.empty()?std::string("E"):plot.energySymbol;
            const double eWidth=scriptWidth(header,energyFont);
            const double unitWidth=unitCaption.empty()?0:scriptWidth(unitCaption,energyFont);
            axisText(header,{x0,headerY},energyFont,plot.yColor,false);
            if(unitWidth>0)axisText(unitCaption,{x0+eWidth/2+unitWidth/2,headerY},energyFont,plot.yColor,false);
            external.push_back({x0-eWidth/2,headerY-energyFont*.4,eWidth+unitWidth,energyFont*1.7});
        }else if(plot.showYAxis)axisText(yCaption,{yAxisX,y0-h/2},energyFont,plot.yColor,true);
        // Label regular ticks only; range boundaries never create an extra irregular tick.
        double tickHeight=tickFont*1.3,lastLabel=y0+tickHeight+10;
        for(size_t tick=0;tick<ticks.size();tick++){
            double v=ticks[tick],y=yAt(v);bool endpoint=tick==0||tick+1==ticks.size();
            if(endpoint||(lastLabel-y>=tickHeight+4&&y-topY>=tickHeight+4)){
                plotText(energyTickLabel(v,axis.step),{x0-10-widthOf(energyTickLabel(v,axis.step),tickFont),y-tickHeight/2},tickFont,plot.yColor);lastLabel=y;
            }
            Primitive mark;mark.owner=owner;mark.points={{x0-3*plot.axisScale,y},{x0,y}};mark.width=.5*plot.axisScale;s.items.push_back(mark);
        }
        auto waveSpacing=[&](double width){return std::clamp(width*inkFactor*.2+1.6,2.4,3.8);};
        auto waveHalfLength=[&](double width){return std::clamp(12.+width*inkFactor,13.,21.);};
        auto automaticWaveStroke=[&](double width){return std::clamp(width*inkFactor*.65,.7,2.6);};
        auto waveStroke=[&](double width){return plot.waveWidth>0?plot.waveWidth:automaticWaveStroke(width);};
        auto waveClearance=[&](double width){return (plot.waveGap>=0?plot.waveGap:1.)+std::max(0.,waveStroke(width)-automaticWaveStroke(width))*.5;};
        auto wave=[&](Vec center,Vec along,Vec across,double width,const std::string& color){
            const double spacing=waveSpacing(width),length=waveHalfLength(width);
            const double stroke=waveStroke(width)/inkFactor;
            for(double shift:{-spacing,spacing})for(int half=0;half<2;++half){
                const double start=-length+length*half,side=(half==0?1.:-1.)*length*.25;
                Primitive curve;curve.type=Primitive::Bezier;curve.owner=owner;curve.color=color;curve.width=stroke;
                curve.points={center+along*shift+across*start,center+along*(shift+side)+across*(start+length/3),center+along*(shift+side)+across*(start+2*length/3),center+along*shift+across*(start+length)};s.items.push_back(curve);
            }
        };
        if(plot.showYAxis)for(const auto& band:displayBands)if(band.marker&&band.high>=low&&band.low<=high){
            const double y=(yAt(std::max(low,band.low))+yAt(std::min(high,band.high)))/2;
            const bool waveBreak=std::any_of(plot.states.begin(),plot.states.end(),[](const auto& state){return state.breakAfter&&state.breakStyle==1&&(state.breakScale!=1||state.breakOmit>0);});
            const double axisHole=std::max(7.,waveSpacing(yAxisStroke)+waveHalfLength(yAxisStroke)*.1875+yAxisStroke*inkFactor*.4+(waveBreak?waveClearance(yAxisStroke):1.));
            // Cut the axis itself instead of masking with a background-colored patch.
            for(size_t k=0,count=s.items.size();k<count;++k){const auto shaft=s.items[k];
                if(shaft.owner!=owner||shaft.type!=Primitive::Line||shaft.points.size()!=2||std::abs(shaft.points[0].x-x0)>1e-8||std::abs(shaft.points[1].x-x0)>1e-8)continue;
                const double top=std::min(shaft.points[0].y,shaft.points[1].y),bottom=std::max(shaft.points[0].y,shaft.points[1].y);
                if(top<y-axisHole&&bottom>y+axisHole){s.items[k].points={{x0,top},{x0,y-axisHole}};auto lower=shaft;lower.points={{x0,y+axisHole},{x0,bottom}};s.items.push_back(lower);}
            }

            if(waveBreak){wave({x0,y},{0,1},{1,0},yAxisStroke,"#000000");continue;}
            for(double shift:{-3.,3.}){Primitive slash;slash.owner=owner;slash.width=yAxisStroke;slash.points={{x0-5,y+shift+3},{x0+5,y+shift-3}};s.items.push_back(slash);}
        }
        std::vector<EnergySeries> paths=plot.series;if(paths.empty()){EnergySeries path;for(auto& st:plot.states)path.values.push_back(st.energy);paths.push_back(path);}
        if(plot.curveWidth>0)for(auto& path:paths)path.width=plot.curveWidth/inkFactor;
        titleFont=std::min(titleFont,std::max(4.,w/std::max(1.,widthOf(plot.title,1))));
        auto legendWidth=[&](const std::string& name,double size){
            return textWidth(measure,name,size,graphFont(name,true),plot.legendFace<0?faceFor(name):plot.legendFace);
        };
        std::vector<size_t> legendPaths;
        for(size_t k=0;k<paths.size();k++){
            bool hasData=false;for(size_t i=0;i<plot.states.size();i++)if(paths[k].hasValue(i)){hasData=true;break;}
            if(hasData){
                legendPaths.push_back(k);
                legendFont=std::min(legendFont,std::max(1.,(w-29)/std::max(1.,legendWidth(paths[k].name,1))));
            }
        }
        // Align each legend row with the upper-right corner of the two axes.
        // The default title duplicates the x caption; retain its saved value without printing twice.
        bool showTitle=!plot.title.empty()&&plot.title!=plot.xLabel;
        const bool autoLegend=legendPaths.size()>1||(!legendPaths.empty()&&paths[legendPaths[0]].name!="Path 1");
        const bool showLegend=!legendPaths.empty()&&(plot.legendVisibility==1||(plot.legendVisibility==0&&autoLegend));
        struct LegendRow{std::vector<size_t> paths;double width=0;};std::vector<LegendRow> rows;
        double itemGap=std::max(14.,legendFont),sampleWidth=22,swatchGap=7,rowHeight=legendFont*1.3,rowGap=5;
        if(showLegend)for(size_t k:legendPaths){
            double itemWidth=sampleWidth+swatchGap+legendWidth(paths[k].name,legendFont);
            if(rows.empty()||(!rows.back().paths.empty()&&rows.back().width+itemGap+itemWidth>w*.92))rows.push_back({});
            auto& row=rows.back();if(!row.paths.empty())row.width+=itemGap;row.width+=itemWidth;row.paths.push_back(k);
        }
        const double legendRight=axes.xEnd.x+plot.legendOffset.x;
        const double legendTop=axes.yEnd.y+plot.legendOffset.y;
        if(showTitle){
            double titleY=topY-18-titleFont*1.3-7;
            Rect titleBox{x0+w/2-widthOf(plot.title,titleFont)/2,titleY,widthOf(plot.title,titleFont),titleFont*1.3};
            for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto box:external)if(overlaps(titleBox,box)){titleBox.y=box.y-titleBox.h-8;moved=true;}if(!moved)break;}
            plotText(plot.title,{x0+w/2,titleBox.y},titleFont,"#000000",true,4);
        }
        for(size_t r=0;r<rows.size();r++){
            double cursor=legendRight-rows[r].width,y=legendTop+r*(rowHeight+rowGap);
            external.push_back({cursor,y,rows[r].width,rowHeight});
            for(size_t k:rows[r].paths){auto& path=paths[k];double cy=y+rowHeight/2;
                Primitive sample;sample.owner=owner;sample.textRole=7;sample.textIndex=int(k);sample.width=path.width;sample.color=path.color;sample.dash=1;sample.points={{cursor,cy},{cursor+sampleWidth,cy}};s.items.push_back(sample);
                plotText(path.name,{cursor+sampleWidth+swatchGap,y},legendFont,path.color,false,7,int(k));
                cursor+=sampleWidth+swatchGap+legendWidth(path.name,legendFont)+itemGap;
            }
        }
        auto pointAt=[&](size_t i,double value){return Vec{stateX(i),yAt(value)};};
        if(independent&&low<0&&high>0){
            const double zeroY=yAt(0);
            Primitive reference;
            reference.owner=owner;
            reference.color="#b6bac0";
            reference.width=.75*plot.axisScale;
            reference.dash=1;
            reference.points={{x0,zeroY},{x0+w,zeroY}};
            s.items.push_back(reference);
        }
        // All plateaus share one symmetric length; marker and state center never move.
        double stateGap=(w-100)/std::max(size_t(1),plot.states.size()-1),labelWidth=0;
        for(size_t i=1;i<plot.states.size();++i)stateGap=std::min(stateGap,stateX(i)-stateX(i-1));
        for(auto& path:paths)for(size_t i=0;i<path.values.size();i++)if(path.hasValue(i)){
            std::ostringstream label;label.imbue(std::locale::classic());label<<std::fixed<<std::setprecision(1)<<path.values[i];
            labelWidth=std::max(labelWidth,widthOf(label.str(),valueFont));
        }
        double half=.75*std::min({45.,stateGap*.30,std::max({stateGap*.20,labelWidth/2+5*scale,12*scale})});
        const Vec levelInset{plot.mode>=2?half:0,0};
        auto visible=[&](const EnergySeries& path,size_t i){return path.hasValue(i)&&path.values[i]>=low-1e-9&&path.values[i]<=high+1e-9;};
        std::vector<Vec> obstacles,levelObstacles;
        auto remember=[&](Vec a,Vec b){int samples=std::clamp(int(std::ceil((b-a).length()/3)),2,60);for(int j=0;j<=samples;j++){Vec pt=a+(b-a)*(double(j)/samples);if(pt.y>=topY&&pt.y<=y0)obstacles.push_back(pt);}};
        auto segment=[&](Vec a,Vec b,const EnergySeries& path,int dash,bool level=false){
            remember(a,b);double dy=b.y-a.y,t0=0,t1=1;
            if(std::abs(dy)<1e-12){if(a.y<topY||a.y>y0)return;}
            else{double u=(topY-a.y)/dy,v=(y0-a.y)/dy;if(u>v)std::swap(u,v);t0=std::max(0.,u);t1=std::min(1.,v);if(t0>t1)return;}
            Primitive part;part.owner=owner;part.width=path.width*(level&&plot.boldLevels&&plot.thinLines&&plot.curveWidth==0?2.:1.);part.color=path.color;part.dash=dash;part.points={a+(b-a)*t0,a+(b-a)*t1};s.items.push_back(part);
        };
        std::map<size_t,std::pair<const EnergyCrossing*,Vec>> crossings;
        if(!independent)for(const auto& crossing:plot.crossings){
            const auto i=size_t(crossing.segment);
            if(!hasCrossingPaths(plot,crossing))continue;
            const auto a=pointAt(i,crossing.energy),b=pointAt(i+1,crossing.energy);
            const double inset=plot.mode>=2?half:0;
            const auto left=a+Vec{inset,0},right=b-Vec{inset,0};
            crossings.emplace(i,std::make_pair(&crossing,left+(right-left)*crossing.position));
        }
        // A continuing branch starts on the already drawn common plateau.
        // Keep that level in its route, but render its mark and value only once.
        std::vector<std::optional<size_t>> sharedStarts(paths.size());
        for(size_t k=1;k<paths.size();++k){
            if(independent||!paths[k].sharedStart)continue;
            for(size_t i=0;i<plot.states.size();++i){
                if(!paths[k].hasValue(i))continue;
                const auto& branch=paths[k];
                const auto shared=std::find_if(paths.begin(),paths.begin()+k,[&](const auto& earlier){
                    return earlier.hasValue(i)&&std::abs(earlier.values[i]-branch.values[i])<1e-9;
                });
                if(shared!=paths.begin()+k)sharedStarts[k]=i;
                break;
            }
        }
        auto connect=[&](Vec a,Vec b,const EnergySeries& path,double breakX,double breakScale,int breakStyle){
            if(std::isfinite(breakX)&&breakX>a.x&&breakX<b.x&&b.x-a.x>20){
                Vec tangent=(b-a)*(1/std::max(1e-9,(b-a).length()));
                const double hole=breakStyle==1?std::min((b.x-a.x)*.25,std::max(7.,std::abs(tangent.x)*(waveSpacing(path.width)+waveHalfLength(path.width)*.1875)+std::abs(tangent.y)*waveHalfLength(path.width)+path.width*inkFactor*.4+waveClearance(path.width))):8.;
                breakX=std::clamp(breakX,a.x+hole+1,b.x-hole-1);
                // Split the real path instead of painting a white rectangle over other objects.
                Vec c1{a.x+(b.x-a.x)*.42,a.y},c2{b.x-(b.x-a.x)*.42,b.y};
                auto at=[&](double t){double u=1-t;return plot.mode==0?a*(u*u*u)+c1*(3*u*u*t)+c2*(3*u*t*t)+b*(t*t*t):a+(b-a)*t;};
                auto parameter=[&](double x){double lo=0,hi=1;for(int j=0;j<40;++j){double t=(lo+hi)/2;if(at(t).x<x)lo=t;else hi=t;}return (lo+hi)/2;};
                const double t0=parameter(breakX-hole),t1=parameter(breakX+hole);
                auto part=[&](double start,double end){
                    Vec last=at(start);
                    if(plot.mode==0&&a.y>=topY&&a.y<=y0&&b.y>=topY&&b.y<=y0){
                        auto derivative=[&](double t){double u=1-t;return (c1-a)*(3*u*u)+(c2-c1)*(6*u*t)+(b-c2)*(3*t*t);};
                        Primitive curve;curve.type=Primitive::Bezier;curve.owner=owner;curve.color=path.color;curve.width=path.width;curve.dash=path.dash;
                        curve.points={last,last+derivative(start)*((end-start)/3),at(end)-derivative(end)*((end-start)/3),at(end)};s.items.push_back(curve);
                        for(int j=1;j<=40;++j){auto next=at(start+(end-start)*j/40);remember(last,next);last=next;}
                    }else for(int j=1;j<=40;++j){auto next=at(start+(end-start)*j/40);segment(last,next,path,plot.mode==2?1:path.dash);last=next;}
                };
                part(0,t0);part(t1,1);
                Vec middle=at(parameter(breakX));
                if(breakStyle==1&&middle.y>=topY&&middle.y<=y0){Vec tangent=(b-a)*(1/std::max(1e-9,(b-a).length()));wave(middle,tangent,{-tangent.y,tangent.x},path.width,path.color);}
                if(breakStyle==0&&middle.y>=topY&&middle.y<=y0)for(double shift:{-4.,4.}){Primitive slash;slash.owner=owner;slash.color=path.color;slash.width=path.width;slash.points={middle+Vec{shift-3,9},middle+Vec{shift+3,-9}};s.items.push_back(slash);}
                if(breakStyle==0&&std::abs(breakScale-1)>1e-9&&middle.y>=topY&&middle.y<=y0){
                    std::ostringstream label;label.imbue(std::locale::classic());label<<"× "<<std::setprecision(4)<<breakScale*100<<"%";
                    plotText(label.str(),middle+Vec{0,13},std::max(6.,tickFont*.7),path.color,true);
                }
                return;
            }
            if(plot.mode!=0){segment(a,b,path,plot.mode==2?1:path.dash);return;}
            Vec c1{a.x+(b.x-a.x)*.42,a.y},c2{b.x-(b.x-a.x)*.42,b.y},last=a;
            const bool inRange=a.y>=topY-1e-9&&a.y<=y0+1e-9&&b.y>=topY-1e-9&&b.y<=y0+1e-9;
            for(int j=1;j<=60;j++){
                double t=j/60.,u=1-t;
                Vec next=a*(u*u*u)+c1*(3*u*u*t)+c2*(3*u*t*t)+b*(t*t*t);
                remember(last,next);
                if(!inRange)segment(last,next,path,path.dash);
                last=next;
            }
            if(inRange){Primitive curve;curve.type=Primitive::Bezier;curve.owner=owner;curve.width=path.width;curve.color=path.color;curve.dash=path.dash;curve.points={a,c1,c2,b};s.items.push_back(curve);}
        };
        auto photonArrow=[&](Vec a,Vec b,const EnergySeries& path){
            if(a.y<b.y)std::swap(a,b);
            const double length=(b-a).length();
            if(length<1e-6||a.y<topY||b.y>y0||std::abs(a.y-b.y)<1e-6)return;
            const auto along=(b-a)*(1/length);
            const Vec normal{-along.y,along.x};
            if(a.y>y0)a=a+along*((y0-a.y)/along.y);
            if(b.y>=a.y)return;
            const bool tipVisible=b.y>=topY;
            if(!tipVisible)b=a+along*((topY-a.y)/along.y);
            const double span=(b-a).length();
            const double headLength=tipVisible?std::min(8.,span*.23):0.;
            const Vec shaftEnd=b-along*headLength*.94;
            const double shaftLength=(shaftEnd-a).length();
            const int lobes=std::clamp(int(std::round(shaftLength/9)),3,24);
            const double step=shaftLength/lobes;
            const double amplitude=std::min(5.,step*.75);
            const double width=std::clamp(path.width*.65,1.,2.5)/inkFactor;
            for(int lobe=0;lobe<lobes;++lobe){
                const Vec start=a+along*(step*lobe),end=start+along*step;
                const auto side=normal*(lobe%2?-amplitude:amplitude);
                Primitive wave;wave.owner=owner;wave.color=path.color;
                wave.width=width;wave.type=Primitive::Bezier;
                wave.points={start,start+along*(step/3)+side,end-along*(step/3)+side,end};
                s.items.push_back(wave);
                remember(start,end);
            }
            if(tipVisible){
                Primitive head;head.owner=owner;head.type=Primitive::Polygon;head.color=path.color;head.width=0;head.filled=true;
                const Vec base=b-along*headLength;
                head.points={b,base+normal*(headLength*.45),base-normal*(headLength*.45)};
                s.items.push_back(head);
            }
            const std::string label="hν";
            const double font=std::clamp(tickFont,8.,22.);
            const Vec center=(a+b)*.5;
            const double gap=amplitude+5+font*.55;
            Rect caption{center.x+normal.x*gap-widthOf(label,font)/2,
                         center.y+normal.y*gap-font*.65,widthOf(label,font),font*1.3};
            if(std::any_of(external.begin(),external.end(),[&](auto box){return overlaps(caption,box);})){
                caption.x-=2*normal.x*gap;caption.y-=2*normal.y*gap;
            }
            plotText(label,{caption.x,caption.y},font,path.color);
            external.push_back(caption);
        };
        for(size_t pathIndex=0;pathIndex<paths.size();pathIndex++){const auto& basePath=paths[pathIndex];if(basePath.values.size()!=plot.states.size())continue;auto path=basePath;
            struct RoutePoint { Vec left,right; size_t index; bool crossing; };
            std::vector<RoutePoint> route;
            for(size_t i=0;i<path.values.size();i++){
                if(path.hasValue(i)){
                    const auto point=pointAt(i,path.values[i]);
                    route.push_back({point-levelInset,point+levelInset,i,false});
                }
                const auto found=crossings.find(i);
                if(found==crossings.end())continue;
                const auto& [crossing,point]=found->second;
                if(int(pathIndex)==crossing->firstPath||int(pathIndex)==crossing->secondPath)
                    route.push_back({point,point,i,true});
            }
            for(size_t i=1;!independent&&i<route.size();i++){
                const auto& left=route[i-1];
                const auto& right=route[i];
                // Ordinary levels leave gaps. A crossing connects to the next
                // available level or crossing even when intervening levels are missing.
                if(!left.crossing&&!right.crossing&&left.index+1!=right.index)continue;
                const auto colorIndex=right.crossing?right.index:left.index;
                path.color=colorIndex<basePath.segmentColors.size()&&!basePath.segmentColors[colorIndex].empty()
                    ?basePath.segmentColors[colorIndex]:basePath.color;
                if(!left.crossing&&!right.crossing&&plot.states[left.index].photoPath==int(pathIndex)&&
                    std::abs(left.right.y-right.left.y)>1e-6){
                    photonArrow(left.right,right.left,path);
                    continue;
                }
                double breakX=std::numeric_limits<double>::quiet_NaN();
                if(left.index<plot.states.size()&&plot.states[left.index].breakAfter&&left.index+1<plot.states.size()){
                    const double inset=plot.mode>=2?half:0;
                    double start=stateX(left.index)+inset,end=stateX(left.index+1)-inset;
                    breakX=start+(end-start)*plot.states[left.index].breakPosition;
                }
                connect(left.right,right.left,path,breakX,left.index<plot.states.size()?(plot.states[left.index].breakOmit>0?1.:plot.states[left.index].breakScale):1.,left.index<plot.states.size()?plot.states[left.index].breakStyle:0);
                if(plot.mode==0||(plot.mode!=2&&!path.dash))continue;
                auto solidEnd=[&](Vec point,Vec other){
                    const auto toward=other-point;
                    const double length=toward.length();
                    if(length>1e-8)
                        segment(point+toward*(std::min(16.,length*.65)/length),point,path,0);
                };
                if(right.crossing)solidEnd(right.left,left.right);
                if(left.crossing&&i==1)solidEnd(left.right,right.left);
            }
            for(size_t i=0;i<path.values.size();i++){
                if(!path.hasValue(i)||sharedStarts[pathIndex]==i)continue;
                Vec a=pointAt(i,path.values[i]);
                path.color=i<basePath.levelColors.size()&&!basePath.levelColors[i].empty()?basePath.levelColors[i]:basePath.color;
                if(visible(path,i))for(int edge: {-1,0,1}){
                    if(edge&&plot.mode<2)continue;
                    Vec pt=a+Vec{edge*half,0};s.plotSnapPoints[owner].push_back({pt,100*(pt.x-x0)/w,int(i),int(pathIndex),edge});
                }
                if(plot.mode>=2&&visible(path,i)){
                    segment(a-levelInset,a+levelInset,path,0,true);
                    for(int j=0;j<=30;j++)levelObstacles.push_back(a+Vec{-half+2*half*j/30.,0});
                }
                if(!visible(path,i))continue;
                int marker=plot.states[i].marker<0?path.marker:plot.states[i].marker;
                if(marker){
                    double r=path.markerSize;Primitive symbol;symbol.owner=owner;symbol.color=path.color;symbol.width=path.width;symbol.filled=true;
                    if(marker==1){symbol.type=Primitive::Ellipse;symbol.points={a-Vec{r,r},a+Vec{r,r}};}
                    else{symbol.type=Primitive::Polygon;if(marker==2)symbol.points={a+Vec{-r,-r},a+Vec{r,-r},a+Vec{r,r},a+Vec{-r,r}};else if(marker==3)symbol.points={a+Vec{0,-r*1.3},a+Vec{r*1.3,0},a+Vec{0,r*1.3},a+Vec{-r*1.3,0}};else symbol.points={a+Vec{0,-r*1.4},a+Vec{r*1.3,r},a+Vec{-r*1.3,r}};}
                    s.items.push_back(symbol);for(int j=-4;j<=4;j++)obstacles.push_back(a+Vec{j*r/4,0});for(int dx:{-1,0,1})for(int dy:{-1,0,1})levelObstacles.push_back(a+Vec{dx*r,dy*r});
                }
            }
        }
        for(const auto& [index,entry]:crossings){
            const auto& [crossing,point]=entry;
            if(point.y<topY||point.y>y0)continue;
            const double radius=std::clamp(valueFont*.49,4.5,7.5);
            const double haloRadius=radius+2.4;
            auto diamondPoints=[&](double r){
                return std::vector<Vec>{point+Vec{0,-r},point+Vec{r,0},point+Vec{0,r},point+Vec{-r,0}};
            };
            Primitive diamond;diamond.type=Primitive::Polygon;diamond.owner=owner;diamond.filled=true;
            diamond.color="#ffffff";diamond.points=diamondPoints(haloRadius);
            s.items.push_back(diamond);
            for(auto corner:diamond.points){obstacles.push_back(corner);levelObstacles.push_back(corner);}
            diamond.color="#000000";diamond.closed=true;diamond.points=diamondPoints(radius);
            s.items.push_back(diamond);
            diamond.color="#ffffff";diamond.closed=false;diamond.points=diamondPoints(radius-1);
            s.items.push_back(diamond);

            auto pathColor=[&](int path){
                const auto& series=paths[size_t(path)];
                return index<series.segmentColors.size()&&!series.segmentColors[index].empty()
                    ?series.segmentColors[index]:series.color;
            };
            const double inset=radius-1.6,separator=.45;
            Primitive face=diamond;
            face.color=pathColor(crossing->secondPath);
            face.points={point+Vec{-inset+separator,-separator},point+Vec{0,-inset},point+Vec{inset-separator,-separator}};
            s.items.push_back(face);
            face.color=pathColor(crossing->firstPath);
            for(auto& vertex:face.points)vertex.y=2*point.y-vertex.y;
            s.items.push_back(face);

            const double font=std::max(4.,valueFont*.75);
            const auto lines=1+std::count(crossing->label.begin(),crossing->label.end(),'\n');
            const double labelHeight=font*(1.1+(lines-1)*1.28);
            const double numberFont=font*.92;
            const double height=labelHeight+(plot.showValues?numberFont*1.2:0);
            const double distance=haloRadius+12;
            const bool below=point.y+distance+height<=y0||point.y-topY<distance+height;
            const auto offset=crossing->labelOffset.value_or(Vec{0,below?distance:-distance-height});
            const auto anchor=point+offset;
            std::ostringstream value;value.imbue(std::locale::classic());
            value<<std::fixed<<std::setprecision(2)<<crossing->energy;
            const double captionWidth=std::max(widthOf(crossing->label,font),
                                               plot.showValues?widthOf(value.str(),numberFont):0.);
            const Rect box{anchor.x-captionWidth/2,anchor.y,captionWidth,height};
            s.crossingCaptions.push_back({owner,size_t(crossing-plot.crossings.data()),box,offset});
            if(crossing->leaderWidth>0){
                const Vec edge{std::clamp(point.x,box.x,box.x+box.w),std::clamp(point.y,box.y,box.y+box.h)};
                const auto toward=edge-point;
                const double length=toward.length();
                if(length>0){
                    const auto unit=toward*(1/length);
                    const double start=haloRadius/(std::abs(unit.x)+std::abs(unit.y))+1.5;
                    if(length>start+3){
                        Primitive leader;leader.owner=owner;leader.color="#606060";
                        leader.width=crossing->leaderWidth*(plot.thinLines?2:1);
                        leader.points={point+unit*start,edge-unit*3};
                        s.items.push_back(leader);
                    }
                }
            }
            plotText(crossing->label,anchor,font,"#000000",true);
            if(plot.showValues)
                plotText(value.str(),anchor+Vec{0,labelHeight},numberFont,"#333333",true);
        }
        // TS brackets follow the actual inserted structures, including labels and bonds.
        for(size_t i=0;i<plot.states.size();++i)if(plot.states[i].transitionState){
            for(const auto* members:{&plot.states[i].above,&plot.states[i].below}){
                if(members->empty())continue;
                double l=1e20,t=1e20,r=-1e20,b=-1e20;
                for(const auto& item:s.items)if(std::find(members->begin(),members->end(),item.owner)!=members->end())for(auto pt:item.points){
                    double right=pt.x,bottom=pt.y;
                    if(item.type==Primitive::Text){right+=textWidth(measure,item.text,item.fontSize,item.font,item.face);bottom+=item.fontSize*1.3;}
                    l=std::min(l,pt.x);t=std::min(t,pt.y);r=std::max(r,right);b=std::max(b,bottom);
                }
                if(l>r)continue;l-=8;r+=8;t-=7;b+=7;
                for(bool right:{false,true}){double x=right?r:l,dx=right?-7:7;Primitive bracket;bracket.owner=owner;bracket.width=1.2;bracket.points={{x+dx,t},{x,t},{x,b},{x+dx,b}};s.items.push_back(bracket);}
                plotText("‡",{r+5,t-6},tickFont);
                external.push_back({l,t-6,r-l+tickFont+8,b-t+6});
            }
        }
        struct StateNameLayout {std::string text;double x=0;bool raised=false;};
        std::vector<StateNameLayout> names(plot.states.size());
        std::vector<size_t> automaticAxisNames;
        for(size_t i=0;i<plot.states.size();++i){
            const auto& state=plot.states[i];
            if(state.labelPlacement>=3||state.label.empty())continue;
            auto& name=names[i];
            name.text=state.label;
            if(state.transitionState&&state.above.empty()&&state.below.empty())
                name.text="["+name.text+"]^{‡}";
            name.x=stateX(i)+state.labelOffset.x;
            for(const auto& run:scriptRuns(name.text))if(run.script==1)name.raised=true;
            if(state.labelPlacement==0&&state.labelOffset.y==0)automaticAxisNames.push_back(i);
        }

        // Narrow plots wrap long captions while the first lines stay on one baseline.
        double axisStateFont=stateFont;
        auto ordered=automaticAxisNames;
        std::sort(ordered.begin(),ordered.end(),[&](size_t a,size_t b){return names[a].x<names[b].x;});
        std::vector<double> available(names.size(),std::numeric_limits<double>::infinity());
        for(size_t j=1;j<ordered.size();++j){
            const auto left=ordered[j-1],right=ordered[j];
            const double gap=names[right].x-names[left].x;
            const double width=std::max(.25,gap-1);
            available[left]=std::min(available[left],width);
            available[right]=std::min(available[right],width);
            const double needed=(widthOf(names[left].text,1)+widthOf(names[right].text,1))/2;
            if(needed>0)axisStateFont=std::min(axisStateFont,std::max(std::min(4.,width*.7),width/needed));
        }
        for(size_t i:ordered)if(widthOf(names[i].text,axisStateFont)>available[i])
            names[i].text=wrapStage(names[i].text,axisStateFont,available[i]);
        auto nameBox=[&](const StateNameLayout& name,double baseline,double font){
            const double pad=name.raised?font*.4:0;
            const double width=widthOf(name.text,font);
            const double height=font*1.3*(1+std::count(name.text.begin(),name.text.end(),'\n'));
            return Rect{name.x-width/2,baseline-pad,width,height+pad};
        };
        double axisNameBaseline=axisY+10;
        for(size_t i=0;i<names.size();++i)if(plot.states[i].labelPlacement==0&&names[i].raised){
            axisNameBaseline+=axisStateFont*.4;
            break;
        }
        // If a structure occupies the row, lower the row as a unit.
        for(size_t pass=0;pass<external.size();++pass){
            double next=axisNameBaseline;
            for(size_t i:automaticAxisNames){
                const auto& name=names[i];
                const auto box=nameBox(name,axisNameBaseline,axisStateFont);
                for(const auto& outside:external)if(overlaps(box,outside))
                    next=std::max(next,outside.y+outside.h+5+(name.raised?axisStateFont*.4:0));
            }
            if(next==axisNameBaseline)break;
            axisNameBaseline=next;
        }
        std::vector<Rect> stateBoxes;
        double stateBottom=axisY+10;
        for(size_t i=0;i<names.size();++i){
            const auto& state=plot.states[i];
            const auto& name=names[i];
            if(name.text.empty())continue;
            const bool onCurve=state.labelPlacement==1||state.labelPlacement==2;
            const double font=onCurve?stateFont:axisStateFont;
            double baseline=axisNameBaseline;
            if(onCurve){
                const size_t path=size_t(state.labelPath);
                if(path>=paths.size()||!visible(paths[path],i))continue;
                const double level=pointAt(i,paths[path].values[i]).y;
                const double textHeight=font*1.3*(1+std::count(name.text.begin(),name.text.end(),'\n'));
                baseline=state.labelPlacement==1?level-10-textHeight:level+10+(name.raised?font*.4:0);
            }
            baseline+=state.labelOffset.y;
            const auto box=nameBox(name,baseline,font);
            stateBoxes.push_back(box);
            if(!onCurve)stateBottom=std::max(stateBottom,box.y+box.h);
            plotText(name.text,{name.x,baseline},font,"#000000",true,6,int(i));
        }
        // Gaps use current data, so changing energies or units updates the annotation.
        for(size_t i=0;i<plot.states.size();++i){const auto& state=plot.states[i];
            if(state.gapFrom<0||size_t(state.gapFrom)>=plot.states.size()||size_t(state.gapPath)>=paths.size())continue;
            const auto& path=paths[size_t(state.gapPath)];size_t from=size_t(state.gapFrom);
            if(!visible(path,i)||!visible(path,from)||std::abs(path.values[i]-path.values[from])<1e-10)continue;
            Vec start=pointAt(from,path.values[from]),end=pointAt(i,path.values[i]);
            double x=end.x+state.gapOffset.x,mid=(start.y+end.y)/2;
            // Dashed reference guides and two arrowheads at the physical energies.
            for(auto pt:{start,end}){Primitive guide;guide.owner=owner;guide.width=.8;guide.dash=1;guide.points={pt,{x,pt.y}};s.items.push_back(guide);}
            double head=std::min(4.,std::abs(end.y-start.y)*.2);
            Primitive shaft;shaft.owner=owner;shaft.width=.9;shaft.dash=1;shaft.points={{x,start.y},{x,end.y}};s.items.push_back(shaft);
            for(auto pair:{std::pair{start.y,end.y},std::pair{end.y,start.y}}){double dy=pair.second>pair.first?head:-head;Primitive tip;tip.type=Primitive::Polygon;tip.owner=owner;tip.filled=true;tip.width=0;tip.points={{x,pair.first},{x-2.5,pair.first+dy},{x+2.5,pair.first+dy}};s.items.push_back(tip);}
            std::ostringstream value;value.imbue(std::locale::classic());value<<std::fixed<<std::setprecision(1)<<(path.values[i]-path.values[from]);
            std::string label=state.gapLabel.empty()?plotEnergySymbol(plot)+(state.transitionState?"^{‡} = ":" = ")+value.str():state.gapLabel;
            double tw=widthOf(label,valueFont),tx=x-tw/2-8,ty=mid-valueFont*.65+state.gapOffset.y;
            if(state.gapOffset.y==0){
                // Keep automatic gap captions clear of curves and state names.
                auto clear=[&](double y){Rect box{tx-tw/2,y-valueFont*.4,tw,valueFont*1.7};
                    for(auto other:external)if(overlaps(box,other,3))return false;
                    for(auto other:stateBoxes)if(overlaps(box,other,3))return false;
                    for(auto pt:obstacles)if(pt.x>box.x-4&&pt.x<box.x+box.w+4&&pt.y>box.y-4&&pt.y<box.y+box.h+4)return false;
                    return true;
                };
                const double initial=ty;
                for(int step=0;step<80;++step){double distance=step*valueFont*.3;
                    if(clear(initial-distance)){ty=initial-distance;break;}
                    if(clear(initial+distance)){ty=initial+distance;break;}
                }
            }
            plotText(label,{tx,ty},valueFont,"#000000",true);
            Rect gapBox{tx-tw/2,ty-valueFont*.4,tw,valueFont*1.7};
            s.gapCaptions.push_back({owner,i,gapBox,{state.gapOffset.x,ty-(mid-valueFont*.65)}});
            external.push_back(gapBox);
        }
        Rect xAxisBox{x0+w/2-widthOf(plot.xLabel,axisFont)/2,stateBottom+10,widthOf(plot.xLabel,axisFont),axisFont*1.3};
        for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto other:external)if(overlaps(xAxisBox,other)){xAxisBox.y=other.y+other.h+8;moved=true;}if(!moved)break;}
        if(plot.showXAxis)axisText(plot.xLabel,{x0+w/2,xAxisBox.y},axisFont,plot.xColor,false);
        // Own captions are also obstacles if dense values have to use the outer rail.
        for(auto& item:s.items)if(item.owner==owner&&item.type==Primitive::Text&&!item.text.empty()){
            Vec at=item.points[0];double tw=textWidth(measure,item.text,item.fontSize,item.font,item.face),th=item.fontSize*1.3;
            external.push_back(item.rotation==-90?Rect{at.x,at.y-tw,th,tw}:Rect{at.x,at.y,tw,th});
        }
        struct ValueCaption{Vec anchor;std::string text,color;size_t path,index;Rect box;bool backing=false;};std::vector<ValueCaption> captions;
        if(plot.showValues)for(size_t k=0;k<paths.size();k++)for(size_t i=0;i<paths[k].values.size();i++)if(visible(paths[k],i)&&sharedStarts[k]!=i){
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(1)<<paths[k].values[i];
            Vec anchor=pointAt(i,paths[k].values[i]);
            captions.push_back({anchor,energyCellText(paths[k],i,out.str()),i<paths[k].levelColors.size()&&!paths[k].levelColors[i].empty()?paths[k].levelColors[i]:paths[k].color,k,i,{}});
        }
        // A state's labels always follow its numerical energy order, irrespective of path order.
        std::stable_sort(captions.begin(),captions.end(),[&](const ValueCaption& a,const ValueCaption& b){
            if(a.index!=b.index)return a.index<b.index;
            double av=paths[a.path].values[a.index],bv=paths[b.path].values[b.index];
            return av==bv?a.path<b.path:av>bv;
        });
        // Optimize every state's labels jointly. Dynamic programming chooses the nearest ordered
        // vertical stack instead of letting an early greedy placement force later labels away.
        std::vector<Rect> placedValueBoxes;
        auto fitGroup=[&](size_t first,size_t end,double font,bool allowConnectors)->bool{
            size_t count=end-first;if(!count)return true;
            double th=font*1.3,gap=std::max({2.,font*.20,plot.valueGap}),pitch=th+gap;
            double minY=topY+2,maxY=y0-th-4,step=std::max(.5,h/600.);
            int slots=int(std::floor((maxY-minY)/step))+1;if(slots<=0)return false;
            double maxShift=std::min(h*.45,std::max(2*th,count*pitch+gap));
            const double infinity=std::numeric_limits<double>::infinity();
            std::vector<double> prev(slots,infinity),next(slots,infinity);
            std::vector<std::vector<int>> parent(count,std::vector<int>(slots,-1));
            const auto& profile=allowConnectors?levelObstacles:obstacles;
            double maxWidth=0;for(size_t c=first;c<end;c++)maxWidth=std::max(maxWidth,widthOf(captions[c].text,font));
            std::vector<Vec> local;for(auto pt:profile)if(std::abs(pt.x-captions[first].anchor.x)<=maxWidth/2+gap+1)local.push_back(pt);
            int separation=int(std::ceil(pitch/step));
            for(size_t c=0;c<count;c++){
                auto& caption=captions[first+c];double tw=widthOf(caption.text,font);
                std::fill(next.begin(),next.end(),infinity);double best=infinity;int bestSlot=-1;
                for(int slot=0;slot<slots;slot++){
                    if(c&&slot>=separation&&prev[slot-separation]<best){best=prev[slot-separation];bestSlot=slot-separation;}
                    double y=minY+slot*step;Rect box{caption.anchor.x-tw/2,y,tw,th};
                    double center=y+th/2;if(std::abs(center-caption.anchor.y)>maxShift)continue;
                    if(box.x<x0+4||box.x+box.w>x0+w-4)continue;
                    bool clear=true;for(auto other:external)if(overlaps(box,other,2.)){clear=false;break;}
                    if(clear)for(auto other:placedValueBoxes)if(overlaps(box,other,2.)){clear=false;break;}
                    if(clear)for(auto pt:local)if(pt.x>box.x-gap&&pt.x<box.x+box.w+gap&&pt.y>box.y-gap&&pt.y<box.y+box.h+gap){clear=false;break;}
                    if(!clear|| (c&&!std::isfinite(best)))continue;
                    double above=caption.anchor.y-gap-th,below=caption.anchor.y+gap;
                    double distance=std::min(std::abs(y-above),std::abs(y-below));
                    // Prefer centered, nearby text and a compact group, with no horizontal escape.
                    double cost=distance*distance+.03*std::pow(center-caption.anchor.y,2);
                    next[slot]=(c?best:0)+cost;parent[c][slot]=c?bestSlot:-1;
                }
                prev.swap(next);
            }
            int best=int(std::min_element(prev.begin(),prev.end())-prev.begin());if(!std::isfinite(prev[best]))return false;
            for(size_t c=count;c-->0;){auto& caption=captions[first+c];caption.box={caption.anchor.x-widthOf(caption.text,font)/2,minY+best*step,widthOf(caption.text,font),th};caption.backing=allowConnectors;best=parent[c][best];}
            for(size_t c=first;c<end;c++)placedValueBoxes.push_back(captions[c].box);
            return true;
        };
        // Keep labels in their own state's column and choose one readable size for the graph.
        double maxUnitWidth=0;for(auto& caption:captions)maxUnitWidth=std::max(maxUnitWidth,widthOf(caption.text,1));
        if(!plot.fixedValueFontSize||plot.valueFontSize==0)valueFont=std::min(valueFont,std::max(4.,std::max(12.,stateGap*.80)/std::max(1.,maxUnitWidth)));
        std::map<size_t,size_t> labelsPerState;size_t mostLabels=1;
        for(auto& caption:captions)mostLabels=std::max(mostLabels,++labelsPerState[caption.index]);
        // A shallow multi-path plot needs smaller text before labels drift far from their levels.
        if(!plot.fixedValueFontSize||plot.valueFontSize==0)valueFont=std::min(valueFont,std::max(6.,h/(8.+1.3*mostLabels)));
        const bool strongOmission=std::any_of(plot.states.begin(),plot.states.end(),[](const auto& state){return state.breakAfter&&state.breakOmit>0;});
        const double minimumValueFont=(plot.fixedValueFontSize||strongOmission)&&plot.valueFontSize>0?valueFont:4.;
        bool fitted=captions.empty();
        for(int pass=0;pass<40&&!fitted;pass++){
            fitted=true;placedValueBoxes.clear();
            for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                if(!fitGroup(first,end,valueFont,false)){fitted=false;break;}first=end;
            }
            if(fitted)break;
            // Try a small white backing over connectors before reducing readable type.
            // Dots and level plateaus remain hard obstacles; nothing is moved to a side rail.
            {
                fitted=true;placedValueBoxes.clear();for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                    if(!fitGroup(first,end,valueFont,false)&&!fitGroup(first,end,valueFont,true)){fitted=false;break;}first=end;
                }
                if(fitted)break;
            }
            if(valueFont<=minimumValueFont+.01)break;valueFont=std::max(minimumValueFont,valueFont*.94);
        }
        if(!fitted){
            // If the chosen physical height cannot hold a group, keep that group local above
            // its own state. This bounded column has no diagonal leader or remote value list.
            placedValueBoxes.clear();
            for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                if(!fitGroup(first,end,valueFont,false)&&!fitGroup(first,end,valueFont,true)){
                    double th=valueFont*1.3,gap=std::max(2.,plot.valueGap),top=captions[first].anchor.y-(end-first)*(th+gap)-5;
                    for(size_t c=first;c<end;c++)top=std::min(top,captions[c].anchor.y-(end-first)*(th+gap)-5);
                    bool clear=false;for(int pass=0;pass<300&&!clear;pass++){clear=true;
                        for(size_t c=first;c<end;c++){Rect box{captions[c].anchor.x-widthOf(captions[c].text,valueFont)/2,top+(c-first)*(th+gap),widthOf(captions[c].text,valueFont),th};
                            for(auto other:external)if(overlaps(box,other,2)){clear=false;break;}
                            if(clear)for(auto other:placedValueBoxes)if(overlaps(box,other,2.)){clear=false;break;}
                            if(clear)for(auto pt:levelObstacles)if(pt.x>box.x-2&&pt.x<box.x+box.w+2&&pt.y>box.y-2&&pt.y<box.y+box.h+2){clear=false;break;}
                        }if(!clear)top-=th+gap;
                    }
                    for(size_t c=first;c<end;c++){auto& caption=captions[c];caption.box={caption.anchor.x-widthOf(caption.text,valueFont)/2,top+(c-first)*(th+gap),widthOf(caption.text,valueFont),th};caption.backing=true;placedValueBoxes.push_back(caption.box);}
                }first=end;
            }
        }
        std::stable_sort(captions.begin(),captions.end(),[](const ValueCaption& a,const ValueCaption& b){return a.path!=b.path?a.path<b.path:a.index<b.index;});
        for(auto& caption:captions){
            if(caption.backing){Primitive backing;backing.type=Primitive::Box;backing.owner=owner;backing.color="#ffffff";backing.filled=true;backing.width=0;backing.points={{caption.box.x-1,caption.box.y-1},{caption.box.x+caption.box.w+1,caption.box.y+caption.box.h+1}};s.items.push_back(backing);}
            plotText(caption.text,{caption.box.x,caption.box.y},valueFont,caption.color,false,caption.path==0?8:0,int(caption.index));
        }
        // Place the state name after its numeric caption, whose collision solver
        // may have moved the energy below the plotted level.
        for(size_t i=0;i<plot.states.size();++i){
            const auto& state=plot.states[i];
            const size_t path=size_t(state.labelPath);
            if(state.labelPlacement!=4||path>=paths.size()||!visible(paths[path],i)||state.label.empty())continue;
            std::string label=state.label;
            if(state.transitionState&&state.above.empty()&&state.below.empty())label="["+label+"]^{‡}";
            const double x=stateX(i)+state.labelOffset.x;
            double y=pointAt(i,paths[path].values[i]).y+10;
            const auto caption=std::find_if(captions.begin(),captions.end(),[&](const auto& value){
                return value.index==i&&(value.path==path||(sharedStarts[path]==i&&
                    std::abs(paths[value.path].values[i]-paths[path].values[i])<1e-9));
            });
            if(caption!=captions.end())y=std::max(y,caption->box.y+caption->box.h+5);
            const double width=scriptWidth(label,stateFont);
            const double topPad=label.find("^{")==std::string::npos?0:stateFont*.4;
            Rect box{x-width/2,y+state.labelOffset.y,width,stateFont*1.3+topPad};
            if(state.labelOffset.x==0&&state.labelOffset.y==0){
                auto clear=[&](const Rect& other){if(overlaps(box,other))box.y=other.y+other.h+5;};
                for(size_t pass=0;pass<=external.size()+placedValueBoxes.size();++pass){
                    const double old=box.y;
                    for(const auto& other:external)clear(other);
                    for(const auto& other:placedValueBoxes)clear(other);
                    if(box.y==old)break;
                }
            }
            plotText(label,{x,box.y+topPad},stateFont,"#000000",true,6,int(i));
            external.push_back(box);
        }
        s.plotAxes[owner].valueFont=valueFont;
        if(plot.thinLines)for(size_t i=firstPlotItem;i<s.items.size();i++)if(s.items[i].type!=Primitive::Text)s.items[i].width*=.5;
    }
    for(auto& reaction:d.reactions)for(auto& item:s.items)if(item.owner==reaction.id&&item.type==Primitive::Text){
        if(!reaction.textColor.empty())item.color=reaction.textColor;
        for(auto& caption:reaction.captionColors)if(caption.role==item.textRole&&caption.index==item.textIndex)item.color=caption.color;
    }
    std::map<int,double> graphWeights;
    for(const auto& graph:d.plots)if(std::abs(graph.inkScale-1)>1e-9){
        graphWeights[graph.id]=graph.inkScale;
        for(const auto& state:graph.states)for(const auto* ids:{&state.above,&state.below})for(int id:*ids)graphWeights.emplace(id,graph.inkScale);
    }
    std::map<int,double> structureWeights;
    for(const auto& graph:d.plots)if(std::abs(graph.structureInkScale-1)>1e-9)
        for(const auto& state:graph.states)for(const auto* ids:{&state.above,&state.below})for(int id:*ids)structureWeights.emplace(id,graph.structureInkScale);
    for(auto& item:s.items)if(item.type!=Primitive::Text&&item.type!=Primitive::Image)
        if(const auto found=structureWeights.find(item.owner);found!=structureWeights.end()){
            item.width*=found->second;
            if(item.type==Primitive::Polygon)if(const auto* bond=d.bond(item.owner)){
                const auto v=d.atom(bond->b)->p-d.atom(bond->a)->p;const double length=v.length();
                if(length>1e-9){Vec center{};for(auto q:item.points)center=center+q;center=center*(1./item.points.size());const Vec normal{-v.y/length,v.x/length};
                    for(auto& q:item.points){const auto offset=q-center;q=q+normal*((offset.x*normal.x+offset.y*normal.y)*(found->second-1));}}
            }
        }
    for(auto& item:s.items)if(const auto found=graphWeights.find(item.owner);found!=graphWeights.end()){
        if(item.type==Primitive::Text)item.textStrokeWidth=item.fontSize*.05*(found->second-1);
        else if(item.type!=Primitive::Image){
            item.width*=found->second;
            if(item.type==Primitive::Polygon)if(const auto* bond=d.bond(item.owner)){
                const auto v=d.atom(bond->b)->p-d.atom(bond->a)->p;const double length=v.length();
                if(length>1e-9){Vec center{};for(auto q:item.points)center=center+q;center=center*(1./item.points.size());const Vec normal{-v.y/length,v.x/length};
                    for(auto& q:item.points){const auto offset=q-center;q=q+normal*((offset.x*normal.x+offset.y*normal.y)*(found->second-1));}}
            }
        }
    }
    double l=1e20,t=1e20,r=-1e20,b=-1e20;
    for(auto& p:s.items){double pl=1e20,pt=1e20,pr=-1e20,pb=-1e20;for(auto q:p.points){double pad=p.type==Primitive::Text?std::max(0.,p.textStrokeWidth/2):p.width/2;pl=std::min(pl,q.x-pad);pt=std::min(pt,q.y-pad);pr=std::max(pr,q.x+pad);pb=std::max(pb,q.y+pad);if(p.type==Primitive::Text){pr=std::max(pr,q.x+textWidth(measure,p.text,p.fontSize,p.font,p.face));pb=std::max(pb,q.y+p.fontSize*1.3*(1+std::count(p.text.begin(),p.text.end(),'\n')));if(p.rotation==-90){pr=q.x+p.fontSize*1.3;pt=q.y-textWidth(measure,p.text,p.fontSize,p.font,p.face);pb=q.y;}}}
        if(p.points.empty())continue;l=std::min(l,pl);t=std::min(t,pt);r=std::max(r,pr);b=std::max(b,pb);auto it=s.objectBounds.find(p.owner);if(it==s.objectBounds.end())s.objectBounds[p.owner]={pl,pt,pr-pl,pb-pt};else{auto& v=it->second;double x=std::min(v.x,pl),y=std::min(v.y,pt);v={x,y,std::max(v.x+v.w,pr)-x,std::max(v.y+v.h,pb)-y};}}
    s.bounds=s.items.empty()?Rect{0,0,100,100}:Rect{l-6,t-6,r-l+12,b-t+12};return s;
}
static std::string escape(const std::string& v){std::string s;for(char c:v){if(c=='&')s+="&amp;";else if(c=='<')s+="&lt;";else if(c=='\"')s+="&quot;";else s+=c;}return s;}
std::string hexToBase64(const std::string& hex){static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};std::string bytes;for(size_t i=0;i+1<hex.size();){if(digit(hex[i])<0){i++;continue;}int hi=digit(hex[i++]),lo=digit(hex[i++]);if(lo<0)return {};bytes+=char(hi*16+lo);}std::string out;for(size_t i=0;i<bytes.size();i+=3){unsigned n=unsigned(static_cast<unsigned char>(bytes[i]))<<16;if(i+1<bytes.size())n|=unsigned(static_cast<unsigned char>(bytes[i+1]))<<8;if(i+2<bytes.size())n|=static_cast<unsigned char>(bytes[i+2]);out+=alphabet[(n>>18)&63];out+=alphabet[(n>>12)&63];out+=i+1<bytes.size()?alphabet[(n>>6)&63]:'=';out+=i+2<bytes.size()?alphabet[n&63]:'=';}return out;}
PublicationReport publicationCheck(const Scene& s,double widthPt){
    if(!std::isfinite(widthPt)||widthPt<=0||!std::isfinite(s.bounds.w)||s.bounds.w<=0||!std::isfinite(s.bounds.h)||s.bounds.h<=0)
        throw std::runtime_error("Invalid publication width or scene bounds");
    PublicationReport result;
    result.widthPt=widthPt;
    result.scale=widthPt/s.bounds.w;
    result.heightPt=s.bounds.h*result.scale;
    int smallFonts=0,thinLines=0,rasterImages=0;
    for(const auto& item:s.items){
        if(item.type==Primitive::Text&&!item.text.empty()&&item.fontSize*result.scale<4.5)++smallFonts;
        if((item.type==Primitive::Line||item.type==Primitive::Bezier||
            item.type==Primitive::Ellipse||item.type==Primitive::Box)&&
           item.width>0&&item.width*result.scale<0.5)++thinLines;
        if(item.type==Primitive::Image)++rasterImages;
    }
    if(result.heightPt>660.)
        result.warnings.push_back("Height exceeds JACS 660 pt maximum, including the caption.");
    if(smallFonts)
        result.warnings.push_back(std::to_string(smallFonts)+" text items fall below 4.5 pt at the selected output width.");
    if(thinLines)
        result.warnings.push_back(std::to_string(thinLines)+" strokes fall below 0.5 pt at the selected output width.");
    if(rasterImages)
        result.warnings.push_back(std::to_string(rasterImages)+" embedded raster images require manual DPI verification (color 300, grayscale 600, line art 1200).");
    return result;
}
std::string toSvg(const Scene& s,double widthPt){std::ostringstream o;o.imbue(std::locale::classic());o<<std::fixed<<std::setprecision(4);auto r=s.bounds;
    const double printedWidth=widthPt>0?publicationCheck(s,widthPt).widthPt:r.w;
    const double printedHeight=widthPt>0?r.h*printedWidth/r.w:r.h;
    o<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""<<printedWidth<<"pt\" height=\""<<printedHeight<<"pt\" viewBox=\""<<r.x<<' '<<r.y<<' '<<r.w<<' '<<r.h<<"\">\n<g stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
    int inkFilter=0;
    for(auto& p:s.items){if(p.points.empty())continue;auto a=p.points[0];if(p.opacity!=255)o<<"<g opacity=\""<<double(p.opacity)/255<<"\">";std::string dash=p.dash==1?" stroke-dasharray=\"4 3\"":p.dash==2?" stroke-dasharray=\"1 2\"":"";
        if(p.type==Primitive::Line||p.type==Primitive::Bezier){o<<"<path fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\""<<((p.flatEnd||p.flatStart)?" stroke-linecap=\"butt\"":"")<<dash<<" d=\"M "<<a.x<<' '<<a.y;
            if(p.type==Primitive::Bezier){for(size_t i=1;i+2<p.points.size();i+=3)o<<" C "<<p.points[i].x<<' '<<p.points[i].y<<' '<<p.points[i+1].x<<' '<<p.points[i+1].y<<' '<<p.points[i+2].x<<' '<<p.points[i+2].y;}
            else for(size_t i=1;i<p.points.size();i++)o<<" L "<<p.points[i].x<<' '<<p.points[i].y;if(p.closed)o<<" Z";o<<"\"/>\n";}
        else if(p.type==Primitive::Polygon){o<<"<polygon fill=\""<<p.color<<"\" points=\"";for(auto v:p.points)o<<v.x<<','<<v.y<<' ';o<<"\"/>\n";}
        else if(p.type==Primitive::Text){
            std::string ink;
            if(p.textStrokeWidth>0){std::ostringstream attr;attr.imbue(std::locale::classic());attr<<" stroke=\""<<p.color<<"\" stroke-width=\""<<p.textStrokeWidth<<"\" paint-order=\"stroke fill\"";ink=attr.str();}
            else if(p.textStrokeWidth<0){const auto name="ink"+std::to_string(++inkFilter);o<<"<defs><filter id=\""<<name<<"\" x=\"-20%\" y=\"-20%\" width=\"140%\" height=\"140%\"><feMorphology operator=\"erode\" radius=\""<<-p.textStrokeWidth/2<<"\"/></filter></defs>";ink=" filter=\"url(#"+name+")\"";}
            o<<"<text"<<ink<<" transform=\"rotate("<<p.rotation<<" "<<a.x<<" "<<a.y<<")\" fill=\""<<p.color<<"\" x=\""<<a.x<<"\" y=\""<<a.y+p.fontSize*.91<<"\" font-family=\""<<escape(p.font)<<"\" font-size=\""<<p.fontSize<<"\""<<(p.face&1?" font-weight=\"bold\"":"")<<(p.face&2?" font-style=\"italic\"":"")<<(p.face&4?" text-decoration=\"underline\"":"")<<">";std::istringstream lines(p.text);std::string line;int row=0;while(std::getline(lines,line)){o<<"<tspan x=\""<<a.x<<"\" dy=\""<<(row++?p.fontSize*1.3:0)<<"\">"<<escape(line)<<"</tspan>";}o<<"</text>\n";}
        else if(p.points.size()>=2){Vec z=p.points[1];double x=std::min(a.x,z.x),y=std::min(a.y,z.y),w=std::abs(z.x-a.x),h=std::abs(z.y-a.y);
            if(p.type==Primitive::Image)o<<"<image x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<w<<"\" height=\""<<h<<"\" href=\"data:"<<p.mime<<";base64,"<<hexToBase64(p.imageHex)<<"\"/>\n";
            else if(p.type==Primitive::Ellipse)o<<"<ellipse fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\""<<dash<<" cx=\""<<x+w/2<<"\" cy=\""<<y+h/2<<"\" rx=\""<<w/2<<"\" ry=\""<<h/2<<"\"/>\n";
            else o<<"<rect fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\""<<dash<<" x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<w<<"\" height=\""<<h<<"\"/>\n";}if(p.opacity!=255)o<<"</g>\n";}
    o<<"</g>\n</svg>\n";return o.str();
}
}
