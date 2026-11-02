"""Rebuild editable 2D templates from the user's Youngcraft/CDXML references.

RDKit is a development-time dependency only. Bundled data is compiled into the exe.
Axial/equatorial variants are diagram projections, not computed isomers.
"""
from pathlib import Path
import copy, hashlib, json, math, xml.etree.ElementTree as ET
import numpy as np
from rdkit import Chem
from rdkit.Chem import rdDepictor, rdMolDescriptors
from coordination_motifs import add_motif
from template_presentation import apply_presentation

ROOT = Path(__file__).resolve().parents[1]
YOUNGCRAFT = ROOT.parent / 'Youngcraft'
RESEARCH = Path(r'D:\이영섭\01_연구\01_계산화학\AfterPhD')
OUT = ROOT / 'resources' / 'lab_templates'
OUT.mkdir(parents=True, exist_ok=True)
REAR = '#a2a9b2'
entries = []


def document():
    return ET.Element('YoungDraw', Version='3', BondLength='30', LabelSize='14',
                      LabelFace='1', LineWidth='2', BoldWidth='3.2', BondSpacing='12')


def add_atom(root, element, p, **kw):
    id = 1 + max([int(x.get('id', '0')) for x in root] or [0])
    attrs = dict(id=str(id), e=str(element), p=f'{p[0]:.6f} {p[1]:.6f}', q='0', h='-1')
    attrs.update({k: str(v) for k, v in kw.items()})
    ET.SubElement(root, 'atom', attrs)
    return id


def add_bond(root, a, b, kind=0, **kw):
    id = 1 + max(int(x.get('id', '0')) for x in root)
    attrs = dict(id=str(id), a=str(a), b=str(b), kind=str(kind))
    attrs.update({k: str(v) for k, v in kw.items()})
    ET.SubElement(root, 'bond', attrs)


def save(slug, category, name, root, source, **extra):
    apply_presentation(root, slug)
    data = ET.tostring(root, encoding='unicode')
    (OUT / (slug + '.ydraw')).write_text(data, encoding='utf-8')
    entries.append(dict(slug=slug, category=category, name=name, source=source,
                        atoms=len(root.findall('atom')), bonds=len(root.findall('bond')),
                        sha256=hashlib.sha256(data.encode()).hexdigest(), **extra))
    return data


def from_cjson(path):
    j = json.loads(path.read_text(encoding='utf-8'))
    rw = Chem.RWMol(); mapping = {}; donors = []
    for i, e in enumerate(j['atoms']['elements']['number']):
        if e > 1:
            mapping[i] = rw.AddAtom(Chem.Atom(e))
    ids = j['bonds']['connections']['index']
    for k, order in enumerate(j['bonds']['order']):
        a, b = ids[k*2:k*2+2]
        if a == 0 and b in mapping: donors.append(mapping[b])
        if b == 0 and a in mapping: donors.append(mapping[a])
        if a in mapping and b in mapping:
            rw.AddBond(mapping[a], mapping[b], {1: Chem.BondType.SINGLE,
                       1.5: Chem.BondType.AROMATIC, 2: Chem.BondType.DOUBLE,
                       3: Chem.BondType.TRIPLE}[order])
    mol = rw.GetMol(); Chem.SanitizeMol(mol)
    xyz = np.array(j['atoms']['coords']['3d']).reshape(-1, 3)
    return mol, xyz[list(mapping)], donors, j


def mol_document(mol):
    m = Chem.Mol(mol); rdDepictor.Compute2DCoords(m, canonOrient=True)
    Chem.Kekulize(m, clearAromaticFlags=True)
    root = document(); ids = {}
    for atom in m.GetAtoms():
        p = m.GetConformer().GetAtomPosition(atom.GetIdx())
        ids[atom.GetIdx()] = add_atom(root, atom.GetAtomicNum(), (p.x*20, -p.y*20), q=atom.GetFormalCharge())
    for bond in m.GetBonds():
        add_bond(root, ids[bond.GetBeginAtomIdx()], ids[bond.GetEndAtomIdx()],
                 {1.: 0, 2.: 1, 3.: 2}[bond.GetBondTypeAsDouble()])
    return root


