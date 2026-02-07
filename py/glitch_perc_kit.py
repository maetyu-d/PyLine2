import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Glitch perc kit:
    - a small set of synthetic "kit" sounds (click, blip, tick, zap, thud)
    - random pattern with micro-timing offsets and occasional stutters
    """
    n = int(sr * duration)
    y = np.zeros(n, dtype=np.float32)
    if n <= 0:
        return y
    rng = np.random.default_rng()

    def add(env, sig, t0):
        L = len(sig)
        a = max(0, t0)
        b = min(n, t0 + L)
        if b > a:
            y[a:b] += (sig[:b-a] * env[:b-a]).astype(np.float32)

    def click(L):
        s = np.zeros(L, dtype=np.float32)
        s[0] = 1.0
        if L > 6:
            s[6] = -0.5
        return s

    def blip(L, f):
        t = np.linspace(0, L/sr, L, endpoint=False, dtype=np.float32)
        return np.sin(2*np.pi*f*t).astype(np.float32)

    def thud(L):
        t = np.linspace(0, L/sr, L, endpoint=False, dtype=np.float32)
        s = np.sin(2*np.pi*float(rng.uniform(50, 110))*t).astype(np.float32)
        s += 0.3*rng.standard_normal(L).astype(np.float32)
        return s.astype(np.float32)

    steps = max(8, int(duration * float(rng.uniform(10, 26))))
    grid = np.linspace(0, n, steps, endpoint=False).astype(np.int32)

    for g in grid:
        if rng.random() < 0.55:
            continue
        jitter = int(rng.uniform(-0.008, 0.008) * sr)
        t0 = int(g + jitter)

        kind = rng.integers(0, 5)
        if kind == 0:
            L = int(0.004*sr)
            sig = click(L)
            env = np.hanning(L).astype(np.float32)
            add(env, sig, t0)
        elif kind == 1:
            L = int(rng.uniform(0.01, 0.03)*sr)
            sig = blip(L, float(rng.uniform(350, 2200)))
            env = np.hanning(L).astype(np.float32)
            add(env, sig, t0)
        elif kind == 2:
            L = int(rng.uniform(0.02, 0.06)*sr)
            sig = thud(L)
            env = np.exp(-np.linspace(0, 1, L, endpoint=False, dtype=np.float32) * float(rng.uniform(3, 10))).astype(np.float32)
            add(env, sig, t0)
        elif kind == 3:
            # zap (fm)
            L = int(rng.uniform(0.01, 0.05)*sr)
            t = np.linspace(0, L/sr, L, endpoint=False, dtype=np.float32)
            f = float(rng.uniform(200, 1800))
            mod = np.sin(2*np.pi*float(rng.uniform(20, 90))*t) * float(rng.uniform(30, 300))
            sig = np.sin(2*np.pi*(f+mod)*t).astype(np.float32)
            env = np.hanning(L).astype(np.float32)
            add(env, sig, t0)
        else:
            # tick (decimated noise)
            L = int(rng.uniform(0.003, 0.02)*sr)
            sig = rng.standard_normal(L).astype(np.float32)
            step = int(rng.integers(2, 10))
            sig = np.repeat(sig[::step], step)[:L].astype(np.float32)
            env = np.hanning(L).astype(np.float32)
            add(env, sig, t0)

        # stutter occasionally
        if rng.random() < 0.10:
            st = int(rng.uniform(0.01, 0.04)*sr)
            st = max(16, st)
            a = max(0, t0)
            b = min(n, a + st)
            if b > a:
                seg = y[a:b].copy()
                for r in range(int(rng.integers(2, 5))):
                    aa = b + r*(b-a)
                    bb = min(n, aa + (b-a))
                    if bb > aa:
                        y[aa:bb] += seg[:bb-aa]

    y = np.tanh(y * 1.5).astype(np.float32) * 0.85
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
