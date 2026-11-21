"""Build editable BQCN / MeSiP2 templates from verified local XYZ references.
Run with development-only RDKit and NumPy; the portable app has no Python dependency.
"""
from pathlib import Path
import argparse, copy, hashlib, json, math, xml.etree.ElementTree as ET
import numpy as np
from rdkit import Chem
from rdkit.Chem import rdDetermineBonds, rdDepictor, rdMolDescriptors

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'resources' / 'ligand_templates'
REAR = '#a2a9b2'

def extract(path, family):
    mol=Chem.MolFromXYZBlock(path.read_text(encoding='utf-8-sig'))
    if mol is None: raise ValueError('Invalid XYZ')
    rdDetermineBonds.DetermineConnectivity(mol,useVdw=True,covFactor=1.2)
    metal=next(a.GetIdx() for a in mol.GetAtoms() if a.GetAtomicNum()==27)
    rw=Chem.RWMol(mol);rw.RemoveAtom(metal);isolated=rw.GetMol()
    fragments=Chem.GetMolFrags(isolated,asMols=False)
    fragment=max((f for f in fragments if any(isolated.GetAtomWithIdx(i).GetAtomicNum()==(7 if family=='bqcn' else 14) for i in f)),key=len)
    rw=Chem.RWMol(isolated)
    for i in reversed(range(isolated.GetNumAtoms())):
        if i not in fragment:rw.RemoveAtom(i)
    ligand=rw.GetMol()
    for atom in ligand.GetAtoms():
        if family=='mesip2' and atom.GetAtomicNum()==14:atom.SetFormalCharge(-1)
    rdDetermineBonds.DetermineBondOrders(ligand,charge=0 if family=='bqcn' else -1)
    ligand=Chem.RemoveHs(ligand)
    Chem.AssignStereochemistryFrom3D(ligand)
    xyz=np.array(ligand.GetConformer().GetPositions())
    origin=np.array(mol.GetConformer().GetAtomPosition(metal))
    donors=[a.GetIdx() for a in ligand.GetAtoms() if a.GetAtomicNum()==(7 if family=='bqcn' else 15)]
    assert len(donors)==(4 if family=='bqcn' else 2)
    if family=='mesip2':donors += [next(a.GetIdx() for a in ligand.GetAtoms() if a.GetAtomicNum()==14)]
    assert all(np.linalg.norm(xyz[i]-origin)<2.7 for i in donors)
    if family=='bqcn':assert rdMolDescriptors.CalcMolFormula(ligand)=='C26H28N4'
    else:assert ligand.GetNumAtoms()==28 and sum(a.GetAtomicNum()==15 for a in ligand.GetAtoms())==2
    extra=[]
    positions=np.array(mol.GetConformer().GetPositions())
    if family=='bqcn':
        extra=[(a.GetAtomicNum(),positions[a.GetIdx()]-origin) for a in mol.GetAtoms() if a.GetAtomicNum()==8 and np.linalg.norm(positions[a.GetIdx()]-origin)<2.4]
        assert len(extra)==2
    else:
        for a in mol.GetAtoms():
            if a.GetAtomicNum()!=6 or np.linalg.norm(positions[a.GetIdx()]-origin)>2.1:continue
            oxy=[b for b in a.GetNeighbors() if b.GetAtomicNum()==8 and np.linalg.norm(positions[b.GetIdx()]-positions[a.GetIdx()])<1.3]
            if oxy:extra=[(6,positions[a.GetIdx()]-origin),(8,positions[oxy[0].GetIdx()]-origin)];break
        assert len(extra)==2
    return ligand,xyz-origin,donors,extra

