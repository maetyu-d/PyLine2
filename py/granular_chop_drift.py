import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Granular chop drift:
    - generates a noisy tonal bed
    - slices it into overlapping grains
    - replays grains with random drift, reversal, and spacing -> cut-up shimmer
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    # bed
    t = np.linspace(0, duration, n, endpoint=False, dtype=np.float32)
    bed = (0.18*np.sin(2*np.pi*float(rng.uniform(120, 420))*t) +
           0.08*np.sin(2*np.pi*float(rng.uniform(600, 1800))*t) +
           0.18*rng.standard_normal(n).astype(np.float32)).astype(np.float32)
    bed = np.tanh(bed * 1.2).astype(np.float32) * 0.8

    # grain params
    gmin = int(0.008 * sr)
    gmax = int(0.05 * sr)
    hop = int(0.006 * sr)

    y = np.zeros(n, dtype=np.float32)
    pos = 0
    while pos < n:
        gl = int(rng.integers(gmin, gmax))
        a = int(rng.integers(0, max(1, n - gl)))
        grain = bed[a:a+gl].copy()

        if rng.random() < 0.35:
            grain = grain[::-1].copy()
        if rng.random() < 0.22:
            # drift pitch by simple index warp
            warp = float(rng.uniform(0.7, 1.5))
            idx = (np.arange(gl, dtype=np.float32) * warp) % gl
            i0 = np.floor(idx).astype(np.int32)
            i1 = (i0 + 1) % gl
            frac = (idx - i0).astype(np.float32)
            grain = (grain[i0]*(1-frac) + grain[i1]*frac).astype(np.float32)

        grain *= np.hanning(gl).astype(np.float32)
        amp = float(rng.uniform(0.15, 0.7))

        end = min(n, pos + gl)
        L = end - pos
        y[pos:end] += (amp * grain[:L]).astype(np.float32)

        # jitter spacing + occasional gaps
        pos += hop + int(rng.uniform(-0.004, 0.015) * sr)
        if rng.random() < 0.10:
            pos += int(rng.uniform(0.02, 0.12) * sr)

    y = np.tanh(y * 1.35).astype(np.float32) * 0.85
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
