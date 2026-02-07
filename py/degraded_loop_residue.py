import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Degraded loop residue:
    - tiny loop with tone+noise
    - repeated with cumulative damage: bit loss, dropouts, DC drift
    - produces 'residue' rather than rhythm
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    loop_len = int(rng.uniform(0.05, 0.18) * sr)
    loop_len = max(256, min(loop_len, n))
    tL = np.linspace(0, loop_len/sr, loop_len, endpoint=False, dtype=np.float32)

    loop = (
        0.25*np.sin(2*np.pi*float(rng.uniform(90, 260))*tL) +
        0.15*rng.standard_normal(loop_len).astype(np.float32)
    ).astype(np.float32)
    loop *= np.hanning(loop_len).astype(np.float32)

    y = np.zeros(n, dtype=np.float32)
    pos = 0
    gen = loop.copy()

    while pos < n:
        # cumulative degradation
        if rng.random() < 0.35:
            bits = int(rng.integers(3, 7))
            q = float(2**bits - 1)
            gen = np.round(gen * q) / q
        if rng.random() < 0.25:
            # DC drift
            gen = gen + float(rng.uniform(-0.01, 0.01))
        if rng.random() < 0.22:
            # partial dropout
            a = int(rng.integers(0, len(gen)))
            b = int(min(len(gen), a + rng.integers(len(gen)//8, len(gen)//2)))
            gen[a:b] *= 0.0

        L = len(gen)
        end = min(n, pos + L)
        y[pos:end] += gen[:end-pos]

        pos += L

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