def cdxml_fragment(path, fragment_id):
    source = ET.parse(path).getroot(); f = next(x for x in source.iter('fragment') if x.get('id') == fragment_id)
    colors = {'0': '#000000', '1': '#ffffff'}
    for i, c in enumerate(source.find('colortable'), 2):
        colors[str(i)] = '#' + ''.join(f'{round(float(c.get(k))*255):02x}' for k in ['r','g','b'])
    root = document(); ids = {}; nodes = list(f.findall('n'))
    lengths = []
    pos = {n.get('id'): np.array(list(map(float, n.get('p').split()))) for n in nodes}
    for b in f.findall('b'):
        lengths.append(np.linalg.norm(pos[b.get('B')]-pos[b.get('E')]))
    scale = 30 / np.median(lengths); center = sum(pos.values())/len(pos)
    for n in nodes:
        label = ''.join(n.find('t').itertext()).strip() if n.find('t') is not None else ''
        ids[n.get('id')] = add_atom(root, int(n.get('Element', '6')), (pos[n.get('id')]-center)*scale,
                                   label=label, color=colors.get(n.get('color',next(iter(n.iter('s')),n).get('color','0')),'#000000'),
                                   q=n.get('Charge','0'), h=n.get('NumHydrogens','-1'))
    for b in f.findall('b'):
        a, z = ids[b.get('B')], ids[b.get('E')]; display = b.get('Display','Solid')
        kind = {'2':1,'3':2,'1.5':8,'dative':9}.get(b.get('Order','1'),0)
        if display in ['WedgeBegin','WedgeEnd']: kind=3
        if display in ['WedgedHashBegin','WedgedHashEnd']: kind=4
        if display == 'Bold' and kind==0: kind=6
        if display == 'Dash': kind=5
        if display.endswith('End'): a,z=z,a
        add_bond(root,a,z,kind,color=colors.get(b.get('color','0'),'#000000'),customDisplay='Bold' if display=='Bold' and kind==1 else '')
    return root


def graph_from_native(root):
    rw=Chem.RWMol();ids={}
    for a in root.findall('atom'):
        atom=Chem.Atom(int(a.get('e')));atom.SetFormalCharge(int(a.get('q','0')))
        ids[a.get('id')]=rw.AddAtom(atom)
    for b in root.findall('bond'):
        rw.AddBond(ids[b.get('a')],ids[b.get('b')],{1:Chem.BondType.DOUBLE,2:Chem.BondType.TRIPLE,8:Chem.BondType.AROMATIC}.get(int(b.get('kind')),Chem.BondType.SINGLE))
    mol=rw.GetMol();Chem.SanitizeMol(mol);return mol


def complex_projection(mol, xyz, donors, orientation):
    """Choose a view preserving the supplied 3D topology and minimizing collisions."""
    # The open donor face points away from the mean metal-to-donor vector.
    direction=-xyz[donors].mean(axis=0);direction/=np.linalg.norm(direction)
    seed=np.array([0.,0.,1.]) if abs(direction[2])<.8 else np.array([0.,1.,0.])
    u=np.cross(direction,seed);u/=np.linalg.norm(u);v=np.cross(direction,u)
    best=None
    for degree in range(0,360,10):
        t=math.radians(degree);right=u*math.cos(t)+v*math.sin(t)
        depth=np.cross(direction,right);up=direction*.94+depth*.34;up/=np.linalg.norm(up)
        points=np.column_stack([xyz@right,-xyz@up])*21
        score=0.
        for i in range(len(points)):
            for j in range(i):
                distance=np.linalg.norm(points[i]-points[j]);score+=max(0,15-distance)**2
        if best is None or score<best[0]:best=(score,points,xyz@depth)
    _,points,depth=best
    if orientation=='equatorial':points=np.column_stack([-points[:,1],points[:,0]])
    root=document();m=Chem.Mol(mol);Chem.Kekulize(m,clearAromaticFlags=True);ids={}
    for a in m.GetAtoms():
        i=a.GetIdx();ids[i]=add_atom(root,a.GetAtomicNum(),points[i],protect=1,
                                     color=REAR if depth[i]>.45 else '#000000',h=0)
    for b in m.GetBonds():
        a,z=b.GetBeginAtomIdx(),b.GetEndAtomIdx()
        add_bond(root,ids[a],ids[z],{1.:0,2.:1,3.:2}[b.GetBondTypeAsDouble()],
                 color=REAR if (depth[a]+depth[z])/2>.35 else '#000000')
    metal=add_atom(root,26,(0,0),label='M',protect=1,h=0)
    for i in donors:add_bond(root,metal,ids[i],0,color=REAR if depth[i]>.35 else '#000000')
    return root,metal


