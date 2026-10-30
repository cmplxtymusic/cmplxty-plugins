"""Appends Rewired's 11-group instrument head to instruments.onnx: groups = softmax(log(scores + 1e-4) @ W^T + b)."""
import onnx, numpy as np, sys
from onnx import helper, numpy_helper as nh
src,head,dst=sys.argv[1:4]; m=onnx.load(src); g=m.graph; h=np.load(head)
W=h['W'].T.astype(np.float32); b=h['b'].astype(np.float32)
g.initializer.extend([nh.from_array(np.array([1e-4],np.float32),'rw_eps'),nh.from_array(W,'rw_W'),nh.from_array(b,'rw_b')])
g.node.extend([helper.make_node('Add',['scores','rw_eps'],['rw_a']),helper.make_node('Log',['rw_a'],['rw_l']),
  helper.make_node('MatMul',['rw_l','rw_W'],['rw_m']),helper.make_node('Add',['rw_m','rw_b'],['rw_z']),helper.make_node('Softmax',['rw_z'],['groups'],axis=-1)])
g.output.append(helper.make_tensor_value_info('groups',onnx.TensorProto.FLOAT,[None,W.shape[1]]))
onnx.checker.check_model(m); onnx.save(m,dst)
