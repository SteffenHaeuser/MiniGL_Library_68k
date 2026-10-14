#!/usr/bin/env python3
"""Source-only V29 ABI/token checks; run the target C checks separately."""
from pathlib import Path
import json, re

ROOT = Path(__file__).resolve().parents[1]
REF = json.loads((Path(__file__).with_name('v29_reference.json')).read_text())

OLD_REF = json.loads(json.dumps(REF))
NEW_REF = json.loads((Path(__file__).with_name('v291_reference.json')).read_text())
REF['v29_fields'] = NEW_REF['fields']
for obsolete in ['MGL_FLATFAN', 'MGL_FLATSTRIP']: REF['constants'].pop(obsolete, None)

def read(p): return (ROOT / p).read_bytes().decode('latin1').replace('\r\n','\n')
def clean(s): return re.sub(r'/\*.*?\*/|//[^\n]*', '', s, flags=re.S)
def norm(s): return ' '.join(s.split())
def require(value, message):
    if not value: raise AssertionError(message)
def fields(s):
    b = clean(s).split('typedef struct MGLDispatchTable {')[1].split('} MGLDispatchTable;')[0]
    return [norm(x)+';' for x in b.split(';') if x.strip()]
def name(f):
    m = re.search(r'\(\*(\w+)\)', f)
    return m[1] if m else re.search(r'(\w+);$', f)[1]
def table(s):
    b = re.search(r'const MGLDispatchTable \w+\s*=\s*\{(.*?)\};', clean(s), re.S)[1]
    return [norm(x) for x in b.split(',') if x.strip()]
def split_args(s):
    level = 0; start = 0; parts = []
    for i,c in enumerate(s):
        if c == '(': level += 1
        if c == ')': level -= 1
        if c == ',' and level == 0:
            parts.append(s[start:i].strip()); start = i+1
    parts.append(s[start:].strip())
    return parts

ALIASES = {'GLvoid':'void', 'GLbyte':'char', 'GLubyte':'unsigned char',
    'GLshort':'short', 'GLushort':'unsigned short', 'GLint':'int',
    'GLuint':'unsigned int', 'GLboolean':'unsigned int', 'GLenum':'unsigned int',
    'GLsizei':'long', 'GLbitfield':'unsigned long', 'GLfloat':'float',
    'GLclampf':'float', 'GLdouble':'double', 'GLclampd':'double'}
TYPE_WORDS = set('void char short int long float double signed unsigned const volatile'.split()) | set(ALIASES) | {'GLcontext','GLUquadricObj','MGLUfuncptr','MGLspecial','IdleFn','KeyHandlerFn','MouseHandlerFn','SpecialHandlerFn','MGLScreenModeCallback','MGLLockInfo'}
def simple_type(s):
    for a,b in ALIASES.items(): s = re.sub(r'\b'+a+r'\b',b,s)
    if '*' not in s: s = re.sub(r'\b(?:const|volatile)\b','',s)
    return tuple(re.findall(r'\w+|\*',s))
def parameter(s):
    callback = re.fullmatch(r'(.*?)\(\s*\*\s*\w*\s*\)\s*\((.*)\)',s,re.S)
    if callback:
        return ('callback',simple_type(callback[1]),tuple(parameter(x) for x in split_args(callback[2])))
    m = re.search(r'\b(\w+)\s*$',s)
    if m and m[1] not in TYPE_WORDS and not s.strip().startswith('struct '): s = s[:m.start()]
    elif m and s.strip().startswith('struct ') and len(re.findall(r'\w+',s))>2: s=s[:m.start()]
    return simple_type(s)
def signature(field):
    m = re.fullmatch(r'(.*?)\(\*(\w+)\)\((.*)\);',field,re.S)
    require(m,'Bad field declaration: '+field)
    return simple_type(m[1]),tuple(parameter(p) for p in split_args(m[3]))
def definition(source, target):
    # The shipped adapters and core prototypes use ordinary named C functions.
    m = re.search(r'(?m)^[ \t]*(?:(?:static|extern|inline|INLINE)\s+)*([\w \t*]+?)\b'+re.escape(target)+r'\s*\(',clean(source))
    require(m,'Missing function declaration: '+target)
    s = clean(source); start=m.end(); pos=start; depth=1
    while depth:
        require(pos<len(s),'Unbalanced declaration: '+target)
        if s[pos]=='(': depth+=1
        if s[pos]==')': depth-=1
        pos+=1
    return simple_type(m[1]),tuple(parameter(p) for p in split_args(s[start:pos-1]))