def best_view(xyz,bonds,donors):
    # Search proper rotations only. The last coordinate is distance from the viewer.
    best=None
    for yaw in range(0,360,20):
      for pitch in range(-65,66,20):
        a,b=math.radians(yaw),math.radians(pitch)
        rot=np.array([[math.cos(a),0,math.sin(a)],[0,1,0],[-math.sin(a),0,math.cos(a)]]) @ np.array([[1,0,0],[0,math.cos(b),-math.sin(b)],[0,math.sin(b),math.cos(b)]])
        view=xyz@rot.T
        # Orient the open coordination face up. This is a proper in-plane rotation.
        face=-view[donors,:2].mean(axis=0);angle=-math.pi/2-math.atan2(face[1],face[0])
        c,s=math.cos(angle),math.sin(angle);view[:,:2]=view[:,:2]@np.array([[c,s],[-s,c]])
        p=view[:,:2];score=0.
        for i in range(len(p)):
          for j in range(i):
            if (j,i) not in bonds:score+=max(0,.88-np.linalg.norm(p[i]-p[j]))**2*8
        for i,j in bonds:
            score+=max(0,.8-np.linalg.norm(p[i]-p[j]))**2*5
        for i in donors:score+=max(0,1.4-np.linalg.norm(p[i]))**2*8
        if best is None or score<best[0]:best=score,view.copy()
    view=best[1].copy();view[:,[0,2]] *= -1
    return view*21

def make_doc(mol,positions,donors,spatial,bound=False,peroxo=False,compact=False,precursor=False):
    m=Chem.Mol(mol);Chem.Kekulize(m,clearAromaticFlags=True)
    planar=Chem.Mol(mol);rdDepictor.Compute2DCoords(planar);Chem.WedgeMolBonds(planar,planar.GetConformer())
    flat=np.array(planar.GetConformer().GetPositions())*20;flat[:,1] *= -1
    root=ET.Element('YoungDraw',Version='3',bondGeometryVersion='1',BondLength='30',LabelSize='14',LabelFace='1',LineWidth='1.6',BoldWidth='2.6',BondSpacing='12')
    coords=positions if spatial else flat
    for i,a in enumerate(m.GetAtoms()):
        color='#0000ff' if a.GetAtomicNum()==7 else '#000000'
        q=0 if bound and a.GetAtomicNum()==14 else a.GetFormalCharge()
        if precursor and a.GetAtomicNum()==14:q=0
        attrs=dict(id=str(i+1),e=str(a.GetAtomicNum()),p=f'{coords[i,0]:.8f} {coords[i,1]:.8f}',q=str(q),h=str(a.GetTotalNumHs()),protect='1' if spatial else '0',color=color)
        if spatial:attrs['depth']=f'{coords[i,2]:.8f}'
        ET.SubElement(root,'atom',attrs)
    nextid=m.GetNumAtoms()+1
    for b in m.GetBonds():
        i,j=b.GetBeginAtomIdx(),b.GetEndAtomIdx();kind={1:0,2:1,3:2}[int(b.GetBondTypeAsDouble())]
        if not spatial:
            pb=planar.GetBondWithIdx(b.GetIdx());i,j=pb.GetBeginAtomIdx(),pb.GetEndAtomIdx()
            direction=pb.GetBondDir()
            if direction==Chem.BondDir.BEGINWEDGE:kind=3
            elif direction==Chem.BondDir.BEGINDASH:kind=4
        attrs=dict(id=str(nextid),a=str(i+1),b=str(j+1),kind=str(kind),color=REAR if spatial and (coords[i,2]+coords[j,2])/2>5 else '#000000')
        if spatial:attrs['depthColor']='#000000'
        ET.SubElement(root,'bond',attrs);nextid+=1
    if bound:
        metal=nextid;nextid+=1
        ET.SubElement(root,'atom',id=str(metal),e='27',p='0 0',q='0',h='0',protect='1',depth='0',color='#000000')
        for i in donors:
            kind='0' if m.GetAtomWithIdx(i).GetAtomicNum()==14 else '9'
            ET.SubElement(root,'bond',id=str(nextid),a=str(i+1),b=str(metal),kind=kind,customDisplay='Solid',color=REAR if coords[i,2]/2>5 else '#000000',depthColor='#000000');nextid+=1
    if precursor:
        si=next(a.GetIdx() for a in m.GetAtoms() if a.GetAtomicNum()==14)
        ET.SubElement(root,'atom',id=str(nextid),e='1',p=f'{coords[si,0]} {coords[si,1]-30}',q='0',h='0',color='#000000')
        ET.SubElement(root,'bond',id=str(nextid+1),a=str(si+1),b=str(nextid),kind='0',color='#000000')
    if peroxo:
        # Actual optimized O2 positions are installed by the caller.
        pass
    if compact:
        atoms={int(a.get('id')):a for a in root.findall('atom')};edges=root.findall('bond');remove=set()
        for p in [a for a in m.GetAtoms() if a.GetAtomicNum()==15]:
            for c in p.GetNeighbors():
                if c.GetAtomicNum()!=6 or c.GetIsAromatic():continue
                # Kekulized m has cleared aromatic flags: ring membership distinguishes aryl.
                if c.IsInRing():continue
                children=[a for a in c.GetNeighbors() if a.GetIdx()!=p.GetIdx()]
                if len(children)==2 and all(a.GetAtomicNum()==6 and a.GetDegree()==1 for a in children):
                    atoms[c.GetIdx()+1].set('label','iPr');atoms[c.GetIdx()+1].set('h','0')
                    remove.update(a.GetIdx()+1 for a in children)
        for a in root.findall('atom'):
            if int(a.get('id')) in remove:root.remove(a)
        for b in edges:
            if int(b.get('a')) in remove or int(b.get('b')) in remove:root.remove(b)
    return root

