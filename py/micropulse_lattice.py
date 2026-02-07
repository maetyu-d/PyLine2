import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Micropulse lattice:
    - quasi-regular micro-impulses on a jittered lattice
    - each impulse excites a short modal ring (a few partials)
    - light probability dropouts -> "digital stutter"
    """
    n = int(sr * duration)
    y = np.zeros(n, dtype=np.float32)
    if n <= 0:
        return y

    rng = np.random.default_rng()
    # lattice spacing: 1..12 ms
    step = int(rng.uniform(0.001, 0.012) * sr)
    step = max(8, step)

    # modal set
    modes = np.array([310, 521, 811, 1237, 1973], dtype=np.float32) * float(rng.uniform(0.7, 1.4))
    # ring length 2..18 ms
    L = int(rng.uniform(0.002, 0.018) * sr)
    L = max(32, L)
    t = np.linspace(0.0, L/sr, L, endpoint=False, dtype=np.float32)
    win = np.hanning(L).astype(np.float32)
    dec = np.exp(-t * float(rng.uniform(120, 650))).astype(np.float32)

    pos = 0
    while pos < n:
        if rng.random() < 0.08:
            pos += step
            continue

        j = int(rng.integers(-step//4, step//4))
        i0 = pos + j
        if i0 < 0 or i0 >= n:
            pos += step
            continue

        # build ring
        ring = np.zeros(L, dtype=np.float32)
        for f in rng.choice(modes, size=int(rng.integers(2, 5)), replace=False):
            ring += np.sin(2*np.pi*float(f)*t).astype(np.float32)
        ring /= max(1.0, float(np.max(np.abs(ring))))
        ring = ring * win * dec

        amp = float(rng.uniform(0.15, 0.85))
        end = min(n, i0 + L)
        y[i0:end] += (amp * ring[:end-i0]).astype(np.float32)

        pos += step

    y = np.tanh(y * 1.25).astype(np.float32) * 0.9

    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
