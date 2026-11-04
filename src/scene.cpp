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
struct ScriptRun {std::string text;int script=0;};
static std::vector<ScriptRun> scriptRuns(const std::string& input){
    auto value=chemicalScripts(input);
    std::vector<ScriptRun> runs;std::string normal;
    for(size_t i=0;i<value.size();){
        if((value[i]=='^'||value[i]=='_')&&i+1<value.size()&&value[i+1]=='{'){
            auto end=value.find('}',i+2);if(end!=std::string::npos){if(!normal.empty()){runs.push_back({normal,0});normal.clear();}runs.push_back({value.substr(i+2,end-i-2),value[i]=='^'?1:-1});i=end+1;continue;}
        }normal+=value[i++];
    }if(!normal.empty())runs.push_back({normal,0});return runs;
}
static double textWidth(const Measure& m,const std::string& s,double size,const std::string& font="Arial",int face=1){
    double width=0;std::istringstream lines(s);std::string line;while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();double row=0;for(auto& run:scriptRuns(line)){double fs=size*(run.script?.7:1);size_t chars=0;for(unsigned char c:run.text)if((c&0xc0)!=0x80)chars++;row+=m?m(run.text,fs,font,face):chars*fs*(face&1?.62:.56);}width=std::max(width,row);}return width;
}
static void emitText(std::vector<Primitive>& items,Primitive base,Vec at,bool center,const Measure& measure){
    auto value=base.text;std::istringstream rows(value);std::string row;size_t line=0;
    while(std::getline(rows,row)){
        if(!row.empty()&&row.back()=='\r')row.pop_back();double x=at.x-(center?textWidth(measure,row,base.fontSize,base.font,base.face)/2:0),y=at.y+line++*base.fontSize*1.3;
        for(auto& run:scriptRuns(row)){auto t=base;t.text=run.text;t.fontSize*=run.script?.7:1;t.points={{x,y+(run.script==1?-base.fontSize*.35:run.script==-1?base.fontSize*.3:0)}};items.push_back(t);x+=textWidth(measure,run.text,t.fontSize,t.font,t.face);}
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
        owner=a.id; bool isolated=true;Vec neighbors;for(auto& b:d.bonds)if(b.a==a.id||b.b==a.id){isolated=false;neighbors=neighbors+d.atom(b.a==a.id?b.b:b.a)->p-a.p;}
        if(!a.labelRuns.empty()&&!a.label.empty()){
            Vec cursor=a.p+a.labelOffset;double base=d.style.fontSize*.91;double l=cursor.x,t=cursor.y,r=cursor.x,b=cursor.y;
            for(auto& run:a.labelRuns){Primitive p;p.type=Primitive::Text;p.owner=a.id;p.text=run.text;p.font=run.font;p.fontSize=run.size;p.face=run.face;p.color=run.color;int script=run.face&96;double offset=script==32?run.size*.4:script==64?-run.size*.4:0;p.points={{cursor.x,cursor.y+base-run.size*.91+offset}};labels.push_back(p);r=std::max(r,cursor.x+textWidth(measure,run.text,run.size,run.font,run.face));t=std::min(t,p.points[0].y);b=std::max(b,p.points[0].y+run.size);cursor.x+=textWidth(measure,run.text,run.size,run.font,run.face);}
            boxes[a.id]={l-d.style.margin,t-d.style.margin,r-l+2*d.style.margin,b-t+2*d.style.margin};continue;
        }
        if(a.element==6&&!isolated&&a.label.empty()&&!a.charge&&!a.isotope&&!a.radical)continue;
        std::string el=a.label.empty()?symbol(a.element):a.label;double fs=d.style.fontSize,w=textWidth(measure,el,fs,"Arial",d.style.labelFace);
        double anchor=w/2;
        if(!a.label.empty()){
            auto formatted=chemicalScripts(el);auto script=formatted.find("^{");auto sym=symbol(a.element);auto pos=formatted.find(sym);
            if(script!=std::string::npos&&pos!=std::string::npos&&pos<script)anchor=textWidth(measure,formatted.substr(0,pos),fs,"Arial",d.style.labelFace)+textWidth(measure,sym,fs,"Arial",d.style.labelFace)/2;
        }
        double x=a.p.x-anchor,y=a.p.y-fs/2;boxes[a.id]={x-d.style.margin,y-d.style.margin,w+2*d.style.margin,fs+2*d.style.margin};
        Primitive t;t.owner=a.id;t.type=Primitive::Text;t.points={{x,y}};t.fontSize=fs;t.face=d.style.labelFace;t.color=a.color;t.text=el;emitText(labels,t,{x,y},false,measure);
        if(chemicalScripts(el).find("^{")!=std::string::npos){boxes[a.id].y-=fs*.35;boxes[a.id].h+=fs*.35;}
        // Only neutral common heteroatoms get automatic H labels in v0.1.
        int h=a.label.empty()&&(a.element==7||a.element==8||a.element==16)?d.hydrogens(a.id):0;
        if(h){double hw=textWidth(measure,"H",fs),nw=h>1?textWidth(measure,std::to_string(h),fs*.7):0;
            bool leftBond=false,rightBond=false;for(auto& bond:d.bonds)if(bond.a==a.id||bond.b==a.id){Vec v=d.atom(bond.a==a.id?bond.b:bond.a)->p-a.p;if(v.x>v.length()*.3)rightBond=true;if(v.x<-v.length()*.3)leftBond=true;}
            bool vertical=leftBond&&rightBond;double hx=vertical?a.p.x-(hw+nw)/2:neighbors.x>1?x-hw-nw:x+w;double hy=vertical?y+(neighbors.y>0?-fs-1:fs+1):y;Primitive ht=t;ht.text="H";ht.points={{hx,hy}};labels.push_back(ht);
            if(h>1){ht.text=std::to_string(h);ht.fontSize=fs*.7;ht.points={{hx+hw,hy+fs*.4}};labels.push_back(ht);}
            Rect& box=boxes[a.id];double left=std::min(box.x,hx-d.style.margin),right=std::max(box.x+box.w,hx+hw+nw+d.style.margin);box.x=left;box.w=right-left;if(vertical){double top=std::min(box.y,hy-d.style.margin),bottom=std::max(box.y+box.h,hy+fs+d.style.margin);box.y=top;box.h=bottom-top;}
        }
        if(a.label.empty()&&a.isotope){Primitive isotope=t;isotope.fontSize=fs*.65;isotope.text=std::to_string(a.isotope);double iw=textWidth(measure,isotope.text,isotope.fontSize);isotope.points={{x-iw,y-fs*.3}};labels.push_back(isotope);boxes[a.id].x-=iw;boxes[a.id].w+=iw;}
    }
    // Place charges and radical dots independently of custom or imported atom labels.
    for(const auto& a:d.atoms)if((a.charge&&!atomLabelHasCharge(a))||a.radical){
        double fs=d.style.fontSize;auto box=boxes.at(a.id);
        Primitive mark;mark.type=Primitive::Text;mark.owner=a.id;mark.textRole=13;mark.fontSize=fs*.7;mark.face=d.style.labelFace;mark.color=a.color;
        int charge=atomLabelHasCharge(a)?0:a.charge;
        mark.text=(std::abs(charge)>1?std::to_string(std::abs(charge)):"")+(charge>0?"+":charge<0?"\xe2\x88\x92":"")+(a.radical==1?"\xe2\x80\xa2\xe2\x80\xa2":a.radical==3?"\xe2\x80\xa2\xe2\x80\xa2":a.radical?"\xe2\x80\xa2":"");
        double width=textWidth(measure,mark.text,mark.fontSize,"Arial",mark.face),height=mark.fontSize;
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
    // Clip against ink spans, not the empty space underneath oxidation superscripts.
    std::map<int,std::vector<Rect>> inkBoxes;
    for(auto& t:labels)if(t.type==Primitive::Text&&!t.points.empty()&&t.textRole!=13){double m=d.style.margin;inkBoxes[t.owner].push_back({t.points[0].x-m,t.points[0].y-m,textWidth(measure,t.text,t.fontSize,t.font,t.face)+2*m,t.fontSize+2*m});}
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
        owner=b.id;strokeColor=b.color;double bondWidth=b.customDisplay=="Bold"?d.style.boldWidth:d.style.lineWidth; Vec origA=d.atom(b.a)->p,origB=d.atom(b.b)->p,a=clip(b.a,origA,origB),z=clip(b.b,origB,origA),v=z-a;double len=v.length();
        if(len<.2 || (a-origA).length()+(z-origB).length()>=(origB-origA).length())continue;
        Vec u=v*(1/len),n={-u.y,u.x};double gap=d.style.bondLength*d.style.bondSpacing;
        // BoldWidth is the full width, also at the broad end of a wedge.
        // Keep short, label-clipped coordination bonds from becoming squat triangles.
        double wedgeWidth=std::min(d.style.boldWidth,len*.35);
        if(b.kind==BondKind::Wedge){Primitive p;p.owner=owner;p.color=b.color;p.type=Primitive::Polygon;p.points={a,z+n*(wedgeWidth/2),z-n*(wedgeWidth/2)};s.items.push_back(p);}
        else if(b.kind==BondKind::Hash){
            double ink=std::min(d.style.lineWidth*.6,len/6);
            int count=std::max(2,int(len/std::max(d.style.hashSpacing,ink*2.2)));
            for(int i=0;i<=count;i++){double t=double(i)/count;Vec c=a+v*((ink/2+(len-ink)*t)/len);double w=std::max(ink*.4,wedgeWidth*t/2);line(c+n*w,c-n*w,ink);s.items.back().flatStart=s.items.back().flatEnd=true;}}
        else if(b.kind==BondKind::Double){auto path=ringPath(d,b);
            if(!path.empty()&&b.doubleSide==0){Vec center;for(int id:path)center=center+d.atom(id)->p;center=center*(1./path.size());double side=((center-a).x*n.x+(center-a).y*n.y)>0?1:-1;
                line(a,z,bondWidth);double inset=std::min(len*.25,gap*.65+std::max(0.,bondWidth-d.style.lineWidth)/2);line(a+u*inset+n*(side*gap),z-u*inset+n*(side*gap),d.style.lineWidth);}
            else if(b.doubleSide){line(a,z,bondWidth);line(a+n*(gap*b.doubleSide),z+n*(gap*b.doubleSide),d.style.lineWidth);}else{line(a+n*(gap/2),z+n*(gap/2),bondWidth);line(a-n*(gap/2),z-n*(gap/2),d.style.lineWidth);}}
        else if(b.kind==BondKind::Triple){line(a,z,bondWidth);line(a+n*gap,z+n*gap,bondWidth);line(a-n*gap,z-n*gap,bondWidth);}
        else if(b.kind==BondKind::Wavy){Vec last=a;for(int i=1;i<=24;i++){double t=double(i)/24;Vec q=a+v*t+n*(std::sin(t*pi*8)*.8);line(last,q,bondWidth);last=q;}}
        else if(b.kind==BondKind::Dative){line(a,z,bondWidth);line(z,z-u*3+n*1.4,bondWidth);line(z,z-u*3-n*1.4,bondWidth);}
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
        else line(a,z,b.kind==BondKind::Bold?d.style.boldWidth:bondWidth,b.kind==BondKind::Dash?1:0);
    }
    strokeColor="#000000";s.items.insert(s.items.end(),labels.begin(),labels.end());
    int arrowIndex=-1;std::vector<int> hiddenArrowIndices;
    auto arrow=[&](Vec a,Vec z,const std::string& color,double width,int head,double headScale=1.){if(arrowIndex>=0&&std::find(hiddenArrowIndices.begin(),hiddenArrowIndices.end(),arrowIndex)!=hiddenArrowIndices.end())return;Vec v=z-a;double len=v.length();if(len<1e-6)return;Vec u=v*(1/len),n{-u.y,u.x};Primitive p;p.reactionArrow=arrowIndex;p.owner=owner;p.points={a,head?z-u*(4.8*headScale):z};p.width=width;p.color=color;s.items.push_back(p);if(head){p.type=Primitive::Polygon;p.points=head==2?std::vector<Vec>{z,z-u*(5*headScale)+n*(2*headScale)}:std::vector<Vec>{z,z-u*(5*headScale)+n*(2*headScale),z-u*(5*headScale)-n*(2*headScale)};s.items.push_back(p);}};
    auto addText=[&](std::string value,Vec p,double size=9,std::string color="#000000",bool center=false,int role=0,int index=-1){Primitive t;t.type=Primitive::Text;t.owner=owner;t.textRole=role;t.textIndex=index;t.text=value;t.fontSize=size*d.style.fontSize/10.;t.face=d.style.labelFace;t.color=color;for(unsigned char c:value)if(c>=0xe3)t.font="Malgun Gothic";emitText(s.items,t,p,center,measure);};
    for(auto& o:d.objects){owner=o.id;Primitive p;p.owner=o.id;p.points=o.points;p.width=o.width;p.color=o.color;p.filled=o.filled;p.fontSize=o.fontSize;p.font=o.font;p.dash=o.dash;p.face=o.face;p.closed=o.closed;p.text=o.text;
        if(o.points.empty())continue;
        if(o.head&&(o.kind==ObjectKind::Line||o.kind==ObjectKind::Curve)){Object routed=o;routed.points=routeArrow(d,o,boxes);s.arrowPaths[o.id]=routed.points;auto pieces=arrowPrimitives(routed);s.items.insert(s.items.end(),pieces.begin(),pieces.end());appendArrowCross(s.items,pieces,o.cross,o.id);continue;}
        switch(o.kind){case ObjectKind::Line:p.type=Primitive::Line;break;case ObjectKind::Curve:p.type=Primitive::Bezier;break;case ObjectKind::Text:p.type=Primitive::Text;break;case ObjectKind::Ellipse:p.type=Primitive::Ellipse;break;case ObjectKind::Rectangle:p.type=Primitive::Box;break;case ObjectKind::Image:p.type=Primitive::Image;p.imageHex=o.imageHex;p.mime=o.mime;break;}
        if(o.kind==ObjectKind::Text&&!o.runs.empty()){Vec cursor=o.points[0];double baseline=cursor.y+o.fontSize*.91;for(auto& run:o.runs){Primitive t=p;t.text=run.text;t.fontSize=run.size;t.color=run.color;t.font=run.font;t.face=run.face;int script=run.face&96;double offset=script==32?run.size*.45:script==64?-run.size*.4:0;t.points={{cursor.x,baseline-run.size*.91+offset}};s.items.push_back(t);cursor.x+=textWidth(measure,run.text,run.size,run.font,run.face);}}
        else if(o.kind==ObjectKind::Text){emitText(s.items,p,o.points[0],o.centerText,measure);}
        else s.items.push_back(p);
        if(o.head&&o.points.size()>=2){Vec z=o.points.back(),a=o.points[o.points.size()-2];Vec v=z-a;double len=v.length();if(len>.001){Vec u=v*(1/len),n{-u.y,u.x};Primitive head;head.owner=o.id;head.type=Primitive::Polygon;head.color=o.color;head.points=o.head==2?std::vector<Vec>{z,z-u*5+n*2}:std::vector<Vec>{z,z-u*5+n*2,z-u*5-n*2};s.items.push_back(head);}}
    }
    auto addEnergy=[&](const std::string& value,Vec at,int role,int index,double available){
        std::vector<SpinEnergy> energies;std::vector<std::string> parts;
        try{energies=reactionEnergies(value);}catch(...){addText(value,at,8,"#000000",true,role,index);return;}
        for(auto& e:energies){std::ostringstream label;if(e.spin!=0&&e.spin!=1&&e.spin!=2)label<<"S="<<e.spin<<": ";label<<e.value;parts.push_back(e.value.empty()?"":label.str());}
        size_t count=parts.size();while(count&&parts[count-1].empty())count--;
        if(!count){addText("",at,8,"#000000",true,role,index);return;}
        double size=8;for(auto& reaction:d.reactions)if(reaction.id==owner)size=mechanismFontSize(reaction,d.style)*10/d.style.fontSize;
        double total=0;auto width=[&](const std::string& text){return textWidth(measure,text,size*d.style.fontSize/10.,"Arial",d.style.labelFace);};
        auto gap=[&](){return size*d.style.fontSize/10.*.4;};
        for(size_t i=0;i<count;i++)total+=width(parts[i])+(i?width("/")+2*gap():0);
        if(total>available&&available>0){size*=available/total;total=available;}
        double cursor=at.x-total/2;
        for(size_t i=0;i<count;i++){
            if(i){cursor+=gap();addText("/",{cursor,at.y},size,"#000000",false,role,index);cursor+=width("/")+gap();}
            if(!parts[i].empty()){addText(parts[i],{cursor,at.y},size,energies[i].spin==0?"#000000":energies[i].spin==1?"#ff0000":energies[i].spin==2?"#0000ff":"#555555",false,role,index);cursor+=width(parts[i]);}
        }
    };
    auto transitionLabels=[&](const Reaction& reaction,size_t i,Vec mid,double aboveY,double belowY){
        auto caption=transitionCaption(i<reaction.above.size()?reaction.above[i]:"");
        double font=mechanismFontSize(reaction,d.style),gap=reaction.spacing-reaction.slotWidth-2*mechanismArrowMargin(reaction,d.style);
        auto description=caption.description;
        if(i<reaction.above.size())addText(description,mid+Vec{0,aboveY-font*1.3*std::count(description.begin(),description.end(),'\n')},font*10/d.style.fontSize,"#000000",true,2,int(i));
        if(!caption.name.empty())addText(caption.name,mid+Vec{0,belowY},font*10/d.style.fontSize,"#000000",true,11,int(i));
        if(i<reaction.below.size())addEnergy(reaction.below[i],mid+Vec{0,belowY+(caption.name.empty()?0:font*1.5*(1+std::count(caption.name.begin(),caption.name.end(),'\n')))},3,int(i),gap);
    };
    for(auto& r:d.reactions){hiddenArrowIndices=r.hiddenArrows;owner=r.id;double font=mechanismFontSize(r,d.style),factor=font/d.style.fontSize;
        if(r.cyclic)addText(r.centerLabel,r.p+Vec{0,-font*.65},10*factor,"#000000",true,10);
        if(!r.cyclic&&!r.unit.empty()&&!r.slots.empty()){double right=-1e20,top=1e20;for(size_t j=0;j<r.slots.size();j++){Vec pos=reactionSlot(r,j);right=std::max(right,pos.x);top=std::min(top,pos.y);}addText("("+r.unit+")",{right+r.slotWidth/2,top-r.slotHeight/2-font*2},10*factor,"#000000",true);}
        for(size_t i=0;i<r.slots.size();i++){arrowIndex=int(i);Vec c=reactionSlot(r,i);addText(r.slots[i].label,c+Vec{0,r.slotHeight/2+font*.6},10*factor,"#000000",true,1,int(i));
            if(r.cyclic||!r.slots[i].energy.empty())addEnergy(r.slots[i].energy,c+Vec{0,r.slotHeight/2+font*.6+font*1.5*(1+std::count(r.slots[i].label.begin(),r.slots[i].label.end(),'\n'))},9,int(i),r.spacing-20);
            if(r.cyclic){
                bool hidden=std::find(r.hiddenArrows.begin(),r.hiddenArrows.end(),int(i))!=r.hiddenArrows.end();
                double radius=(c-r.p).length(),step=2*pi/r.slots.size(),start=-pi/2+step*i,trim=std::asin(std::min(.95,(mechanismCycleNodeRadius(r,d.style)+font*.5)/radius));
                auto arc=[&](double rad,double begin,double end){double bend=4./3*std::tan((end-begin)/4);Vec a=r.p+Vec{std::cos(begin),std::sin(begin)}*rad,z=r.p+Vec{std::cos(end),std::sin(end)}*rad,t0{-std::sin(begin),std::cos(begin)},t1{-std::sin(end),std::cos(end)};double direction=end>begin?1:-1;Vec u=t1*direction,n{-u.y,u.x};Primitive curve;curve.reactionArrow=int(i);curve.owner=owner;curve.type=Primitive::Bezier;curve.width=std::max(1.1,font*.085);double headSize=std::max(6.,font*.5);curve.points={a,a+t0*(bend*rad),z-t1*(bend*rad),z-u*(headSize*.96)};s.items.push_back(curve);Primitive head;head.reactionArrow=int(i);head.owner=owner;head.type=Primitive::Polygon;head.points={z,z-u*headSize+n*(headSize*.4),z-u*headSize-n*(headSize*.4)};s.items.push_back(head);};
                if(!hidden){arc(radius-(r.arrowType==1?2:0),start+trim,start+step-trim);if(r.arrowType==1)arc(radius+2,start+step-trim,start+trim);}
                Vec mid=mechanismCycleTransitionPosition(r,i,d.style,radius);
                transitionLabels(r,i,mid,-font*1.7,font*.65);continue;
            }
            if(!r.links.empty())continue;
            if(i+1<r.slots.size()){Vec next=reactionSlot(r,(i+1)%r.slots.size()),v=next-c;double len=v.length();if(len<1)continue;Vec u=v*(1/len);
                if(r.vertical){
                    double margin=mechanismArrowMargin(r,d.style),weight=std::max(1.1,font*.085),head=std::max(1.25,font/10);
                    Vec a=c+Vec{0,r.slotHeight/2+mechanismCaptionHeight(r,d.style)+margin},b=next-Vec{0,r.slotHeight/2+margin};
                    if(r.arrowType==1){arrow(a+Vec{-2,0},b+Vec{-2,0},"#000000",weight,1,head);arrow(b+Vec{2,0},a+Vec{2,0},"#000000",weight,1,head);}else arrow(a,b,"#000000",weight,1,head);
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
                    double outside=c.x+direction*(edge+font*2),weight=std::max(1.1,font*.085),head=std::max(1.25,font/10);
                    Vec a=c+Vec{direction*edge,0},b=next+Vec{direction*edge,0};
                    auto bent=[&](Vec from,Vec to,double x,double dy){from.y+=dy;to.y+=dy;Vec p{x,from.y},q{x,to.y};arrow(from,p,"#000000",weight,0);arrow(p,q,"#000000",weight,0);arrow(q,to,"#000000",weight,1,head);};
                    if(r.arrowType==1){bent(a,b,outside+direction*2,-2);bent(b,a,outside-direction*2,2);}else bent(a,b,outside,0);
                    auto caption=transitionCaption(i<r.above.size()?r.above[i]:"");
                    double captionWidth=std::max(textWidth(measure,caption.description,font),textWidth(measure,caption.name,font));
                    if(i<r.below.size())captionWidth=std::max(captionWidth,textWidth(measure,r.below[i],font));
                    Vec mid{outside+direction*(captionWidth/2+font),(c.y+next.y)/2};
                    transitionLabels(r,i,mid,-font*1.7,font*.65);continue;
                }
                double trim=std::min(len*.49,r.slotWidth/2+mechanismArrowMargin(r,d.style));Vec a=c+u*trim,b=next-u*trim,n{-u.y,u.x};
                double weight=std::max(1.1,font*.085),head=std::max(1.25,font/10);
                if(r.arrowType==1){arrow(a-n*2,b-n*2,"#000000",weight,1,head);arrow(b+n*2,a+n*2,"#000000",weight,1,head);}else arrow(a,b,"#000000",weight,1,head);
                Vec mid=(a+b)*.5;transitionLabels(r,i,mid,-font*1.7,font*.65);
            }
        }
        // External branches have no invented reactant frame: one shared vertical
        // spine joins their horizontal arrow tails.
        bool external=false;double stemX=0,top=1e30,bottom=-1e30;
        for(size_t e=0;e<r.links.size();e++)if(r.links[e].from<0){auto path=mechanismLinkPath(r,e,d.style);external=true;stemX=path[0].x;top=std::min(top,path[0].y);bottom=std::max(bottom,path[0].y);}
        if(external){arrowIndex=-1;arrow({stemX,top},{stemX,bottom},"#000000",std::max(1.1,font*.085),0);}
        for(size_t e=0;e<r.links.size();e++){
            arrowIndex=int(e);auto path=mechanismLinkPath(r,e,d.style);double weight=std::max(1.1,font*.085),head=std::max(1.25,font/10);
            auto draw=[&](std::vector<Vec> points,double offset){for(auto& p:points)p=p+Vec{offset,offset};for(size_t j=1;j<points.size();j++)arrow(points[j-1],points[j],"#000000",weight,j+1==points.size()?1:0,head);};
            draw(path,r.arrowType==1?-2:0);if(r.arrowType==1){std::reverse(path.begin(),path.end());draw(path,2);}
            transitionLabels(r,e,mechanismLinkCaption(r,e,d.style),-font*1.7,font*.65);
        }
        arrowIndex=-1;
        for(auto& mark:r.crosses){std::vector<Primitive> shaft;for(auto& p:s.items)if(p.owner==r.id&&p.reactionArrow==mark.index)shaft.push_back(p);if(r.arrowType==1){auto firstHead=std::find_if(shaft.begin(),shaft.end(),[](const Primitive& p){return p.type==Primitive::Polygon;});if(firstHead!=shaft.end())shaft.erase(firstHead+1,shaft.end());}appendArrowCross(s.items,shaft,mark,r.id);}
        if(r.cyclic&&!r.unit.empty()&&!r.slots.empty()){
            double right=r.p.x,top=r.p.y;
            for(auto& item:s.items)if(item.owner==r.id&&item.type==Primitive::Text&&!item.points.empty()){right=std::max(right,item.points[0].x+textWidth(measure,item.text,item.fontSize,item.font,item.face));top=std::min(top,item.points[0].y);}
            addText("("+r.unit+")",{right+font,top-font*2},10*factor,"#000000",false);
        }
    }
    // Axis captions accept x^{2}, H_{2} and Unicode scripts. Each run stays vector text.
    auto axisText=[&](const std::string& value,Vec anchor,double size,const std::string& color,bool vertical,int role=0,int index=-1){
        struct Run{std::string text;int script;};std::vector<Run> runs;std::string normal;
        for(size_t i=0;i<value.size();){if((value[i]=='^'||value[i]=='_')&&i+1<value.size()&&value[i+1]=='{'){if(!normal.empty()){runs.push_back({normal,0});normal.clear();}int script=value[i]=='^'?1:-1;size_t end=value.find('}',i+2);if(end==std::string::npos){normal+=value.substr(i);break;}runs.push_back({value.substr(i+2,end-i-2),script});i=end+1;}else normal+=value[i++];}if(!normal.empty())runs.push_back({normal,0});
        double total=0;for(auto& r:runs)total+=textWidth(measure,r.text,size*(r.script?.7:1),"Arial",d.style.labelFace);double cursor=-total/2;
        for(auto& r:runs){double fs=size*(r.script?.7:1),offset=r.script==1?-size*.4:r.script==-1?size*.35:0;Primitive t;t.type=Primitive::Text;t.owner=owner;t.textRole=role;t.textIndex=index;t.text=r.text;for(unsigned char c:r.text)if(c>=0xe3)t.font="Malgun Gothic";t.fontSize=fs;t.face=d.style.labelFace;t.color=color;t.rotation=vertical?-90:0;t.points={vertical?anchor+Vec{offset,-cursor}:anchor+Vec{cursor,offset}};s.items.push_back(t);cursor+=textWidth(measure,r.text,fs,"Arial",t.face);}
    };
    auto scriptWidth=[&](const std::string& text,double size){
        double width=0;for(size_t i=0;i<text.size();){
            if((text[i]=='^'||text[i]=='_')&&i+1<text.size()&&text[i+1]=='{'){
                size_t end=text.find('}',i+2);if(end!=std::string::npos){width+=textWidth(measure,text.substr(i+2,end-i-2),size*.7,"Arial",d.style.labelFace);i=end+1;continue;}
            }
            size_t end=i+1;while(end<text.size()&&!(end+1<text.size()&&(text[end]=='^'||text[end]=='_')&&text[end+1]=='{'))end++;
            width+=textWidth(measure,text.substr(i,end-i),size,"Arial",d.style.labelFace);i=end;
        }return width;
    };
    for(auto& plot:d.plots){
        owner=plot.id;auto axis=energyAxis(plot);double low=axis.low,high=axis.high;
        double scale=std::clamp(std::sqrt(plot.width*plot.height/(460.*240.)),.8,2.);
        double tickFont=plot.tickFontSize>0?plot.tickFontSize:11.5*scale*std::pow(plot.axisScale,.2);
        tickFont=std::min(tickFont,std::max(4.,plot.height*.14));
        double axisFont=plot.axisFontSize>0?plot.axisFontSize:tickFont;
        double energyFont=plot.energyLabelFontSize>0?plot.energyLabelFontSize:tickFont;
        double valueFont=plot.valueFontSize>0?plot.valueFontSize:12.5*scale;
        auto widthOf=[&](const std::string& text,double size){return textWidth(measure,text,size,"Arial",d.style.labelFace);};
        std::string yCaption=plot.yLabel+(plot.unit.empty()?"":" ("+plot.unit+")");
        // Requested sizes are upper bounds: shrink only when the physical graph cannot fit them.
        axisFont=std::min(axisFont,std::max(4.,(plot.width-114)/std::max(1.,widthOf(plot.xLabel,1))));
        tickFont=std::min(tickFont,std::max(4.,plot.height*.14));
        valueFont=std::min(valueFont,std::max(4.,plot.height*.18));
        double titleFont=axisFont*1.05,legendFont=tickFont;
        double x0=plot.p.x+50,w=std::max(100.,plot.width-54);
        double y0=plot.p.y+plot.height-std::max(40.,tickFont*1.3+axisFont*1.4+16);
        double axisY=y0;s.plotBaselines[owner]=axisY;
        struct StageLayout {std::string text;double font=0,height=0;int lane=0;};
        std::vector<StageLayout> stageLayouts;std::vector<std::vector<size_t>> lanes;std::vector<double> laneHeights,laneOffsets;
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
        for(size_t i=0;i<plot.stages.size();i++){
            const auto& stage=plot.stages[i];double span=w*(stage.end-stage.start)/100;
            // Preserve readable type; use multiple lines instead of shrinking all stages to the longest label.
            double font=stage.fontSize>0?stage.fontSize:tickFont;
            font=std::min(font,span*.65); // Only physically tiny intervals constrain individual glyph size.
            double available=std::max(1.,span-2*std::min(font*1.5,span*.16));
            auto text=wrapStage(stage.label,font,available);double height=font*1.3*(1+std::count(text.begin(),text.end(),'\n'));
            size_t lane=0;for(;lane<lanes.size();lane++){bool clear=true;for(auto j:lanes[lane])if(stage.start<plot.stages[j].end+1&&stage.end+1>plot.stages[j].start)clear=false;if(clear)break;}
            if(lane==lanes.size()){lanes.push_back({});laneHeights.push_back(0);}lanes[lane].push_back(i);
            laneHeights[lane]=std::max(laneHeights[lane],height+std::max(12.,font*.8));stageLayouts.push_back({text,font,height,int(lane)});
        }
        double stageBand=0;for(double height:laneHeights){laneOffsets.push_back(stageBand);stageBand+=height;}
        if(!plot.stages.empty())y0-=stageBand+8;
        double topY=plot.p.y+std::max(25.,valueFont*1.3+12),h=std::max(25.,y0-topY);topY=y0-h;
        energyFont=std::min(energyFont,std::max(4.,h*.95/std::max(1.,widthOf(yCaption,1))));
        auto plotText=[&](const std::string& text,Vec at,double size,const std::string& color="#000000",bool centered=false,int role=0,int index=-1){
            addText(text,at,size*10/d.style.fontSize,color,centered,role,index);
        };
        auto overlaps=[](Rect a,Rect b,double pad=4.){return a.x<b.x+b.w+pad&&a.x+a.w+pad>b.x&&a.y<b.y+b.h+pad&&a.y+a.h+pad>b.y;};
        std::vector<Rect> external;
        for(auto& item:s.items)if(item.owner!=owner&&!item.points.empty()){
            double l=1e20,t=1e20,r=-1e20,b=-1e20;
            for(auto pt:item.points){l=std::min(l,pt.x);t=std::min(t,pt.y);r=std::max(r,pt.x);b=std::max(b,pt.y);}
            if(item.type==Primitive::Text){r=l+textWidth(measure,item.text,item.fontSize,item.font,item.face);b=t+item.fontSize*1.3;}
            external.push_back({l,t,std::max(1.,r-l),std::max(1.,b-t)});
        }
        arrow({x0,axisY},{x0+w+12*plot.axisScale,axisY},"#000000",std::max(.8,d.style.lineWidth*.6)*plot.axisScale,1,plot.axisScale*(3.5/3.0));
        arrow({x0,axisY},{x0,topY-14*plot.axisScale},"#000000",std::max(.8,d.style.lineWidth*.6)*plot.axisScale,1,plot.axisScale*(3.5/3.0));
        for(size_t i=0;i<plot.stages.size();i++){
            const auto& stage=plot.stages[i];double a=x0+w*stage.start/100,b=x0+w*stage.end/100,c=(a+b)/2;
            const auto& layout=stageLayouts[i];double font=layout.font;
            double y=y0+8+laneOffsets[layout.lane]+laneHeights[layout.lane]/2;
            double gap=widthOf(layout.text,font)/2+std::min(font*.35,(b-a)*.04),shaft=std::max(.01,(b-a)/2-gap);
            double weight=std::max(.6,font*.085),headScale=std::min(font*.10,shaft*.65/5);
            arrow({c-gap,y},{a,y},plot.xColor,weight,1,headScale);
            arrow({c+gap,y},{b,y},plot.xColor,weight,1,headScale);
            plotText(layout.text,{c,y-layout.height/2},font,plot.xColor,true,12,int(i));
        }
        std::vector<double> ticks;for(int i=0;i<=axis.intervals;i++)ticks.push_back(axis.firstTick+axis.step*i);
        std::sort(ticks.begin(),ticks.end());ticks.erase(std::unique(ticks.begin(),ticks.end(),[&](double a,double b){return std::abs(a-b)<axis.step*1e-7;}),ticks.end());
        double tickWidth=0;for(double v:ticks)tickWidth=std::max(tickWidth,widthOf(energyTickLabel(v,axis.step),tickFont));
        double yAxisX=x0-tickWidth-18-energyFont*1.35;Rect yAxisBox{yAxisX,y0-h/2-widthOf(yCaption,energyFont)/2,energyFont*1.3,widthOf(yCaption,energyFont)};
        for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto other:external)if(overlaps(yAxisBox,other)){yAxisBox.x=other.x-yAxisBox.w-8;moved=true;}if(!moved)break;}
        axisText(yCaption,{yAxisBox.x,y0-h/2},energyFont,plot.yColor,true);
        // Label regular ticks only; range boundaries never create an extra irregular tick.
        double tickHeight=tickFont*1.3,lastLabel=y0+tickHeight+10;
        for(size_t tick=0;tick<ticks.size();tick++){
            double v=ticks[tick],y=y0-h*(v-low)/(high-low);bool endpoint=tick==0||tick+1==ticks.size();
            if(endpoint||(lastLabel-y>=tickHeight+4&&y-topY>=tickHeight+4)){
                plotText(energyTickLabel(v,axis.step),{x0-10-widthOf(energyTickLabel(v,axis.step),tickFont),y-tickHeight/2},tickFont,plot.yColor);lastLabel=y;
            }
            Primitive mark;mark.owner=owner;mark.points={{x0-3*plot.axisScale,y},{x0,y}};mark.width=.5*plot.axisScale;s.items.push_back(mark);
        }
        std::vector<EnergySeries> paths=plot.series;if(paths.empty()){EnergySeries path;for(auto& st:plot.states)path.values.push_back(st.energy);paths.push_back(path);}
        titleFont=std::min(titleFont,std::max(4.,w/std::max(1.,widthOf(plot.title,1))));
        std::vector<size_t> legendPaths;
        for(size_t k=0;k<paths.size();k++){
            bool hasData=false;for(size_t i=0;i<plot.states.size();i++)if(paths[k].hasValue(i)){hasData=true;break;}
            if(hasData){legendPaths.push_back(k);legendFont=std::min(legendFont,std::max(4.,(w-30)/std::max(1.,widthOf(paths[k].name,1))));}
        }
        // Compact, centered legend rows directly above the plotting area.
        // The default title duplicates the x caption; retain its saved value without printing twice.
        bool showTitle=!plot.title.empty()&&plot.title!=plot.xLabel;
        bool showLegend=legendPaths.size()>1||(!legendPaths.empty()&&paths[legendPaths[0]].name!="Path 1");
        struct LegendRow{std::vector<size_t> paths;double width=0;};std::vector<LegendRow> rows;
        double itemGap=std::max(14.,legendFont),sampleWidth=22,swatchGap=7,rowHeight=legendFont*1.3,rowGap=5;
        if(showLegend)for(size_t k:legendPaths){
            double itemWidth=sampleWidth+swatchGap+widthOf(paths[k].name,legendFont);
            if(rows.empty()||(!rows.back().paths.empty()&&rows.back().width+itemGap+itemWidth>w*.92))rows.push_back({});
            auto& row=rows.back();if(!row.paths.empty())row.width+=itemGap;row.width+=itemWidth;row.paths.push_back(k);
        }
        double legendHeight=rows.empty()?0:rows.size()*rowHeight+(rows.size()-1)*rowGap;
        double legendTop=topY-18-legendHeight,bandWidth=0;for(auto& row:rows)bandWidth=std::max(bandWidth,row.width);
        Rect legendBox{x0+(w-bandWidth)/2,legendTop,bandWidth,legendHeight};
        if(showLegend)for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto box:external)if(overlaps(legendBox,box)){legendBox.y=box.y-legendBox.h-8;moved=true;}if(!moved)break;}
        legendTop=legendBox.y;
        if(showTitle){
            double titleY=(showLegend?legendTop:topY-18)-titleFont*1.3-7;
            Rect titleBox{x0+w/2-widthOf(plot.title,titleFont)/2,titleY,widthOf(plot.title,titleFont),titleFont*1.3};
            for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto box:external)if(overlaps(titleBox,box)){titleBox.y=box.y-titleBox.h-8;moved=true;}if(!moved)break;}
            plotText(plot.title,{x0+w/2,titleBox.y},titleFont,"#000000",true,4);
        }
        for(size_t r=0;r<rows.size();r++){
            double cursor=x0+(w-rows[r].width)/2,y=legendTop+r*(rowHeight+rowGap);
            for(size_t k:rows[r].paths){auto& path=paths[k];double cy=y+rowHeight/2;
                Primitive sample;sample.owner=owner;sample.textRole=7;sample.textIndex=int(k);sample.width=path.width;sample.color=path.color;sample.dash=1;sample.points={{cursor,cy},{cursor+sampleWidth,cy}};s.items.push_back(sample);
                plotText(path.name,{cursor+sampleWidth+swatchGap,y},legendFont,path.color,false,7,int(k));
                cursor+=sampleWidth+swatchGap+widthOf(path.name,legendFont)+itemGap;
            }
        }
        auto pointAt=[&](size_t i,double value){return Vec{x0+50+(w-100)*i/std::max(size_t(1),plot.states.size()-1),y0-h*(value-low)/(high-low)};};
        // All plateaus share one symmetric length; marker and state center never move.
        double stateGap=(w-100)/std::max(size_t(1),plot.states.size()-1),labelWidth=0;
        for(auto& path:paths)for(size_t i=0;i<path.values.size();i++)if(path.hasValue(i)){
            std::ostringstream label;label.imbue(std::locale::classic());label<<std::fixed<<std::setprecision(1)<<path.values[i];
            labelWidth=std::max(labelWidth,widthOf(label.str(),valueFont));
        }
        double half=.75*std::min({45.,stateGap*.30,std::max({stateGap*.20,labelWidth/2+5*scale,12*scale})});
        std::vector<double> leftSpan(plot.states.size(),half),rightSpan(plot.states.size(),half);
        auto visible=[&](const EnergySeries& path,size_t i){return path.hasValue(i)&&path.values[i]>=low-1e-9&&path.values[i]<=high+1e-9;};
        std::vector<Vec> obstacles,levelObstacles;
        auto remember=[&](Vec a,Vec b){int samples=std::clamp(int(std::ceil((b-a).length()/3)),2,60);for(int j=0;j<=samples;j++){Vec pt=a+(b-a)*(double(j)/samples);if(pt.y>=topY&&pt.y<=y0)obstacles.push_back(pt);}};
        auto segment=[&](Vec a,Vec b,const EnergySeries& path,int dash){
            remember(a,b);double dy=b.y-a.y,t0=0,t1=1;
            if(std::abs(dy)<1e-12){if(a.y<topY||a.y>y0)return;}
            else{double u=(topY-a.y)/dy,v=(y0-a.y)/dy;if(u>v)std::swap(u,v);t0=std::max(0.,u);t1=std::min(1.,v);if(t0>t1)return;}
            Primitive part;part.owner=owner;part.width=path.width;part.color=path.color;part.dash=dash;part.points={a+(b-a)*t0,a+(b-a)*t1};s.items.push_back(part);
        };
        for(size_t pathIndex=0;pathIndex<paths.size();pathIndex++){const auto& basePath=paths[pathIndex];if(basePath.values.size()!=plot.states.size())continue;auto path=basePath;
            for(size_t i=0;i<path.values.size();i++){
                if(!path.hasValue(i))continue;Vec a=pointAt(i,path.values[i]);
                path.color=i<basePath.levelColors.size()&&!basePath.levelColors[i].empty()?basePath.levelColors[i]:basePath.color;
                if(visible(path,i))for(int edge: {-1,0,1}){
                    if(edge&&plot.mode<2)continue;
                    Vec pt=a+Vec{edge*half,0};s.plotSnapPoints[owner].push_back({pt,100*(pt.x-x0)/w,int(i),int(pathIndex),edge});
                }
                if((plot.mode==2||plot.mode==3)&&visible(path,i)){
                    segment(a-Vec{leftSpan[i],0},a+Vec{rightSpan[i],0},path,0);
                    for(int j=0;j<=30;j++)levelObstacles.push_back(a+Vec{-half+2*half*j/30.,0});
                }
                if(i+1<path.values.size()&&path.hasValue(i+1)){
                    Vec b=pointAt(i+1,path.values[i+1]);
                    path.color=i<basePath.segmentColors.size()&&!basePath.segmentColors[i].empty()?basePath.segmentColors[i]:basePath.color;
                    if(plot.mode==0){
                        Vec c1{a.x+(b.x-a.x)*.42,a.y},c2{b.x-(b.x-a.x)*.42,b.y},last=a;
                        for(int j=1;j<=60;j++){double t=j/60.,u=1-t;Vec next=a*(u*u*u)+c1*(3*u*u*t)+c2*(3*u*t*t)+b*(t*t*t);remember(last,next);if(!visible(path,i)||!visible(path,i+1))segment(last,next,path,path.dash);last=next;}
                        if(visible(path,i)&&visible(path,i+1)){Primitive curve;curve.type=Primitive::Bezier;curve.owner=owner;curve.width=path.width;curve.color=path.color;curve.dash=path.dash;curve.points={a,c1,c2,b};s.items.push_back(curve);}
                    }else segment(a+Vec{plot.mode>=2?rightSpan[i]:0,0},b-Vec{plot.mode>=2?leftSpan[i+1]:0,0},path,plot.mode==2?1:path.dash);
                }
                if(!visible(path,i))continue;
                path.color=i<basePath.levelColors.size()&&!basePath.levelColors[i].empty()?basePath.levelColors[i]:basePath.color;
                int marker=plot.states[i].marker<0?path.marker:plot.states[i].marker;
                if(marker){
                    double r=path.markerSize;Primitive symbol;symbol.owner=owner;symbol.color=path.color;symbol.width=path.width;symbol.filled=true;
                    if(marker==1){symbol.type=Primitive::Ellipse;symbol.points={a-Vec{r,r},a+Vec{r,r}};}
                    else{symbol.type=Primitive::Polygon;if(marker==2)symbol.points={a+Vec{-r,-r},a+Vec{r,-r},a+Vec{r,r},a+Vec{-r,r}};else if(marker==3)symbol.points={a+Vec{0,-r*1.3},a+Vec{r*1.3,0},a+Vec{0,r*1.3},a+Vec{-r*1.3,0}};else symbol.points={a+Vec{0,-r*1.4},a+Vec{r*1.3,r},a+Vec{-r*1.3,r}};}
                    s.items.push_back(symbol);for(int j=-4;j<=4;j++)obstacles.push_back(a+Vec{j*r/4,0});for(int dx:{-1,0,1})for(int dy:{-1,0,1})levelObstacles.push_back(a+Vec{dx*r,dy*r});
                }
            }
        }
        // State captions can use separate lanes if long names would touch their neighbors.
        std::vector<Rect> stateBoxes;double stateBottom=axisY+10,stateScriptTop=0;
        for(auto& state:plot.states)if(state.label.find("^{")!=std::string::npos)stateScriptTop=-tickFont*.4;
        for(size_t i=0;i<plot.states.size();i++){
            double x=pointAt(i,0).x,tw=scriptWidth(plot.states[i].label,tickFont);
            double scriptTop=stateScriptTop;
            Rect box{x-tw/2,axisY+10,tw,tickFont*1.3-scriptTop};
            for(size_t pass=0;pass<=stateBoxes.size()+external.size();pass++){
                bool moved=false;for(auto other:stateBoxes)if(overlaps(box,other)){box.y=other.y+other.h+5;moved=true;}
                for(auto other:external)if(overlaps(box,other)){box.y=other.y+other.h+5;moved=true;}
                if(!moved)break;
            }
            stateBoxes.push_back(box);stateBottom=std::max(stateBottom,box.y+box.h);axisText(plot.states[i].label,{x,box.y-scriptTop},tickFont,"#000000",false,6,int(i));
        }
        Rect xAxisBox{x0+w/2-widthOf(plot.xLabel,axisFont)/2,stateBottom+10,widthOf(plot.xLabel,axisFont),axisFont*1.3};
        for(size_t pass=0;pass<=external.size();pass++){bool moved=false;for(auto other:external)if(overlaps(xAxisBox,other)){xAxisBox.y=other.y+other.h+8;moved=true;}if(!moved)break;}
        axisText(plot.xLabel,{x0+w/2,xAxisBox.y},axisFont,plot.xColor,false);
        // Own captions are also obstacles if dense values have to use the outer rail.
        for(auto& item:s.items)if(item.owner==owner&&item.type==Primitive::Text&&!item.text.empty()){
            Vec at=item.points[0];double tw=textWidth(measure,item.text,item.fontSize,item.font,item.face),th=item.fontSize*1.3;
            external.push_back(item.rotation==-90?Rect{at.x,at.y-tw,th,tw}:Rect{at.x,at.y,tw,th});
        }
        struct ValueCaption{Vec anchor;std::string text,color;size_t path,index;Rect box;bool backing=false;};std::vector<ValueCaption> captions;
        if(plot.showValues)for(size_t k=0;k<paths.size();k++)for(size_t i=0;i<paths[k].values.size();i++)if(visible(paths[k],i)){
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(1)<<paths[k].values[i];
            Vec anchor=pointAt(i,paths[k].values[i]);
            captions.push_back({anchor,out.str(),i<paths[k].levelColors.size()&&!paths[k].levelColors[i].empty()?paths[k].levelColors[i]:paths[k].color,k,i,{}});
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
            double th=font*1.3,gap=std::max(2.,font*.20),pitch=th+gap;
            double minY=topY+2,maxY=y0-th-4,step=std::max(.5,h/600.);
            int slots=int(std::floor((maxY-minY)/step))+1;if(slots<=0)return false;
            double maxShift=std::min(h*.45,std::max(2*th,(count-1)*pitch+gap));
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
        valueFont=std::min(valueFont,std::max(4.,std::max(12.,stateGap*.80)/std::max(1.,maxUnitWidth)));
        std::map<size_t,size_t> labelsPerState;size_t mostLabels=1;
        for(auto& caption:captions)mostLabels=std::max(mostLabels,++labelsPerState[caption.index]);
        // A shallow multi-path plot needs smaller text before labels drift far from their levels.
        valueFont=std::min(valueFont,std::max(6.,h/(8.+1.3*mostLabels)));
        bool fitted=captions.empty();
        for(int pass=0;pass<40&&!fitted;pass++){
            fitted=true;placedValueBoxes.clear();
            for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                if(!fitGroup(first,end,valueFont,false)){fitted=false;break;}first=end;
            }
            if(fitted)break;
            // At normal reading size, allow a small white text backing over connectors only.
            // Dots and level plateaus remain hard obstacles; nothing is moved to a side rail.
            if(valueFont<=9.){
                fitted=true;placedValueBoxes.clear();for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                    if(!fitGroup(first,end,valueFont,false)&&!fitGroup(first,end,valueFont,true)){fitted=false;break;}first=end;
                }
                if(fitted)break;
            }
            if(valueFont<=4.01)break;valueFont=std::max(4.,valueFont*.94);
        }
        if(!fitted){
            // If the chosen physical height cannot hold a group, keep that group local above
            // its own state. This bounded column has no diagonal leader or remote value list.
            placedValueBoxes.clear();
            for(size_t first=0;first<captions.size();){size_t end=first+1;while(end<captions.size()&&captions[end].index==captions[first].index)end++;
                if(!fitGroup(first,end,valueFont,false)&&!fitGroup(first,end,valueFont,true)){
                    double th=valueFont*1.3,gap=2,top=captions[first].anchor.y-(end-first)*(th+gap)-5;
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
    }
    for(auto& reaction:d.reactions)for(auto& item:s.items)if(item.owner==reaction.id&&item.type==Primitive::Text){
        if(!reaction.textColor.empty())item.color=reaction.textColor;
        for(auto& caption:reaction.captionColors)if(caption.role==item.textRole&&caption.index==item.textIndex)item.color=caption.color;
    }
    double l=1e20,t=1e20,r=-1e20,b=-1e20;
    for(auto& p:s.items){double pl=1e20,pt=1e20,pr=-1e20,pb=-1e20;for(auto q:p.points){double pad=p.type==Primitive::Text?0:p.width/2;pl=std::min(pl,q.x-pad);pt=std::min(pt,q.y-pad);pr=std::max(pr,q.x+pad);pb=std::max(pb,q.y+pad);if(p.type==Primitive::Text){pr=std::max(pr,q.x+textWidth(measure,p.text,p.fontSize,p.font,p.face));pb=std::max(pb,q.y+p.fontSize*1.3*(1+std::count(p.text.begin(),p.text.end(),'\n')));if(p.rotation==-90){pr=q.x+p.fontSize*1.3;pt=q.y-textWidth(measure,p.text,p.fontSize,p.font,p.face);pb=q.y;}}}
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
    for(auto& p:s.items){if(p.points.empty())continue;auto a=p.points[0];std::string dash=p.dash==1?" stroke-dasharray=\"4 3\"":p.dash==2?" stroke-dasharray=\"1 2\"":"";
        if(p.type==Primitive::Line||p.type==Primitive::Bezier){o<<"<path fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\""<<((p.flatEnd||p.flatStart)?" stroke-linecap=\"butt\"":"")<<dash<<" d=\"M "<<a.x<<' '<<a.y;
            if(p.type==Primitive::Bezier){for(size_t i=1;i+2<p.points.size();i+=3)o<<" C "<<p.points[i].x<<' '<<p.points[i].y<<' '<<p.points[i+1].x<<' '<<p.points[i+1].y<<' '<<p.points[i+2].x<<' '<<p.points[i+2].y;}
            else for(size_t i=1;i<p.points.size();i++)o<<" L "<<p.points[i].x<<' '<<p.points[i].y;if(p.closed)o<<" Z";o<<"\"/>\n";}
        else if(p.type==Primitive::Polygon){o<<"<polygon fill=\""<<p.color<<"\" points=\"";for(auto v:p.points)o<<v.x<<','<<v.y<<' ';o<<"\"/>\n";}
        else if(p.type==Primitive::Text){o<<"<text transform=\"rotate("<<p.rotation<<" "<<a.x<<" "<<a.y<<")\" fill=\""<<p.color<<"\" x=\""<<a.x<<"\" y=\""<<a.y+p.fontSize*.91<<"\" font-family=\""<<escape(p.font)<<"\" font-size=\""<<p.fontSize<<"\""<<(p.face&1?" font-weight=\"bold\"":"")<<(p.face&2?" font-style=\"italic\"":"")<<(p.face&4?" text-decoration=\"underline\"":"")<<">";std::istringstream lines(p.text);std::string line;int row=0;while(std::getline(lines,line)){o<<"<tspan x=\""<<a.x<<"\" dy=\""<<(row++?p.fontSize*1.3:0)<<"\">"<<escape(line)<<"</tspan>";}o<<"</text>\n";}
        else if(p.points.size()>=2){Vec z=p.points[1];double x=std::min(a.x,z.x),y=std::min(a.y,z.y),w=std::abs(z.x-a.x),h=std::abs(z.y-a.y);
            if(p.type==Primitive::Image)o<<"<image x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<w<<"\" height=\""<<h<<"\" href=\"data:"<<p.mime<<";base64,"<<hexToBase64(p.imageHex)<<"\"/>\n";
            else if(p.type==Primitive::Ellipse)o<<"<ellipse fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\" cx=\""<<x+w/2<<"\" cy=\""<<y+h/2<<"\" rx=\""<<w/2<<"\" ry=\""<<h/2<<"\"/>\n";
            else o<<"<rect fill=\""<<(p.filled?p.color:"none")<<"\" stroke=\""<<p.color<<"\" stroke-width=\""<<p.width<<"\" x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<w<<"\" height=\""<<h<<"\"/>\n";}}
    o<<"</g>\n</svg>\n";return o.str();
}
}
