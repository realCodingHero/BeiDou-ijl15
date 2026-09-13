"""Port pinned CuNNy convolution weights to ps_3_0, one RGBA group per draw.

The network, UNORM activation quantization, padding and subpixel order are
unchanged. Splitting outputs avoids D3D9 instruction/constant-register limits.
"""
import argparse
import json
from pathlib import Path
import re


def parse_model(path):
    layers = []
    for part in re.split(r'//!PASS \d+', path.read_text(encoding='utf-8'))[1:]:
        inputs = re.search(r'//!IN (.*)', part)[1].strip().split(', ')
        final = 'out-shuffle' in part
        if final:
            inputs.remove('INPUT')
        groups = sorted(set(int(x) for x in re.findall(r'r(\d+) \+=', part)))
        outputs = [[] for group in groups]
        samples = {}
        # Upstream reuses s0_* variables for L2 after accumulating L0/L1.
        # Resolve each use at its position in the program, not from its name.
        tokens = re.finditer(r'(s\d+_\d_\d)\s*=\s*L(\d+)\(([-.\d]+),\s*([-.\d]+)\);|r(\d+) \+= (.*?);', part)
        for token in tokens:
            sample, texture, x, y, group, expr = token.groups()
            if sample:
                samples[sample] = dict(i=int(texture), x=int(float(x)), y=int(float(y)))
            else:
                terms = outputs[int(group)]
                numbers = lambda text: [float(n) for n in text.split(',')]
                matrix = re.fullmatch(r'mul\(s(\d+)_(\d)_(\d), M4\((.*?)\)\)', expr)
                scalar = re.fullmatch(r'V4\((.*?)\) \* s(\d+)_(\d)_(\d)', expr)
                bias = re.fullmatch(r'V4\((.*?)\)', expr)
                if matrix:
                    i, y, x, weights = matrix.groups()
                    terms.append(dict(**samples[f's{i}_{y}_{x}'], w=numbers(weights)))
                elif scalar:
                    weights, i, y, x = scalar.groups()
                    terms.append(dict(**samples[f's{i}_{y}_{x}'], w=numbers(weights)))
                elif bias:
                    terms.append(dict(bias=numbers(bias[1])))
                else:
                    raise ValueError(f'Unrecognized expression: {expr}')
        layers.append(dict(inputs=len(inputs), outputs=outputs, final=final))
    assert len(layers) == 4 and layers[0]['inputs'] == 1
    return layers


def shader(layer, terms, first):
    lines = ['// Generated from the pinned CuNNy NVL model; see third_party/cunny.',
             'float4 inputSize : register(c0);']
    for i in range(layer['inputs']):
        lines.append(f'sampler2D t{i} : register(s{i});')
    lines += ['float4 main(float2 uv : TEXCOORD0) : COLOR0 {', 'float4 r = 0;']
    fmt = lambda values: ','.join(format(v, '.9g') for v in values)
    for n, term in enumerate(terms):
        if 'bias' in term:
            lines.append(f'r += float4({fmt(term["bias"])});')
        else:
            lines.append(f'float4 s{n} = tex2D(t{term["i"]}, uv + float2({term["x"]},{term["y"]}) * inputSize.xy);')
            if first:
                lines.append(f'r += float4({fmt(term["w"])}) * dot(s{n}.rgb, float3(.299,.587,.114));')
            else:
                lines.append(f'r += mul(s{n}, float4x4({fmt(term["w"])}));')
    lines += ['return r;' if layer['final'] else 'return saturate(r);', '}']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.out.mkdir(parents=True, exist_ok=True)
    metadata = {}
    for quality, name in [('fast', 'veryfast'), ('balanced', 'fast')]:
        layers = parse_model(root / f'third_party/cunny/CuNNy-{name}-NVL.hlsl')
        metadata[quality] = layers
        for index, layer in enumerate(layers):
            for group, terms in enumerate(layer['outputs']):
                (args.out / f'{quality}-{index}-{group}.hlsl').write_text(shader(layer, terms, index == 0), encoding='utf-8')
    (args.out / 'models.json').write_text(json.dumps(metadata), encoding='utf-8')
    print('Generated two CuNNy models (unmodified trained weights).')


if __name__ == '__main__':
    main()
