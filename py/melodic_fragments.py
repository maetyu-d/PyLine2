import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Sparse melodic fragments with silence.
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(1)
    scale = np.array([0, 2, 5, 7, 9])
    base = 55

    y = np.zeros(n, dtype=np.float32)
    events = int(duration * 3) + 1

    for _ in range(events):
        start = rng.integers(0, n)
        L = int(rng.uniform(0.1, 0.4) * sr)
        L = min(L, n - start)
        if L <= 0:
            continue
        note = base + rng.choice(scale)
        freq = 440.0 * 2 ** ((note - 69) / 12)
        t = np.arange(L) / sr
        env = np.hanning(L)
        y[start:start+L] += 0.25 * np.sin(2*np.pi*freq*t) * env

    return np.tanh(y * 1.1).astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
