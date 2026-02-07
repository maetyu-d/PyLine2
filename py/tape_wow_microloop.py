import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Tape wow microloop:
    - build a tiny loop with tone+noise
    - read it back with wow/flutter (phase modulation) + occasional splice clicks
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    loop_len = int(rng.uniform(0.03, 0.18) * sr)
    loop_len = max(256, min(loop_len, max(256, n)))
    tL = np.linspace(0, loop_len/sr, loop_len, endpoint=False, dtype=np.float32)

    f1 = float(rng.uniform(120, 420))
    f2 = float(rng.uniform(700, 1600))
    loop = (0.26*np.sin(2*np.pi*f1*tL) + 0.10*np.sin(2*np.pi*f2*tL) +
            0.10*rng.standard_normal(loop_len).astype(np.float32)).astype(np.float32)
    loop *= np.hanning(loop_len).astype(np.float32)

    # wow/flutter
    wow_hz = float(rng.uniform(0.2, 0.9))
    flut_hz = float(rng.uniform(5.0, 11.0))
    t = np.linspace(0, duration, n, endpoint=False, dtype=np.float32)
    wow = 0.008*np.sin(2*np.pi*wow_hz*t + float(rng.uniform(0, 6.28)))
    flut = 0.0015*np.sin(2*np.pi*flut_hz*t + float(rng.uniform(0, 6.28)))
    pm = wow + flut

    idx = (np.arange(n, dtype=np.float32) + pm*sr) % loop_len
    i0 = np.floor(idx).astype(np.int32)
    i1 = (i0 + 1) % loop_len
    frac = (idx - i0).astype(np.float32)
    y = (loop[i0]*(1-frac) + loop[i1]*frac).astype(np.float32)

    # splice clicks
    clicks = max(2, int(duration * 5))
    for _ in range(clicks):
        p = int(rng.integers(0, n))
        y[p:min(n, p+3)] += np.array([0.8, -0.4, 0.2], dtype=np.float32)[:max(0, min(n, p+3)-p)]

    y = np.tanh(y * 1.3).astype(np.float32) * 0.85

    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