def verify_dispatch(header, sources):
    actual=fields(header)
    require(actual == REF['v29_fields'], 'V29 table order/signatures differ from supplied SDK')
    require(len(actual)==210,'Expected 205 functions plus five prefix fields')
    require([name(f) for f in actual[:158]] == [name(f) for f in REF['v27_fields']], 'V27 offsets changed')
    changed=[name(a) for a,b in zip(actual,REF['v27_fields']) if a!=b]
    require(changed==['MGLResizeContext'],'Unexpected change in existing signatures')
    for fn, s in sources.items():
        entries=table(s)
        require(entries[:158]==REF['v27_tables'][fn],fn+': old function binding changed')
        require(len(entries)==210,fn+': missing/extra table entry')
        for f,t in zip(actual[5:],entries[5:]):
            require(re.sub(r'^(?:PPCStub_|PPCV29_|PPC_|Stub_)','',t)==name(f),fn+': wrong binding '+t)
        decls=read('mglQ3/include/mgl/gl.h')+'\n'+s+'\n'+read('ppc_dll/minigl_v29_fallbacks.h')
        for f,t in zip(actual[5:],entries[5:]):
            require(signature(f)==definition(decls,t),fn+': signature mismatch '+t+'\n'+str(signature(f))+'\n'+str(definition(decls,t)))

def main():
    header=read('include/libraries/minigl_dispatch.h')
    sources={fn:read('ppc_dll/'+fn) for fn in REF['v27_tables']}
    verify_dispatch(header,sources)
    print('PASS: 205 fields and both DLL initializers; V27 slot positions preserved')
    print('PASS: all 410 function-pointer signatures checked, including callback parameters')
    require('#define MINIGL_DISPATCH_ABI_VERSION 5UL' in header,'Wrong ABI')
    require('#define MINIGL_BACKEND_FLAG_PPC       (1UL << 3)' in header,'Missing PPC flag')
    require('#pragma pack' not in header,'Do not pack the PPC table')
    for path in ['include/mgl/gl.h','mglQ3/include/mgl/gl.h']:
        values=dict(re.findall(r'^#define\s+((?:GL|MGL|GLU)_[A-Z0-9_]+)[ \t]+(0x[0-9a-fA-F]+|[0-9]+|(?:GL|MGL|GLU)_[A-Z0-9_]+)[ \t]*$',clean(read(path)),re.M))
        for n,v in REF['constants'].items(): require(values.get(n)==v,path+': token mismatch '+n)
        require(values['GL_BASE'] != values['GL_POINTS'],path+': primitive sentinel collision')
    print('PASS: all 375 GL/MGL/GLU token definitions match V29 in client AND backend')
    loader=read('client/minigl_ppc_open.c')
    require('dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION' in loader,'Loader must demand exact ABI equality')
    require('dispatch->structSize < (ULONG)sizeof(MGLDispatchTable)' in loader,'Missing size guard')
    version=read('include/libraries/minigl.h')
    require('#define MINIGL_VERSION 29' in version and '#define MINIGL_MIN_VERSION 29' in version,'Wrong release version')
    for f in REF['v29_fields'][5:]: require('MiniGLDispatch->'+name(f)+'(' in header,'Missing wrapper '+name(f))
    require('return MiniGLDispatch->MGLResizeContext(' in header,'Resize result lost')
    print('PASS: ABI 5 equality, version 29, all wrappers and changed resize return contract')
    for p in (ROOT/'mglQ3/include/mgl').glob('*.h'):
        forwarding=read('mglQ3/wos_include/mgl/'+p.name)
        require('../../include/mgl/'+p.name in forwarding,'Wrong private forwarding header')
    for p in (ROOT/'include/mgl').glob('*.h'):
        for inc in re.findall(r'#include "([^"\n]*backend/[^"\n]+)"',p.read_text(encoding='latin1')):
            require((p.parent/inc).is_file(),'Missing SDK dependency '+inc)
    for fn in ['Makefile.dll.gcc','Makefile.dll.stub.gcc']:
        s=read(fn)
        require('-ImglQ3/wos_include -Iinclude' in s,'DLL must use private Warp3D context: '+fn)
        require('obj/v29_1/' in s,'Fresh V29 objects required: '+fn)
    require('-Iwos_include -I../include' in read('mglQ3/Makefile.wos.gcc'),'Wrong backend include order')
    require('libmgl-wos-v29_1.a' in read('Makefile.dll.gcc'),'V27 backend archive must not be reused')
    require('defined(WARPUP) || defined(__PPC__) || defined(__powerpc__)' in read('backend/include/v3d_types.h'),'Public SDK byte-swap helpers must have a PPC branch')
    print('PASS: public/private include separation, SDK dependencies and fresh V29 build paths')
    ctx=read('mglQ3/src/context.c'); tex=read('mglQ3/src/texture.c')
    for lib in ['intuition.library','graphics.library','utility.library','Warp3DPPC.library']: require(lib in ctx,'Lost PPC bootstrap '+lib)
    for marker in ['GLTexImage2DLevelN','ARGBFORM4444D','case 0x20003']: require(marker in tex,'Lost WOS texture behavior '+marker)
    require('unit >= GL_TEXTURE0_ARB + MAX_TEXUNIT' in tex,'V29 texture-unit bounds missing')
    require(read('mglQ3/src/vertexbuffer.c').count('u>=MAX_TEXUNIT')==2,'V29 coordinate-unit bounds missing')
    require('if(or_codes & MGL_CLIP_NEGW)' in read('mglQ3/src/aclip.c'),'Lost NEGW fix')
    require('context->w3dBitMap && !context->externalBitMap' in ctx,'Lost borrowed bitmap ownership')
    require('if (context->externalWindow) {' in ctx,'Lost bitmap scissor fix')
    print('PASS: WOS V27 rendering fixes retained; V29 unit bounds applied')
    # Check that deliberate ABI defects fail the same validation path.
    bad=header.replace(REF['v29_fields'][-1], 'void (*WrongSlot)(void);')
    changed=dict(sources); fn=next(iter(changed)); changed[fn]=changed[fn].replace('    GLAlphaFunc,','    GLBegin,',1)
    for h,ss in [(bad,sources),(header,changed)]:
        try: verify_dispatch(h,ss)
        except AssertionError: pass
        else: raise AssertionError('Checker accepted an intentionally broken table')
    verify_291(header, sources)
    print('PASS: negative checks reject a wrong slot and a wrong function binding')
    print('Source checks only. PPC compile/link/runtime remain separate requirements.')

