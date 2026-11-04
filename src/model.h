#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <optional>
#include <set>

namespace yd {
constexpr double pi = 3.14159265358979323846;
constexpr double benzeneBondScale = .75;
constexpr double benzeneConnectorScale = .85;
struct Vec {
    double x=0, y=0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator*(double k) const { return {x*k,y*k}; }
    double length() const { return std::hypot(x,y); }
};
struct Style {
    double bondLength=30, lineWidth=2, boldWidth=3.2, margin=2;
    double hashSpacing=2.7, bondSpacing=.12, fontSize=14;
    int labelFace=1;
};
enum class BondKind { Single, Double, Triple, Wedge, Hash, Dash, Bold, Wavy, Aromatic, Dative };
struct TextRun { std::string text,color="#000000",font="Arial"; double size=10; int face=0; };
struct Atom { int id; Vec p; int element=6; int charge=0,isotope=0,explicitH=-1,radical=0; std::string label; bool protect=false; std::vector<TextRun> labelRuns; Vec labelOffset; std::string color="#000000"; Vec annotationOffset; bool annotationManual=false; };
struct Bond { int id, a, b; BondKind kind=BondKind::Single; int doubleSide=0; std::string customOrder,customDisplay; std::string color="#000000"; };
enum class ObjectKind { Line, Curve, Text, Ellipse, Rectangle, Image };
struct ArrowCross { int index=-1; bool enabled=false; double width=4,size=14; std::string color="#ff0000"; };
struct MechanismLink { int from=0,to=1; std::vector<int> members; }; // from -1: common external vertical spine.
struct Object {
    int id=0; ObjectKind kind=ObjectKind::Line; std::vector<Vec> points;
    std::string text,color="#000000",imageHex,mime="image/png",font="Arial";
    double width=2,fontSize=14; int dash=0,head=0,face=1; bool closed=false,filled=false;
    std::vector<TextRun> runs;
    int sourceAtom=0,targetAtom=0; bool autoRoute=false;
    int sourceBond=0,targetBond=0; double sourceBondT=.5,targetBondT=.5;
    bool centerText=true;
    ArrowCross cross;
};
struct Slot { std::string label; std::vector<int> members; std::string energy; bool startsRow=false; };
struct CaptionColor { int role=0,index=-1; std::string color; };
struct Reaction {
    int id=0; Vec p; double spacing=120,slotWidth=64,slotHeight=65;
    std::vector<Slot> slots; std::vector<std::string> above,below; int arrowType=0; bool cyclic=false; std::string unit="kcal/mol";
    std::string centerLabel;
    double captionFontSize=0,cycleRadius=0; // 0 uses automatic sizing.
    std::vector<int> hiddenArrows; // Connector indices omitted from the ring.
    std::string textColor; // Empty preserves the default caption and spin colors.
    std::vector<CaptionColor> captionColors;
    int wrapColumns=0; // 0: one row; positive: alternating left/right rows (ignored for cycles).
    bool vertical=false; // Straight mechanism runs from top to bottom.
    std::vector<MechanismLink> links; // Explicit reaction graph; empty uses sequential connectors.
    std::vector<Vec> slotGrid; // Frame positions in column/row units.
    std::vector<ArrowCross> crosses;
    double rowGap=100; // Minimum clear vertical space between structure boxes.
};
struct EnergyState { std::string label; double energy=0; std::vector<int> above,below; int marker=-1; };
struct MechanismStage { std::string label; double start=0,end=100,fontSize=0; }; // Percent span; fontSize 0 matches state captions.
// Empty color entries inherit the pathway color. Segment i joins states i and i+1.
struct EnergySeries { std::string name="Path 1",color="#000000"; std::vector<double> values; int dash=1; double width=3.5; int marker=1; double markerSize=2.5; std::vector<bool> present; std::vector<std::string> levelColors,segmentColors; bool hasValue(size_t i) const {return i<values.size()&&(present.empty()||(i<present.size()&&present[i]));} };
struct EnergyPlot {
    int id=0; Vec p; double width=520,height=320;
    std::string title="Reaction coordinate",unit="kcal/mol";
    std::string xLabel="Reaction coordinate",yLabel="Energy",xColor="#000000",yColor="#000000";
    std::optional<double> yMin,yMax;
    double axisScale=1;
    double axisFontSize=0,tickFontSize=0,valueFontSize=0,energyLabelFontSize=0; // pt; 0 fits automatically to graph size.
    double yTickStep=0; // 0: automatic; positive: spacing in the plot's energy unit.
    std::vector<MechanismStage> stages; // Empty in older documents; editable independently of energy states.
    std::vector<EnergyState> states; std::vector<EnergySeries> series; int mode=3; bool showValues=true;
};
struct Group { int id=0; std::vector<int> members; };
struct Document {
    Style style;
    bool imagesLocked=false,graphsLocked=false;
    int bondGeometryVersion=1;
    bool isLocked(int id) const;
    std::vector<int> editableIds() const;
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;
    std::vector<Object> objects;
    std::vector<Reaction> reactions;
    std::vector<EnergyPlot> plots;
    std::vector<Group> groups;
    // Original CDXML and ids let the writer patch editable objects while
    // retaining unsupported source objects, annotations, and references.
    std::string sourceXml;
    std::set<int> sourceOwned;
    std::set<int> sourceGroups;
    std::vector<std::string> importWarnings;
    int nextId=1;
    int addAtom(Vec p, int element=6);
    int addBond(int a, int b, BondKind kind=BondKind::Single);
    Atom* atom(int id);
    const Atom* atom(int id) const;
    Bond* bond(int id);
    const Bond* bond(int id) const;
    int atomAt(Vec p, double tolerance) const;
    int bondAt(Vec p, double tolerance) const;
    void eraseAtom(int id);
    void eraseBond(int id);
    void ring(Vec center, int sides, bool alternating=false);
    void attachRing(int atomId,int bondId,int sides,bool alternating=false);
    std::vector<int> component(int atomId) const;
    void translate(const std::vector<int>& ids,Vec delta);
    void erase(const std::vector<int>& ids);
    std::vector<int> allIds() const;
    void append(const Document& other,Vec delta={});
    Document subset(const std::vector<int>& ids) const;
    int hydrogens(int id) const;
    std::vector<std::string> validate() const;
};
struct CleanupReport { int regularRings=0,protectedComponents=0,components=0; };
bool upgradeBondGeometry(Document& d);
CleanupReport cleanStructure(Document& d,const std::vector<int>& selection={},bool preserveProjection=false);
size_t reactionRow(const Reaction& r,size_t index);
Vec reactionSlot(const Reaction& r,size_t index);
void appendMechanismStage(Reaction& r,bool below=false);
Vec plotAnchor(const EnergyPlot& p,size_t index,bool above);
void optimizeMechanism(Document& d,const std::vector<int>& selection={},bool cyclic=false);
void fillSlot(Document& d,const Document& fragment,int container,size_t index,bool above=false);
int collapsePhenyl(Document& d,const std::vector<int>& selection);
int arrangeImportedMechanism(Document& d,const std::vector<int>& selection={});
void addMechanismBranch(Reaction& r,size_t source);
std::vector<int> addBranchArrows(Document& d,Vec top,double height,double length,size_t count,bool left=false,int stemId=0);
void normalizeChemicalLabels(Document& d,const std::vector<int>& selection={});
bool atomLabelHasCharge(const Atom& a);
Document chairTemplate();
Document mechanismDemo();
Document catalyticCycleDemo();
Document coordinateDemo();
void applyHouseStyle(Document& d);
std::string symbol(int atomicNumber);
int atomicNumber(const std::string& symbol);
std::string chemicalScripts(const std::string& text);
int order(BondKind kind);
Vec snapped(Vec start, Vec end, double length, bool free=false);
double defaultBondLength(const Document& d,int atom,BondKind kind=BondKind::Single,int element=6);
double bondLengthScale(int firstElement,int secondElement,BondKind kind);
void setBondKind(Document& d,int bondId,BondKind kind);
void setAtomElement(Document& d,int atomId,int element);
Vec bondEndpoint(const Document& d,int atom,Vec desired,BondKind kind=BondKind::Single,int element=6);
enum class OrbitalKind { S, P, D, Hybrid };
std::vector<int> addOrbital(Document& d,Vec center,Vec tip,OrbitalKind kind=OrbitalKind::P,int phaseStyle=0);
int delocalizeBonds(Document& d,const std::vector<int>& selection);
// Positive angles turn clockwise in canvas coordinates. Molecules remain rigid.
std::vector<int> scaleStructures(Document& d,const std::vector<int>& selection,double factor,std::optional<Vec> center=std::nullopt);
std::vector<int> transformStructures(Document& d,const std::vector<int>& selection,double degrees,bool flipX=false,bool flipY=false);
enum class AlignMode { Left, CenterX, Right, Top, MiddleY, Bottom, DistributeHorizontal, DistributeVertical };
bool alignObjects(Document& d,const std::vector<int>& selection,AlignMode mode);
bool toggleAromaticDisplay(Document& d,const std::vector<int>& selection);
Document demo();
class History {
    std::vector<Document> undo_, redo_;
public:
    void checkpoint(const Document& d);
    bool undo(Document& d);
    bool redo(Document& d);
    void clear() { undo_.clear(); redo_.clear(); }
};
}