def validate_stereo(doc, reference, spatial):
    expected={a.GetIdx():a.GetProp('_CIPCode') for a in reference.GetAtoms() if a.HasProp('_CIPCode')}
    if not expected:return
    rw=Chem.RWMol();ids={};atoms=[a for a in doc.findall('atom') if int(a.get('id'))<=reference.GetNumAtoms()]
    conf=Chem.Conformer(len(atoms));conf.Set3D(spatial)
    for a in atoms:
        atom=Chem.Atom(int(a.get('e')));atom.SetFormalCharge(int(a.get('q')));atom.SetNumExplicitHs(int(a.get('h','0')))
        idx=rw.AddAtom(atom);ids[a.get('id')]=idx
        x,y=map(float,a.get('p').split());conf.SetAtomPosition(idx,(x,-y,-float(a.get('depth','0')) if spatial else 0))
    for b in doc.findall('bond'):
        if b.get('a') not in ids or b.get('b') not in ids:continue
        kind=int(b.get('kind'));a,z=ids[b.get('a')],ids[b.get('b')]
        rw.AddBond(a,z,{1:Chem.BondType.DOUBLE,2:Chem.BondType.TRIPLE}.get(kind,Chem.BondType.SINGLE))
        if kind in (3,4):rw.GetBondBetweenAtoms(a,z).SetBondDir(Chem.BondDir.BEGINWEDGE if kind==3 else Chem.BondDir.BEGINDASH)
    check=rw.GetMol();check.AddConformer(conf);Chem.SanitizeMol(check)
    if spatial:Chem.AssignStereochemistryFrom3D(check,replaceExistingTags=True)
    else:Chem.AssignChiralTypesFromBondDirs(check);Chem.AssignStereochemistry(check,force=True,cleanIt=True)
    observed={a.GetIdx():a.GetProp('_CIPCode') for a in check.GetAtoms() if a.HasProp('_CIPCode')}
    assert expected==observed, (expected,observed)