def verify_291(header, sources):
    require(fields(header)[:-1] == OLD_REF['v29_fields'], '29.0 prefix changed')
    for fn,s in sources.items(): require(table(s)[:-1] == NEW_REF['old_tables'][fn], '29.0 binding changed: '+fn)
    vectors = ['Vertex2fv','Vertex3fv','Vertex4fv','Color3fv','Color4fv','Color3ubv','Color4ubv','TexCoord2fv','TexCoord4fv','MultiTexCoord2fvARB']
    for path,prefix in [('mglQ3/src/vertexbuffer.c','GL'),('mglQ3/include/mgl/minigl.h','gl')]:
        source=clean(read(path))
        for n in vectors:
            require(re.search(r'void\s+'+prefix+n+r'\([^)]*\)\s*\{\s*if\s*\(!v\)\s*return;',source), 'Missing leading NULL guard '+prefix+n)
    require(re.search(r'void GLNormal3fv\([^)]*\)\s*\{\s*if \(!n\) return;\s*GLNormal3f\(context, n\[0\], n\[1\], n\[2\]\);',read('mglQ3/src/vertexbuffer.c')), 'Normal vector delegation missing')
    for p in (ROOT/'mglQ3').rglob('*'):
        if p.suffix in ['.c','.h']: require(not re.search(r'MGL_FLAT(?:FAN|STRIP)|[dAE]_DrawFlat',p.read_text(encoding='latin1')), 'Legacy flat path: '+str(p))
    for path in ['mglQ3/src/vertexarray.c','mglQ3/src/vertexelements.c']:
        require('mode == 0x7013 || mode == 0x7014' in read(path), 'Missing legacy enum rejection')
    require('#define MINIGL_REVISION 1' in read('include/libraries/minigl.h'), 'Wrong revision')
    print('PASS: 29.0 prefix/bindings intact, SDK 29.1 slot, NULL guards and removed flat paths')

if __name__=='__main__': main()
