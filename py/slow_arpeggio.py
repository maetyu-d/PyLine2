import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Slow evolving arpeggio (modal, non-poppy).
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    scale = np.array([0, 2, 3, 7, 10])  # pentatonic/minorish
    base_midi = 48
    rng = np.random.default_rng(0)

    step = int(0.4 * sr)
    t = np.arange(n) / sr
    y = np.zeros(n, dtype=np.float32)

    idx = 0
    for i in range(0, n, step):
        note = base_midi + scale[idx % len(scale)] + rng.integers(-12, 13, endpoint=False)//12*12
        freq = 440.0 * 2 ** ((note - 69) / 12)
        L = min(step, n - i)
        tt = np.arange(L) / sr
        env = np.exp(-tt * 2.0)
        y[i:i+L] += 0.3 * np.sin(2*np.pi*freq*tt) * env
        idx += 1

    return np.tanh(y * 1.2).astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
