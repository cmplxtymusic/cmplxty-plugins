import tf_keras
import numpy as np, tensorflow as tf, tf2onnx, onnx
import params as P, yamnet as Y, features as F
p=P.Params()
mel=tf.signal.linear_to_mel_weight_matrix(num_mel_bins=64,num_spectrogram_bins=257,sample_rate=16000,lower_edge_hertz=125,upper_edge_hertz=7500)
inp=tf_keras.layers.Input(shape=(96,257),dtype=tf.float32)
x=tf_keras.layers.Lambda(lambda m: tf.math.log(tf.matmul(m,mel)+0.001))(inp)
pred,emb=Y.yamnet(x,p)
m=tf_keras.Model(inp,pred); m.load_weights('yamnet.h5')
spec=[tf.TensorSpec((None,96,257),tf.float32,name='mag')]
mp,_=tf2onnx.convert.from_keras(m,input_signature=spec,opset=17,output_path='yamnet_mag.onnx')
# reference check vs official waveform pipeline
wm=Y.yamnet_frames_model(p); wm.load_weights('yamnet.h5')
rng=np.random.default_rng(0); w=(rng.standard_normal(16000*3)*0.1).astype(np.float32)
ref,_,_=wm(w)
np.save('ref_scores.npy',ref.numpy()); np.save('ref_wave.npy',w)
print(ref.shape)
