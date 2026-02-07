import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    t = np.arange(n, dtype=np.float32) / float(sr)
    freqs = [110.0, 220.0, 330.0, 440.0, 660.0]
    y = np.zeros(n, dtype=np.float32)
    for i, f in enumerate(freqs):
        detune = 1.0 + (i - 2) * 0.0025
        y += np.sin(2.0 * np.pi * f * detune * t) * (0.18 / (1 + i))

    slow = 0.5 + 0.5 * np.sin(2.0 * np.pi * 0.08 * t)
    noise = np.random.uniform(-1.0, 1.0, n).astype(np.float32) * 0.02
    y = (y * slow) + noise

    fade = np.linspace(0.0, 1.0, max(1, int(0.05 * n)), dtype=np.float32)
    y[: len(fade)] *= fade
    y[-len(fade):] *= fade[::-1]

    y = np.tanh(y * 1.2).astype(np.float32)
    stereo = np.stack([y, y], axis=1)
    return stereo


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
