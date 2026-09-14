"""Independent NumPy inference, using the upstream model's arithmetic/UNORM activations."""
import re
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

folder = Path(sys.argv[1])
folder.mkdir(parents=True, exist_ok=True)
if sys.argv[2] == 'prepare':
    rng = np.random.default_rng(7201080)
    pixels = rng.integers(0,256,(48,64,4),dtype=np.uint8)
    pixels[:,:,3]=255
    pixels[:8,:8,:3]=0
    pixels[:8,8:16,:3]=255
    (folder/'input.bgra').write_bytes(pixels.tobytes())
    sys.exit(0)

source = np.frombuffer((folder/'input.bgra').read_bytes(),np.uint8).reshape(48,64,4)[:,:,[2,1,0,3]].astype(np.float32)/255
model_root=Path(__file__).resolve().parents[2]/'third_party/cunny'

def inference(path):
    # Interpret upstream statement order directly. Do not consume the generator's
    # parsed weights/metadata: this catches shader-translation mistakes too.
    textures={'INPUT':np.sum(source[:,:,:3]*[.299,.587,.114],axis=2,keepdims=True).astype(np.float32)}
    for part in re.split(r'//!PASS \d+', path.read_text())[1:]:
        names=re.search(r'//!IN (.*)',part)[1].strip().split(', ')
        if 'out-shuffle' in part: names.remove('INPUT')
        samplers={f'L{i}':np.pad(textures[name],((1,1),(1,1),(0,0)),mode='edge') for i,name in enumerate(names)}
        values={f'r{i}':np.zeros((48,64,4),np.float32) for i in range(3)}
        samples={}
        # Tokens include sample-register reassignments and texture stores.
        for m in re.finditer(r'(s\d+_\d_\d = L\d+\(.*?\)|r\d+ \+= .*?|T\d+\[gxy\] = r\d+);',part):
            statement=m[1]
            if statement.startswith('s'):
                name,op=statement.split(' = ')
                sampler,xy=op[:-1].split('(')
                x,y=map(lambda v:int(float(v)),xy.split(','))
                samples[name]=samplers[sampler][y+1:y+49,x+1:x+65]
            elif statement.startswith('T'):
                texture,result=statement.split('[gxy] = ')
                textures[texture]=np.rint(np.clip(values[result],0,1)*255)/255
            else:
                result,op=statement.split(' += ')
                weights=np.array([float(x) for x in re.search(r'(?:V4|M4)\((.*?)\)',op)[1].split(',')],np.float32)
                if op.startswith('mul('):
                    name=op[4:op.index(',')]
                    values[result]+=samples[name]@weights.reshape(4,4)
                elif ' * ' in op:
                    values[result]+=samples[op.split(' * ')[1]]*weights
                else:
                    values[result]+=weights
    final=values['r0'].astype(np.float16).astype(np.float32)
    residual=np.empty((96,128),np.float32)
    for i in range(4):residual[i//2::2,i%2::2]=final[:,:,i]
    # Bilinear reconstruction with clamp addressing and pixel-center mapping.
    x=(np.arange(128)+.5)/2-.5;y=(np.arange(96)+.5)/2-.5
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int)
    fx=(x-x0)[None,:,None];fy=(y-y0)[:,None,None]
    a=source[np.clip(y0,0,47)[:,None],np.clip(x0,0,63)[None,:],:3]
    b=source[np.clip(y0,0,47)[:,None],np.clip(x0+1,0,63)[None,:],:3]
    c=source[np.clip(y0+1,0,47)[:,None],np.clip(x0,0,63)[None,:],:3]
    d=source[np.clip(y0+1,0,47)[:,None],np.clip(x0+1,0,63)[None,:],:3]
    rgb=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy
    yuv=rgb@np.array([[.299,.587,.114],[-.169,-.331,.5],[.5,-.419,-.081]]).T
    yuv[:,:,0]=np.clip(yuv[:,:,0]+residual,0,1)
    rgb=yuv@np.array([[1,-.00093,1.401687],[1,-.3437,-.71417],[1,1.77216,.00099]]).T
    return np.rint(np.clip(rgb,0,1)*255).astype(np.uint8)

