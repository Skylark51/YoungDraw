"""Attach existing YoungDraw OLE sources to a hidden PowerPoint-created deck, then verify preserved source streams."""
from pathlib import Path
from collections import Counter
from copy import deepcopy
import hashlib
import io
import sys
import zipfile
import xml.etree.ElementTree as E
import olefile
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/powerpoint-background'
SOURCES=ROOT/'build/release-v04-final/office-storage-verification'
P='http://schemas.openxmlformats.org/presentationml/2006/main'
A='http://schemas.openxmlformats.org/drawingml/2006/main'
R='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
REL='http://schemas.openxmlformats.org/package/2006/relationships'
CT='http://schemas.openxmlformats.org/package/2006/content-types'
for prefix,uri in [('p',P),('a',A),('r',R)]: E.register_namespace(prefix,uri)
def native(data):
    with olefile.OleFileIO(io.BytesIO(data)) as ole:
        assert ole.exists('YoungDrawDocument'), 'missing editing source'
        assert any(name[-1].startswith('\x02OlePres') for name in ole.listdir()), 'missing cached presentation'
        return hashlib.sha256(ole.openstream('YoungDrawDocument').read()).hexdigest()
if len(sys.argv)>1 and sys.argv[1]=='prepare':
    with zipfile.ZipFile(OUT/'template.pptx') as z: files={n:z.read(n) for n in z.namelist()}
    for i in range(6):
        slide=f'ppt/slides/slide{i+1}.xml'; root=E.fromstring(files[slide]); tree=root.find(f'.//{{{P}}}spTree');pic=tree.find(f'{{{P}}}pic'); assert pic is not None
        frame=E.Element(f'{{{P}}}graphicFrame'); nv=E.SubElement(frame,f'{{{P}}}nvGraphicFramePr')
        E.SubElement(nv,f'{{{P}}}cNvPr',{'id':'10','name':f'YoungDraw {i}'})
        E.SubElement(nv,f'{{{P}}}cNvGraphicFramePr');E.SubElement(nv,f'{{{P}}}nvPr')
        xf=E.SubElement(frame,f'{{{P}}}xfrm');src=pic.find(f'{{{P}}}spPr/{{{A}}}xfrm')
        for child in src: xf.append(deepcopy(child))
        graphic=E.SubElement(frame,f'{{{A}}}graphic');gd=E.SubElement(graphic,f'{{{A}}}graphicData',{'uri':P.rsplit('/',1)[0]+'/ole'})
        obj=E.SubElement(gd,f'{{{P}}}oleObj',{'name':'YoungDraw Drawing','progId':'YoungDraw.Drawing.1',f'{{{R}}}id':'rIdYoungDraw','imgW':'8255000','imgH':'5715000'})
        E.SubElement(obj,f'{{{P}}}embed');obj.append(deepcopy(pic));tree.remove(pic);tree.append(frame)
        files[slide]=E.tostring(root,encoding='utf-8',xml_declaration=True)
        rel=f'ppt/slides/_rels/slide{i+1}.xml.rels';rels=E.fromstring(files[rel]);E.SubElement(rels,f'{{{REL}}}Relationship',{'Id':'rIdYoungDraw','Type':R+'/oleObject','Target':f'../embeddings/oleObject{i+1}.bin'})
        files[rel]=E.tostring(rels,encoding='utf-8',xml_declaration=True)
        files[f'ppt/embeddings/oleObject{i+1}.bin']=(SOURCES/f'{i}.ole').read_bytes()
    ct=E.fromstring(files['[Content_Types].xml']);E.SubElement(ct,f'{{{CT}}}Default',{'Extension':'bin','ContentType':'application/vnd.openxmlformats-officedocument.oleObject'})
    files['[Content_Types].xml']=E.tostring(ct,encoding='utf-8',xml_declaration=True)
    with zipfile.ZipFile(OUT/'embedded.pptx','w',zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items(): z.writestr(name,data)
    print('Prepared six editable OLE objects with vector previews')
else:
    expected=Counter(native((SOURCES/f'{i}.ole').read_bytes()) for i in range(6))
    with zipfile.ZipFile(OUT/'reopened.pptx') as z:
        objects=[n for n in z.namelist() if n.startswith('ppt/embeddings/')]
        actual=Counter(native(z.read(n)) for n in objects)
        assert actual==expected, 'PowerPoint changed or lost editing sources'
        assert sum(n.endswith('.emf') for n in z.namelist())==6, 'PowerPoint dropped EMF previews'
    print('PASS actual PowerPoint save/reopen: six exact editing sources and presentation caches preserved; vector previews present')
