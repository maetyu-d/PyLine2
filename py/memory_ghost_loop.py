import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Memory ghost loop:
    - harmonic loop that slowly loses identity
    - faint afterimages remain after main material collapses
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(42)

    loop_len = int(rng.uniform(0.25, 0.7) * sr)
    loop_len = max(512, min(loop_len, n))
    t = np.linspace(0, loop_len/sr, loop_len, endpoint=False, dtype=np.float32)

    base_f = rng.uniform(90, 180)
    loop = (
        0.32*np.sin(2*np.pi*base_f*t) +
        0.18*np.sin(2*np.pi*base_f*1.5*t)
    ).astype(np.float32)
    loop *= np.hanning(loop_len).astype(np.float32)

    y = np.zeros(n, dtype=np.float32)
    pos = 0
    generation = 0

    ghost = np.zeros(loop_len, dtype=np.float32)

    while pos < n:
        gen = loop.copy()

        # gradual erasure
        gen *= np.exp(-generation * 0.04)

        # ghost accumulation
        ghost = ghost * 0.98 + gen * 0.02

        # partial collapse
        if generation > 6 and rng.random() < 0.4:
            cut = rng.integers(loop_len//4, loop_len)
            gen[cut:] *= 0.0

        # add ghost back faintly
        gen += ghost * 0.35

        end = min(n, pos + loop_len)
        y[pos:end] += gen[:end-pos]

        pos += loop_len
        generation += 1

    y = np.tanh(y * 1.15).astype(np.float32) * 0.9
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