for quality,model in [('fast','veryfast'),('balanced','fast')]:
    expected=inference(model_root/f'CuNNy-{model}-NVL.hlsl')
    actual=np.frombuffer((folder/f'{quality}.bgra').read_bytes(),np.uint8).reshape(96,128,4)[:,:,[2,1,0]]
    difference=np.abs(expected.astype(int)-actual.astype(int))
    print(f'{quality}: reference diff mean={difference.mean():.4f}, p99={np.percentile(difference,99):.1f}, max={difference.max()}')
    Image.fromarray(actual).save(folder/f'{quality}.png')
    Image.fromarray(expected).save(folder/f'{quality}-reference.png')
    assert difference.mean()<1.2 and np.percentile(difference,99)<=6, 'GPU inference differs from reference'
assert (folder/'identity.bgra').read_bytes()==(folder/'input.bgra').read_bytes(), '1:1 output changed pixels'
print('PASS independent neural reference and pixel-exact identity')

# Independently verify the lightweight GPU path's pixel-center mapping, interpolation,
# and edge clamping. This also detects accidental nearest-neighbor scaling.
raw = np.frombuffer((folder/'input.bgra').read_bytes(), np.uint8).reshape(48, 64, 4)
for width, height in [(64,48), (96,72), (32,24), (128,96), (106,80)]:
    x = (np.arange(width)+.5)*64/width-.5
    y = (np.arange(height)+.5)*48/height-.5
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int)
    fx = (x-x0)[None,:,None]; fy = (y-y0)[:,None,None]
    a = raw[np.clip(y0,0,47)[:,None], np.clip(x0,0,63)[None,:]].astype(float)
    b = raw[np.clip(y0,0,47)[:,None], np.clip(x0+1,0,63)[None,:]].astype(float)
    c = raw[np.clip(y0+1,0,47)[:,None], np.clip(x0,0,63)[None,:]].astype(float)
    d = raw[np.clip(y0+1,0,47)[:,None], np.clip(x0+1,0,63)[None,:]].astype(float)
    expected = np.rint((a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy)
    actual = np.frombuffer((folder/f'linear-{width}x{height}.bgra').read_bytes(),np.uint8).reshape(height,width,4)
    difference = np.abs(expected-actual)
    assert difference.mean()<.75 and np.percentile(difference,99)<=2, 'GPU filtering differs from bilinear reference'
    if width==64 and height==48:
        assert np.array_equal(raw, actual), 'Direct 1:1 copy changed pixels'
    print(f'PASS independent linear {width}x{height}: mean={difference.mean():.4f}, max={difference.max()}')


def resize_axis(pixels, extent, axis):
    # Full wide kernel, independent of the GPU's six-tap/LUT optimization.
    moved=np.moveaxis(pixels,axis,0).astype(np.float64)
    scale=max(1.,moved.shape[0]/extent)
    stride=max(1.,np.ceil(scale/4))
    output=[]
    for n in range(extent):
        p=(n+.5)*len(moved)/extent-.5
        locations=np.floor(p)+np.arange(-8,9)*stride
        distance=np.abs((locations-p)/scale)
        weights=np.sinc(distance)*np.sinc(distance/2)
        weights[distance>=2]=0
        samples=moved[np.clip(locations.astype(int),0,len(moved)-1)]
        value=np.sum(samples*weights[:,None,None],axis=0)/weights.sum()
        a,b=moved[np.clip([int(np.floor(p)),int(np.floor(p))+1],0,len(moved)-1)]
        output.append(np.rint(np.clip(value,np.minimum(a,b),np.maximum(a,b))))
    return np.moveaxis(np.array(output,dtype=np.uint8),0,axis)


neural=np.frombuffer((folder/'balanced.bgra').read_bytes(),np.uint8).reshape(96,128,4)
original=np.frombuffer((folder/'input.bgra').read_bytes(),np.uint8).reshape(48,64,4)
for pixels,width,height in [(neural,96,72),(original,32,24)]:
    expected=resize_axis(resize_axis(pixels,width,1),height,0)
    actual=np.frombuffer((folder/f'{width}x{height}.bgra').read_bytes(),np.uint8).reshape(height,width,4)
    difference=np.abs(expected.astype(int)-actual.astype(int))
    assert difference.mean()<.5 and np.percentile(difference,99)<=2, 'Lanczos resizing differs from reference'
    print(f'PASS independent resample {width}x{height}: mean={difference.mean():.4f}, max={difference.max()}')