def oxygen(root, metal, species, orientation):
    add_motif(root, metal, species, orientation)


references={
 '12-mtp':('Me3-TPADP (12-MTP)','12-mtp.cjson'),
 '14-mtp':('14-MTP','14-mtp.cjson'),
 'mdap':('MDAP','mdap.cjson'),
 'me2s-dpadp':('Me2S-DPADP','me2s-dpadp.cjson'),
 'tbdap':('TBDAP','tbdap-candidate.cjson')}
ligands={}
for slug,(name,filename) in references.items():
    path=YOUNGCRAFT/'lab_fragments'/'ligands'/filename
    m,xyz,donors,j=from_cjson(path)
    source=f'Youngcraft/lab_fragments/ligands/{filename}; '+j['properties']['source']['file']
    if slug=='tbdap':
        expected=Chem.MolFromSmiles('CC(C)(C)N1Cc2cccc(n2)CN(C(C)(C)C)Cc2cccc(n2)C1')
        assert Chem.MolToSmiles(m)==Chem.MolToSmiles(expected)
        source+='; topology cross-check: N,N-di-tert-butyl pyridinophane, DOI 10.1021/jacs.4c03211'
    ligands[slug]=(name,m,xyz,donors,source)

# CHDAP: replace the two N-methyl substituents of verified MDAP by cyclohexyl.
name,m,xyz,donors,source=ligands['mdap'];rw=Chem.RWMol(m)
substituents=[a.GetIdx() for a in m.GetAtoms() if a.GetAtomicNum()==6 and a.GetDegree()==1 and a.GetNeighbors()[0].GetAtomicNum()==7]
assert len(substituents)==2
for carbon in substituents:
    previous=carbon
    for k in range(5):
        i=rw.AddAtom(Chem.Atom(6));rw.AddBond(previous,i,Chem.BondType.SINGLE);previous=i
    rw.AddBond(previous,carbon,Chem.BondType.SINGLE)
chdap=rw.GetMol();Chem.SanitizeMol(chdap)
# New substituents get a constrained conformer so the donor framework stays fixed.
from rdkit.Chem import AllChem
# A deterministic 3D conformer supplies just the new C6 rings, then the source frame is aligned.
h=Chem.AddHs(chdap);assert AllChem.EmbedMolecule(h,randomSeed=20261021)==0
AllChem.MMFFOptimizeMolecule(h,maxIters=300)
fresh=np.array(h.GetConformer().GetPositions())[:chdap.GetNumAtoms()]
old=fresh[:len(xyz)];a=old-old.mean(axis=0);b=xyz-xyz.mean(axis=0);u,_,vt=np.linalg.svd(a.T@b);rot=u@vt
if np.linalg.det(rot)<0:u[:,-1]*=-1;rot=u@vt
fresh=(fresh-old.mean(axis=0))@rot+xyz.mean(axis=0)
ligands['chdap']=('CHDAP',chdap,fresh,donors,source+'; N,N-dicyclohexyl substitution; https://scholar.dgist.ac.kr/handle/20.500.11750/10715')

for slug,(name,m,xyz,donors,source) in ligands.items():
    formula=rdMolDescriptors.CalcMolFormula(m)
    save(slug,'리간드',name,mol_document(m),source,formula=formula,smiles=Chem.MolToSmiles(m))
    for orient in ['axial','equatorial']:
        for species in ['open','peroxo-side','peroxo-end','hydroperoxo-end','hydroperoxo-side']:
            root,metal=complex_projection(m,xyz,donors,orient);oxygen(root,metal,species,orient)
            label={'open':'배위 골격','peroxo-side':'peroxo (side-on)','peroxo-end':'peroxo (end-on)',
                   'hydroperoxo-end':'hydroperoxo (end-on)','hydroperoxo-side':'hydroperoxo (side-on)'}[species]
            save(f'{slug}-{orient}-{species}',f'착물 · {orient}',f'{name} · {label}',root,source,
                 projection=orient,oxygen=species,schematic=True)