def run():
    parser=argparse.ArgumentParser();parser.add_argument('--bqcn-source',type=Path,required=True);parser.add_argument('--cosi-source',type=Path,required=True);args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);entries=[]
    for family,path in [('bqcn',args.bqcn_source),('mesip2',args.cosi_source)]:
        ligand,xyz,donors,extra=extract(path,family)
        archive=ROOT/'build'/'ligand-spatial-sources';archive.mkdir(parents=True,exist_ok=True)
        (archive/f'{family}-source.xyz').write_bytes(path.read_bytes())
        (OUT/f'{family}-ligand.xyz').write_text(Chem.MolToXYZBlock(ligand),encoding='utf-8')
        variants=[('',xyz)] if family=='mesip2' else [('',xyz),('-mirror',xyz*np.array([-1,1,1]))]
        for suffix,space in variants:
            molecule=Chem.Mol(ligand)
            for i,p in enumerate(space):molecule.GetConformer().SetAtomPosition(i,p)
            Chem.AssignStereochemistryFrom3D(molecule,replaceExistingTags=True)
            cip=''.join(a.GetProp('_CIPCode') for a in molecule.GetAtoms() if a.HasProp('_CIPCode'))
            label='BQCN · '+','.join(cip) if family=='bqcn' else 'MeSiP₂ · P(iPr)₂'
            bonds={tuple(sorted((b.GetBeginAtomIdx(),b.GetEndAtomIdx()))) for b in molecule.GetBonds()}
            view=best_view(space,bonds,donors)
            configurations=[('planar','평면',False,False,False),('ligand-3d','입체 리간드',True,False,False),('co-3d','Co 배위 골격',True,True,False)]
            if family=='mesip2':configurations += [('co-compact','Co 배위 · iPr 약어',True,True,True),('precursor','Si–H 전구체',False,False,False)]
            configurations += [('co-peroxo' if family=='bqcn' else 'co-carbonyl','Co–O₂ · side-on' if family=='bqcn' else 'Co–Si · CO',True,True,False)]
            for mode,title,spatial,bound,compact in configurations:
                doc=make_doc(molecule,view,donors,spatial,bound,compact=compact,precursor=mode=='precursor')
                if mode in ('co-peroxo','co-carbonyl'):
                    # Use the same rigid transform as the ligand; do not invent co-ligand geometry.
                    transform=np.linalg.lstsq(space,view,rcond=None)[0]
                    reflected=np.array([-1,1,1]) if suffix else np.ones(3)
                    metal=next(int(a.get('id')) for a in doc.findall('atom') if a.get('e')=='27')
                    nextid=1+max(int(a.get('id')) for a in doc)
                    added=[]
                    for element,position in (extra if family=='bqcn' else extra[:1]):
                        point=(position*reflected)@transform
                        ET.SubElement(doc,'atom',id=str(nextid),e=str(element),p=f'{point[0]:.8f} {point[1]:.8f}',depth=f'{point[2]:.8f}',q='0',h='0',protect='1',color='#000000')
                        if family=='mesip2':doc.findall('atom')[-1].set('label','CO')
                        added.append((nextid,point));nextid+=1
                    edges=[(metal,added[0][0],9)]
                    if family=='bqcn':edges += [(added[0][0],added[1][0],0),(metal,added[1][0],9)]
                    points={int(a.get('id')):float(a.get('depth','0')) for a in doc.findall('atom')}
                    for a,b,kind in edges:
                        ET.SubElement(doc,'bond',id=str(nextid),a=str(a),b=str(b),kind=str(kind),customDisplay='Solid',color=REAR if (points[a]+points[b])/2>5 else '#000000',depthColor='#000000');nextid+=1
                if not compact:validate_stereo(doc,molecule,spatial)
                slug=family+suffix+'-'+mode;data=ET.tostring(doc,encoding='unicode')
                (OUT/(slug+'.ydraw')).write_text(data,encoding='utf-8')
                entries.append(dict(slug=slug,name=label+' · '+title,category='착물 · equatorial' if bound else '리간드',family=family,spatial=spatial,bound=bound,compact=compact,data=data,smiles=Chem.MolToSmiles(molecule),formula=rdMolDescriptors.CalcMolFormula(molecule),source=path.name,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    header=['// Generated by tools/prepare_ligand_spatial_templates.py. Do not hand edit.','#pragma once','namespace yd {','struct LigandTemplateData {const char* slug;const char* name;const char* category;const char* family;bool spatial,bound,compact;const char* data;};','inline constexpr LigandTemplateData ligandTemplateData[] = {']
    for e in entries:
        strings=[json.dumps(e[k],ensure_ascii=False) for k in ['slug','name','category','family']]
        flags=[str(e[k]).lower() for k in ['spatial','bound','compact']]
        header.append('{'+','.join(strings+flags)+',R"LIGAND('+e['data']+')LIGAND"},')
    header+=['};','}']
    (ROOT/'src/ligand_templates_data.h').write_text('\n'.join(header)+'\n',encoding='utf-8')
    (OUT/'manifest.json').write_text(json.dumps([{k:v for k,v in e.items() if k!='data'} for e in entries],ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps([{k:e[k] for k in ['slug','formula','smiles']} for e in entries],ensure_ascii=False,indent=2))
if __name__=='__main__':run()
