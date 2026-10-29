#!/usr/bin/env python3
"""Convert Deezer Spleeter 4stems (MIT, code + weights) TF checkpoint to ONNX.
Output graph: input mag [1,2,T,1024] (T multiple of 64) -> est [4,1,2,T,1024]
(vocals, drums, bass, other estimated magnitudes). Weights stored fp16, cast to fp32."""
import sys, numpy as np, onnx
from onnx import helper as h, TensorProto as TP, numpy_helper as nh
import tensorflow as tf
ck = sys.argv[1]; out = sys.argv[2]; fp16 = '--fp32' not in sys.argv
r = tf.train.load_checkpoint(ck)
g = lambda n: r.get_tensor(n).astype(np.float32)
nodes, inits = [], []
def W(name, arr):
    if fp16:
        inits.append(nh.from_array(arr.astype(np.float16), name + '_h'))
        nodes.append(h.make_node('Cast', [name + '_h'], [name], to=TP.FLOAT))
    else:
        inits.append(nh.from_array(arr.astype(np.float32), name))
    return name
uid = [0]
def U(p): uid[0] += 1; return f'{p}{uid[0]}'
def nm(base, i): return base if i == 0 else f'{base}_{i}'
def conv(x, ci, stride=2, pads=(1,1,2,2), dil=1):
    k = g(nm('conv2d', ci) + '/kernel').transpose(3, 2, 0, 1); b = g(nm('conv2d', ci) + '/bias')
    o = U('c'); nodes.append(h.make_node('Conv', [x, W(o+'w', k), W(o+'b', b)], [o], strides=[stride]*2, pads=list(pads), dilations=[dil]*2)); return o
def deconv(x, di):
    k = g(nm('conv2d_transpose', di) + '/kernel').transpose(3, 2, 0, 1); b = g(nm('conv2d_transpose', di) + '/bias')
    o = U('d'); nodes.append(h.make_node('ConvTranspose', [x, W(o+'w', k), W(o+'b', b)], [o], strides=[2,2], pads=[1,1,2,2])); return o
def bn(x, bi):
    p = nm('batch_normalization', bi)
    o = U('bn'); nodes.append(h.make_node('BatchNormalization', [x, W(o+'g', g(p+'/gamma')), W(o+'be', g(p+'/beta')), W(o+'m', g(p+'/moving_mean')), W(o+'v', g(p+'/moving_variance'))], [o], epsilon=1e-3)); return o
def elu(x): o = U('e'); nodes.append(h.make_node('Elu', [x], [o], alpha=1.0)); return o
def cat(a, b): o = U('k'); nodes.append(h.make_node('Concat', [a, b], [o], axis=1)); return o
outs = []
for inst in range(4):
    c0, d0, b0 = 7*inst, 6*inst, 12*inst
    x = 'mag'; convs = []
    for i in range(6):
        c = conv(x, c0+i); convs.append(c); x = elu(bn(c, b0+i))
    up = convs[5]
    for j in range(6):
        u = deconv(up, d0+j); u = bn(elu(u), b0+6+j)
        up = cat(convs[4-j], u) if j < 5 else u
    m = conv(up, c0+6, stride=1, pads=(3,3,3,3), dil=2)
    s = U('s'); nodes.append(h.make_node('Sigmoid', [m], [s]))
    e = U('est'); nodes.append(h.make_node('Mul', [s, 'mag'], [e]))
    uq = U('u'); nodes.append(h.make_node('Unsqueeze', [e, 'ax0'], [uq])); outs.append(uq)
inits.append(nh.from_array(np.array([0], np.int64), 'ax0'))
nodes.append(h.make_node('Concat', outs, ['est'], axis=0))
graph = h.make_graph(nodes, 'spleeter4', [h.make_tensor_value_info('mag', TP.FLOAT, [1, 2, 'T', 1024])],
                     [h.make_tensor_value_info('est', TP.FLOAT, [4, 1, 2, 'T', 1024])], inits)
model = h.make_model(graph, opset_imports=[h.make_opsetid('', 17)], producer_name='cmplxty-rewired')
model.ir_version = 8
onnx.checker.check_model(model); onnx.save(model, out); print('saved', out)

if '--verify' in sys.argv:
    # independent Keras rebuild following spleeter/model/functions/unet.py exactly
    from tensorflow.keras import layers as L
    rng = np.random.default_rng(0); T = 128
    x = (np.abs(rng.standard_normal((1, T, 1024, 2))) * 0.3).astype(np.float32)
    inp = tf.constant(x); res = []
    for inst in range(4):
        c0, d0, b0 = 7*inst, 6*inst, 12*inst
        def C(i, t, **kw):
            l = L.Conv2D(r.get_tensor(nm('conv2d', i)+'/kernel').shape[-1], r.get_tensor(nm('conv2d', i)+'/kernel').shape[:2], padding='same', **kw); y = l(t)
            l.set_weights([r.get_tensor(nm('conv2d', i)+'/kernel'), r.get_tensor(nm('conv2d', i)+'/bias')]); return l(t)
        def D(i, t):
            k = r.get_tensor(nm('conv2d_transpose', i)+'/kernel'); l = L.Conv2DTranspose(k.shape[2], (5,5), strides=2, padding='same'); l(t)
            l.set_weights([k, r.get_tensor(nm('conv2d_transpose', i)+'/bias')]); return l(t)
        def B(i, t):
            p = nm('batch_normalization', i); l = L.BatchNormalization(axis=-1); l(t)
            l.set_weights([r.get_tensor(p+'/gamma'), r.get_tensor(p+'/beta'), r.get_tensor(p+'/moving_mean'), r.get_tensor(p+'/moving_variance')]); return l(t, training=False)
        t = inp; cs = []
        for i in range(6):
            c = C(c0+i, t, strides=2); cs.append(c); t = tf.nn.elu(B(b0+i, c))
        up = cs[5]
        for j in range(6):
            u = B(b0+6+j, tf.nn.elu(D(d0+j, up)))
            up = tf.concat([cs[4-j], u], -1) if j < 5 else u
        m = tf.sigmoid(C(c0+6, up, dilation_rate=2)); res.append((m*inp).numpy())
    ref = np.stack(res)  # 4,1,T,F,2
    import onnxruntime as ort
    s = ort.InferenceSession(out); o = s.run(None, {'mag': x.transpose(0, 3, 1, 2)})[0]  # 4,1,2,T,F
    o = o.transpose(0, 1, 3, 4, 2)
    print('max abs diff', float(np.abs(o-ref).max()), 'ref max', float(np.abs(ref).max()))