mpdap_path=next(RESEARCH.rglob('MPDAP(2-3-4).cdxml'))
mpdap=cdxml_fragment(mpdap_path,'114350');mpdap_mol=graph_from_native(mpdap)
save('mpdap','리간드','MPDAP',mpdap,str(mpdap_path)+' #114350',formula=rdMolDescriptors.CalcMolFormula(mpdap_mol),smiles=Chem.MolToSmiles(mpdap_mol))
original=cdxml_fragment(mpdap_path,'114427')
for a in original.findall('atom'):a.set('protect','1')
save('mpdap-mn-cl-source','원본 CDXML 착물','MPDAP · Mn(II)-Cl (원본 배위 투영)',original,str(mpdap_path)+' #114427')
for orient in ['axial','equatorial']:
    for species in ['open','peroxo-side','peroxo-end','hydroperoxo-end','hydroperoxo-side']:
        root=copy.deepcopy(original);cl=next(a for a in root.findall('atom') if a.get('e')=='17');cid=cl.get('id');cl_position=np.array(list(map(float,cl.get('p').split())))
        root.remove(cl)
        for b in list(root.findall('bond')):
            if cid in [b.get('a'),b.get('b')]:root.remove(b)
        metal_atom=next(a for a in root.findall('atom') if a.get('e')=='25');metal=int(metal_atom.get('id'));origin=np.array(list(map(float,metal_atom.get('p').split())))
        direction=cl_position-origin;angle=-math.pi/2-math.atan2(direction[1],direction[0]);rotation=np.array([[math.cos(angle),-math.sin(angle)],[math.sin(angle),math.cos(angle)]])
        for a in root.findall('atom'):
            p=rotation@(np.array(list(map(float,a.get('p').split())))-origin)
            if orient=='equatorial':p=np.array([-p[1],p[0]])
            a.set('p',f'{p[0]} {p[1]}')
        metal_atom.set('label','M')
        # The source's coordination sketch omits aromatic double strokes. Match
        # its skeleton to the complete free-ligand graph before adding them.
        ligand=copy.deepcopy(root);ligand.remove(next(a for a in ligand.findall('atom') if a.get('id')==str(metal)))
        for bond in list(ligand.findall('bond')):
            if str(metal) in [bond.get('a'),bond.get('b')]:ligand.remove(bond)
        target=graph_from_native(ligand);query=Chem.RWMol(mpdap_mol)
        for atom in query.GetAtoms():atom.SetIsAromatic(False)
        for bond in query.GetBonds():bond.SetIsAromatic(False);bond.SetBondType(Chem.BondType.SINGLE)
        query=query.GetMol();Chem.SanitizeMol(query);match=target.GetSubstructMatch(query);assert len(match)==26
        target_atoms=ligand.findall('atom');kekule=Chem.Mol(mpdap_mol);Chem.Kekulize(kekule,clearAromaticFlags=True)
        double_pairs={frozenset([target_atoms[match[b.GetBeginAtomIdx()]].get('id'),target_atoms[match[b.GetEndAtomIdx()]].get('id')]) for b in kekule.GetBonds() if b.GetBondTypeAsDouble()==2}
        for bond in root.findall('bond'):
            if frozenset([bond.get('a'),bond.get('b')]) in double_pairs:
                if bond.get('kind')=='6':bond.set('customDisplay','Bold')
                bond.set('kind','1')
        if species.endswith('side'):
            # A side-on O2 motif needs two vacant sites: use an explicitly
            # labelled kappa4 scaffold with the pendant pyridyl donor detached.
            ligand=copy.deepcopy(root);ligand.remove(next(a for a in ligand.findall('atom') if a.get('id')==str(metal)))
            for bond in list(ligand.findall('bond')):
                if str(metal) in [bond.get('a'),bond.get('b')]:ligand.remove(bond)
                elif int(bond.get('kind')) not in [0,1,2,8]:bond.set('kind','0')
            graph=graph_from_native(ligand);atoms=ligand.findall('atom');rings=graph.GetRingInfo()
            pendant=[atoms[a.GetIdx()].get('id') for a in graph.GetAtoms() if a.GetAtomicNum()==7 and a.GetIsAromatic() and rings.NumAtomRings(a.GetIdx())==1]
            assert len(pendant)==1
            for bond in list(root.findall('bond')):
                if set([bond.get('a'),bond.get('b')])=={str(metal),pendant[0]}:root.remove(bond)
        oxygen(root,metal,species,orient)
        save(f'mpdap-{orient}-{species}',f'착물 · {orient}',f'MPDAP · {species}'+(' (kappa4)' if species.endswith('side') else ''),root,str(mpdap_path)+' #114427; ligand connectivity and gray rear strokes retained',schematic=True)

