import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Skipping CD surface:
    - fragile sine bed
    - periodic micro-skips, buffer repeats, and hard dropouts
    - very '94–'98 Oval energy
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()
    t = np.linspace(0, duration, n, endpoint=False, dtype=np.float32)

    bed = (
        0.22*np.sin(2*np.pi*float(rng.uniform(120, 260))*t) +
        0.12*np.sin(2*np.pi*float(rng.uniform(400, 900))*t)
    ).astype(np.float32)

    y = bed.copy()

    # CD-style skips
    skips = max(4, int(duration * 6))
    for _ in range(skips):
        p = int(rng.integers(0, n))
        L = int(rng.uniform(0.01, 0.08) * sr)
        L = max(32, min(L, n - p))
        if L <= 0:
            continue

        mode = rng.integers(0, 3)
        if mode == 0:
            # repeat fragment
            frag = y[p:p+L].copy()
            reps = int(rng.integers(2, 6))
            for r in range(reps):
                a = p + r*L
                b = min(n, a + L)
                if b > a:
                    y[a:b] = frag[:b-a]
        elif mode == 1:
            # dropout
            y[p:p+L] *= 0.0
        else:
            # jittered stutter
            step = int(rng.uniform(8, 64))
            frag = y[p:p+L].copy()
            frag = np.repeat(frag[::step], step)[:L]
            y[p:p+L] = frag.astype(np.float32)

    # light saturation
    y = np.tanh(y * 1.2).astype(np.float32) * 0.85

    # gentle fade
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
