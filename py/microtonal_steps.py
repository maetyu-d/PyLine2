import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Microtonal stepped melody (non-12TET).
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(2)
    steps = np.array([0, 1.5, 3.2, 5.1, 7.0])  # in semitone-ish units
    base = 220.0

    step_len = int(0.5 * sr)
    y = np.zeros(n, dtype=np.float32)

    for i in range(0, n, step_len):
        L = min(step_len, n - i)
        step = rng.choice(steps)
        freq = base * 2 ** (step / 12)
        t = np.arange(L) / sr
        env = np.exp(-t * 1.5)
        y[i:i+L] += 0.3 * np.sin(2*np.pi*freq*t) * env

    return np.tanh(y).astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