small={
 'benzaldehyde':('기질','Benzaldehyde','O=Cc1ccccc1'),
 'acetophenone':('기질','Acetophenone','CC(=O)c1ccccc1'),
 'cyclohexanone':('기질','Cyclohexanone','O=C1CCCCC1'),
 'cyclohexanecarboxaldehyde':('기질','Cyclohexanecarboxaldehyde','O=CC1CCCCC1'),
 'styrene':('기질','Styrene','C=Cc1ccccc1'),
 'cyclohexene':('기질','Cyclohexene','C1=CCCCC1'),
 'cyclooctene':('기질','Cyclooctene','C1=CCCCCCC1'),
 'thioanisole':('기질','Thioanisole','CSc1ccccc1'),
 'pph3':('기질','Triphenylphosphine (PPh3)','P(c1ccccc1)(c1ccccc1)c1ccccc1'),
 'mecn':('기질','Acetonitrile (MeCN)','CC#N'),
 'benzonitrile':('기질','Benzonitrile','N#Cc1ccccc1'),
 'hotf':('산·시약','HOTf (TfOH)','OS(=O)(=O)C(F)(F)F'),
 'acoh':('산·시약','AcOH','CC(=O)O'),
 'hclo4':('산·시약','HClO4','OCl(=O)(=O)=O'),
 'hbf4':('산·시약','HBF4','[H+].[B-](F)(F)(F)F'),
 'h2o2':('산·시약','H2O2','OO'),
 'water':('산·시약','H2O','O')}
for slug,(category,name,smiles) in small.items():
    m=Chem.MolFromSmiles(smiles);assert m is not None
    if slug in ['hotf','acoh','hclo4','h2o2','water']:m=Chem.AddHs(m,onlyOnAtoms=[a.GetIdx() for a in m.GetAtoms() if a.GetAtomicNum()==8])
    save(slug,category,name,mol_document(m),'Explicit molecular graph (SMILES): '+smiles,formula=rdMolDescriptors.CalcMolFormula(m))
ppa_path=next(RESEARCH.rglob('InnoCORE_All_Figures.cdxml'))
save('2-ppa-source','기질','2-PPA (원본 CDXML)',cdxml_fragment(ppa_path,'27728'),str(ppa_path)+' #27728')
(OUT/'manifest.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2),encoding='utf-8')
header=['// Generated by tools/prepare_lab_templates.py; do not edit by hand.',
        '#pragma once','namespace yd {','struct LabTemplate {const char* category;const char* name;const char* data;const char* source;};',
        'inline const LabTemplate labTemplates[] = {']
for e in entries:
    data=(OUT/(e['slug']+'.ydraw')).read_text(encoding='utf-8')
    quote=lambda x:json.dumps(x,ensure_ascii=False)
    header.append('{'+quote(e['category'])+','+quote(e['name'])+',R"YDRAW('+data+')YDRAW",'+quote(e['source'])+'},')
header+=['};','}']
(ROOT/'src'/'lab_templates_data.h').write_text('\n'.join(header),encoding='utf-8')
print(f'Generated {len(entries)} editable templates.')
for e in entries:
    if e['category']=='리간드':print(e['name'],e.get('formula'),e.get('smiles'))
from refresh_coordination_templates import refresh
refresh()
