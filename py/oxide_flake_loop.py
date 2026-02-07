import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Oxide flake loop:
    - simulates tape shedding: high frequencies disappear first
    - random 'flake' dropouts that never recover
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(777)

    loop_len = int(rng.uniform(0.15, 0.45) * sr)
    loop_len = max(512, min(loop_len, n))
    t = np.linspace(0, loop_len/sr, loop_len, endpoint=False, dtype=np.float32)

    loop = (
        0.3*np.sin(2*np.pi*rng.uniform(120, 260)*t) +
        0.2*np.sin(2*np.pi*rng.uniform(400, 900)*t)
    ).astype(np.float32)
    loop *= np.hanning(loop_len).astype(np.float32)

    y = np.zeros(n, dtype=np.float32)
    pos = 0
    generation = 0

    while pos < n:
        gen = loop.copy()

        # spectral dulling
        loss = min(0.02 * generation, 0.8)
        kernel = np.ones(int(1 + loss*40), dtype=np.float32)
        kernel /= kernel.sum()
        gen = np.convolve(gen, kernel, mode="same")

        # irreversible dropouts
        flakes = int(loss * 20)
        for _ in range(flakes):
            a = rng.integers(0, loop_len)
            L = rng.integers(loop_len//16, loop_len//6)
            gen[a:a+L] *= 0.0

        end = min(n, pos + loop_len)
        y[pos:end] += gen[:end-pos]

        pos += loop_len
        generation += 1

    y = np.tanh(y * 1.2).astype(np.float32) * 0.85
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
