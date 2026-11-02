#include "scene.h"
#include <array>
#include <limits>
namespace yd {
namespace {
using Cubic=std::array<Vec,4>;
Vec at(const Cubic& c,double t){double u=1-t;return c[0]*(u*u*u)+c[1]*(3*u*u*t)+c[2]*(3*u*t*t)+c[3]*(t*t*t);}
std::pair<Cubic,Cubic> split(const Cubic& c,double t){Vec a=c[0]+(c[1]-c[0])*t,b=c[1]+(c[2]-c[1])*t,z=c[2]+(c[3]-c[2])*t,m=a+(b-a)*t,n=b+(z-b)*t,p=m+(n-m)*t;return {{c[0],a,m,p},{p,n,z,c[3]}};}
Cubic section(const Cubic& c,double from,double to){auto left=split(c,to).first;return from<=0?left:split(left,from/to).second;}
Rect pad(Rect r,double gap){return {r.x-gap,r.y-gap,r.w+2*gap,r.h+2*gap};}
bool inside(Vec p,Rect r){return p.x>=r.x&&p.x<=r.x+r.w&&p.y>=r.y&&p.y<=r.y+r.h;}
double distance(Vec p,Rect r){double x=std::max({r.x-p.x,0.,p.x-r.x-r.w}),y=std::max({r.y-p.y,0.,p.y-r.y-r.h});return std::hypot(x,y);}
double segmentDistance(Vec p,Vec a,Vec b){Vec v=b-a;double ll=v.x*v.x+v.y*v.y;double t=ll>1e-9?std::clamp(((p-a).x*v.x+(p-a).y*v.y)/ll,0.,1.):0;return (p-a-v*t).length();}
double bondDistance(const Document& d,int id,Vec p){auto* bond=d.bond(id);if(!bond)return 1e10;auto* a=d.atom(bond->a);auto* b=d.atom(bond->b);return a&&b?segmentDistance(p,a->p,b->p):1e10;}
Vec bondPoint(const Document& d,int id,double t,Vec fallback){auto* bond=d.bond(id);if(!bond)return fallback;auto* a=d.atom(bond->a);auto* b=d.atom(bond->b);return a&&b?a->p+(b->p-a->p)*t:fallback;}
}
std::vector<Vec> routeArrow(const Document& d,const Object& o,const std::map<int,Rect>& measured){
    if(!o.autoRoute||o.kind!=ObjectKind::Curve||o.points.size()!=4)return o.points;
    Vec a=bondPoint(d,o.sourceBond,o.sourceBondT,o.points.front()),b=bondPoint(d,o.targetBond,o.targetBondT,o.points.back());if(auto* atom=d.atom(o.sourceAtom))a=atom->p;if(auto* atom=d.atom(o.targetAtom))b=atom->p;
    Vec v=b-a;double length=v.length();if(length<.01)return o.points;Vec n{-v.y/length,v.x/length};
    std::map<int,Rect> boxes;
    for(auto& atom:d.atoms){auto found=measured.find(atom.id);if(found!=measured.end())boxes[atom.id]=found->second;
        else if(atom.element==6&&atom.label.empty()&&!atom.charge&&!atom.isotope)boxes[atom.id]={atom.p.x-2,atom.p.y-2,4,4};
        else {std::string label=atom.label.empty()?symbol(atom.element):atom.label;size_t chars=0;for(unsigned char c:label)if((c&0xc0)!=0x80)chars++;double w=std::max(1.,double(chars))*d.style.fontSize*.65;double h=d.style.fontSize;boxes[atom.id]={atom.p.x-w/2-2,atom.p.y-h/2-2,w+4,h+4};}
    }
    struct Stroke {Vec a,b;double clearance;};std::vector<Stroke> strokes;
    for(const auto& bond:d.bonds){auto* left=d.atom(bond.a);auto* right=d.atom(bond.b);if(!left||!right)continue;double extra=bond.kind==BondKind::Double||bond.kind==BondKind::Triple||bond.kind==BondKind::Aromatic?d.style.bondLength*d.style.bondSpacing:0.;strokes.push_back({left->p,right->p,2.+(d.style.lineWidth+o.width)*.5+extra});}
    double base=std::clamp(length*.3,6.,24.);double bestScore=std::numeric_limits<double>::max();Cubic best{a,a+v*.28+n*base,a+v*.72+n*base,b};
    // Search only compact bends. Crowded labels must never produce a huge loop.
    for(double factor:{1.,-1.,.7,-.7,1.35,-1.35}){
        double bend=std::copysign(std::min(std::abs(base*factor),std::max(6.,std::min(30.,length*.55))),factor);Cubic curve{a,a+v*.28+n*bend,a+v*.72+n*bend,b};double start=0,end=1;
        auto source=boxes.find(o.sourceAtom),target=boxes.find(o.targetAtom);
        double endpointGap=3.+o.width*.5;
        auto bondGap=[&](int id){auto* bond=d.bond(id);double extra=bond&&(bond->kind==BondKind::Double||bond->kind==BondKind::Triple||bond->kind==BondKind::Aromatic)?d.style.bondLength*d.style.bondSpacing:0.;return endpointGap+d.style.lineWidth*.5+extra;};
        auto blocked=[&](Vec p,bool tail){auto box=tail?source:target;int bond=tail?o.sourceBond:o.targetBond;return (box!=boxes.end()&&inside(p,pad(box->second,endpointGap)))||(bond&&bondDistance(d,bond,p)<bondGap(bond));};
        {int step=0;while(step<190&&blocked(at(curve,step/400.),true))step++;if(blocked(at(curve,step/400.),true))continue;start=step/400.;}
        {int step=400;while(step>210&&blocked(at(curve,step/400.),false))step--;if(blocked(at(curve,step/400.),false))continue;end=step/400.;}
        if(end-start<.05)continue;Cubic candidate=section(curve,start,end);double score=std::abs(bend)*.025+(factor<0?.01:0);Vec prev=candidate[0];
        for(int sample=0;sample<=100;sample++){Vec point=at(candidate,sample/100.);score+=(point-prev).length()*.01;prev=point;for(auto& entry:boxes){double clearance=distance(point,entry.second);double needed=entry.first==o.sourceAtom||entry.first==o.targetAtom?3.5:5.5+o.width*.5;if(clearance<needed)score+=100000+(needed-clearance)*1000;else if(clearance<needed+5)score+=(needed+5-clearance)*.2;}}
        // Keep compact arcs and their barbs clear of molecular bond strokes too.
        auto scoreBonds=[&](Vec point){for(const auto& stroke:strokes){double needed=stroke.clearance,clearance=segmentDistance(point,stroke.a,stroke.b);if(clearance<needed)score+=100000+(needed-clearance)*1000;else if(clearance<needed+12)score+=(needed+12-clearance)*.5;}};
        for(int sample=0;sample<=100;sample++)scoreBonds(at(candidate,sample/100.));
        Object preview=o;preview.points.assign(candidate.begin(),candidate.end());auto pieces=arrowPrimitives(preview);
        if(pieces.size()==2)for(auto point:pieces[1].points){scoreBonds(point);for(const auto& entry:boxes)if(distance(point,entry.second)<2.+o.width*.5)score+=100000;}
        // Reserve the arrowhead's lateral extent at the receiving end as well.
        Vec tangent=candidate[3]-candidate[2];double tangentLength=tangent.length();if(tangentLength>.001){Vec u=tangent*(1/tangentLength),side{-u.y,u.x};for(auto& entry:boxes)if(entry.first!=o.targetAtom)for(double sign:{-1.,1.})if(inside(candidate[3]-u*std::max(6.,o.width*3.5)+side*(sign*std::max(2.5,o.width*1.7)),pad(entry.second,3)))score+=100000;}
        if(score<bestScore){bestScore=score;best=candidate;}
    }
    return std::vector<Vec>(best.begin(),best.end());
}
std::vector<Primitive> arrowPrimitives(const Object& o){
    if(o.head==3&&o.points.size()>=2){
        Vec a=o.points.front(),b=o.points.back(),v=b-a;double len=v.length();if(len<1e-6)return {};Vec u=v*(1/len),n{-u.y,u.x};double h=std::min(std::max(6.,o.width*3.5),len*.35),w=h*.5;
        Primitive shaft;shaft.owner=o.id;shaft.width=o.width;shaft.color=o.color;shaft.dash=o.dash;shaft.flatEnd=true;shaft.points={a+u*h,b-u*h};
        Primitive left;left.type=Primitive::Polygon;left.owner=o.id;left.color=o.color;left.filled=true;left.points={a,a+u*h+n*w,a+u*h-n*w};Primitive right=left;right.points={b,b-u*h+n*w,b-u*h-n*w};return {shaft,left,right};
    }
    if(o.points.size()<2||!o.head)return {};std::vector<Vec> points=o.points;bool curved=o.kind==ObjectKind::Curve&&points.size()>=4&&(points.size()-1)%3==0;Vec tip=points.back();double total=0;
    if(curved)for(size_t i=0;i+3<points.size();i+=3){Cubic c{points[i],points[i+1],points[i+2],points[i+3]};Vec prev=c[0];for(int j=1;j<=64;j++){Vec p=at(c,j/64.);total+=(p-prev).length();prev=p;}}
    else for(size_t i=1;i<points.size();i++)total+=(points[i]-points[i-1]).length();
    if(total<1e-6)return {};double headLength=std::min(std::max(6.,o.width*3.5),total*.6),remaining=headLength;
    if(o.head==2){
        // A fishhook is a continuous shaft and one stroked barb. Align the
        // barb with the endpoint tangent, rather than a chord of the curve.
        Vec tangent{};size_t previous=points.size()-1;
        while(previous>0){--previous;tangent=tip-points[previous];if(tangent.length()>1e-6)break;}
        double length=tangent.length();if(length<1e-6)return {};
        Vec u=tangent*(1/length),normal{-u.y,u.x};double side=1;
        if(curved){
            // The outside of the bend keeps the hook clear of the shaft.
            for(size_t i=previous;i>0;--i){Vec before=points[i]-points[i-1];double turn=before.x*u.y-before.y*u.x;
                if(std::abs(turn)>1e-6){side=turn>0?-1:1;break;}}
        }
        Primitive shaft;shaft.owner=o.id;shaft.type=curved?Primitive::Bezier:Primitive::Line;shaft.points=points;shaft.width=o.width;shaft.color=o.color;shaft.dash=o.dash;shaft.flatEnd=true;
        Primitive hook;hook.owner=o.id;hook.type=Primitive::Line;hook.width=o.width;hook.color=o.color;hook.flatEnd=true;
        hook.points={tip-u*headLength+normal*(side*headLength*.55),tip};
        return {shaft,hook};
    }
    if(curved){for(size_t end=points.size();end>=4;){size_t i=end-4;Cubic c{points[i],points[i+1],points[i+2],points[i+3]};Vec prev=c[3];bool found=false;for(int j=127;j>=0;j--){double t=j/128.;Vec p=at(c,t);double segment=(p-prev).length();if(segment>=remaining){double cut=t+(1./128.)*(1-remaining/std::max(segment,1e-9));auto left=split(c,cut).first;points.resize(i+4);for(int k=0;k<4;k++)points[i+k]=left[k];found=true;break;}remaining-=segment;prev=p;}if(found)break;if(i==0)break;points.resize(i+1);end=points.size();}}
    else {while(points.size()>1){Vec a=points[points.size()-2],b=points.back();double len=(b-a).length();if(len>=remaining){points.back()=b-(b-a)*(remaining/len);break;}remaining-=len;points.pop_back();}}
    Vec base=points.back(),direction=tip-base;double len=direction.length();if(len<1e-6)return {};Vec u=direction*(1/len),normal{-u.y,u.x};double halfWidth=std::min(std::max(2.5,o.width*1.7),len*.55);
    Primitive shaft;shaft.owner=o.id;shaft.type=curved?Primitive::Bezier:Primitive::Line;shaft.points=points;shaft.width=o.width;shaft.color=o.color;shaft.dash=o.dash;shaft.flatEnd=true;
    Primitive head;head.owner=o.id;head.type=Primitive::Polygon;head.color=o.color;head.filled=true;head.points={tip,base+normal*halfWidth,base-normal*halfWidth};
    return {shaft,head};
}
}
