import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    y = np.zeros(n, dtype=np.float32)
    tick_every = max(1, int(0.125 * sr))
    tick_len = max(1, int(0.01 * sr))
    freq = 900.0

    for i in range(0, n, tick_every):
        end = min(n, i + tick_len)
        t = np.arange(end - i, dtype=np.float32) / float(sr)
        env = np.linspace(1.0, 0.0, end - i, dtype=np.float32)
        y[i:end] += np.sin(2.0 * np.pi * freq * t) * env * 0.6

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
