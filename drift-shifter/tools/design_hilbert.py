#!/usr/bin/env python3
"""Designs the allpass-pair (IIR) Hilbert transformer used by Drift's single-sideband shifter.
   Path I : z^-1 * prod_k (a_k^2 - z^-2)/(1 - a_k^2 z^-2)   (the 'in-phase' branch)
   Path Q :        prod_k (b_k^2 - z^-2)/(1 - b_k^2 z^-2)   (the 'quadrature' branch, leads/lags I by 90 degrees)
   Minimises the worst phase error over [f_lo, 0.5 - f_lo] (fractions of the sample rate, log-spaced at the low end)."""
import numpy as np, sys
from scipy.optimize import least_squares, minimize
N = int(sys.argv[1]) if len(sys.argv) > 1 else 6
FLO = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0007
f = np.unique(np.concatenate([np.geomspace(FLO, 0.25, 600), 0.5 - np.geomspace(FLO, 0.25, 600)]))
w = 2 * np.pi * f
def ph(c, delay):
    z2 = np.exp(-2j * w); p = np.ones_like(z2)
    for a in c: p = p * (a * a - z2) / (1 - a * a * z2)
    if delay: p = p * np.exp(-1j * w)
    return np.angle(p)
def err(x):
    a, b = x[:N], x[N:]
    d = np.unwrap(ph(a, True) - ph(b, False))
    # accept either +90 or -90 (mod 360): pick the branch nearest
    e = d - np.round((d + np.pi/2) / (2*np.pi)) * 2*np.pi
    return e + np.pi/2
# start values: geometric spacing of section 'frequencies'
def init(n, off):
    r = np.geomspace(0.002, 0.45, n) ; return np.clip(np.tan(np.pi * r * off), 0.05, 0.9995)
def unpack(u):  # u = log(1 - a^2)
    return np.sqrt(1 - np.exp(u))
best = None
for hi in (0.5, 0.6):
  for lo in (6e-4, 2e-4, 1e-3):
    uA = np.log(np.geomspace(hi, lo, N)); uB = np.log(np.geomspace(hi*1.9 if hi*1.9<0.97 else 0.97, lo*3, N))
    u0 = np.concatenate([uA, uB])
    r = least_squares(lambda u: err(unpack(u)), u0, xtol=1e-15, ftol=1e-15, max_nfev=3000)
    x = unpack(r.x); m = np.max(np.abs(err(x)))
    if best is None or m < best[0]: best = (m, x)
m, x = best
# minimax polish
res = minimize(lambda x: np.max(np.abs(err(x))), x, method='Nelder-Mead', options={'xatol':1e-13,'fatol':1e-13,'maxiter':20000,'maxfev':40000})
if res.fun < m: m, x = res.fun, res.x
a, b = np.sort(x[:N]), np.sort(x[N:])
e = np.max(np.abs(err(np.concatenate([a, b]))))
print(f"// N={N} sections per path, band {FLO} .. {0.5-FLO} x fs, worst phase error {np.degrees(e):.4f} deg  (image rejection ~{-20*np.log10(np.tan(e/2)):.1f} dB)")
print("constexpr double kHilbertI[%d] = { %s };" % (N, ", ".join(repr(float(v)) for v in a)))
print("constexpr double kHilbertQ[%d] = { %s };" % (N, ", ".join(repr(float(v)) for v in b)))
