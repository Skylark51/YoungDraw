"""Verify an actual UI-produced PPTX against the generated graph and mechanism inputs."""
import argparse
import io
import xml.etree.ElementTree as E
import zipfile
from pathlib import Path
import olefile
parser=argparse.ArgumentParser()
parser.add_argument('presentation',type=Path)
parser.add_argument('--graph',type=Path,required=True)
parser.add_argument('--mechanism',type=Path,required=True)
args=parser.parse_args()
with zipfile.ZipFile(args.presentation) as package:
    docs=[]
    for name in package.namelist():
        if name.startswith('ppt/embeddings/'):
            with olefile.OleFileIO(io.BytesIO(package.read(name))) as source:
                assert any(p[-1].startswith('\x02OlePres') for p in source.listdir()), 'missing presentation cache'
                docs.append(E.fromstring(source.openstream('YoungDrawDocument').read()))
    assert len(docs)==2, 'expected graph and mechanism objects'
    assert sum(n.endswith('.emf') for n in package.namelist())==2, 'expected two vector previews'
graph=next(d for d in docs if d.find('plot') is not None)
mechanism=next(d for d in docs if d.find('reaction') is not None)
original_graph=E.parse(args.graph).getroot()
original_mechanism=E.parse(args.mechanism).getroot()
assert E.tostring(graph.find('plot'))==E.tostring(original_graph.find('plot')), 'graph data or formatting changed'
assert any('Verified-20261116' in o.get('text','') for o in graph.findall('object')), 're-edit marker missing'
for tag,keys in [('atom',['id','e','q','iso','h','radical','label','protect']),('bond',['id','a','b','kind','customOrder','customDisplay'])]:
    actual=[tuple(o.get(k) for k in keys) for o in mechanism.findall(tag)]
    expected=[tuple(o.get(k) for k in keys) for o in original_mechanism.findall(tag)]
    assert actual==expected, tag+' chemistry changed'
old=original_mechanism.find('reaction');new=mechanism.find('reaction')
assert len(new.findall('slot'))==len(old.findall('slot'))+1, 'added branch not persisted'
print('PASS actual UI flow: graph formatting/data unchanged, edit marker saved, mechanism branch added, original atom/bond chemistry and vector caches preserved')
