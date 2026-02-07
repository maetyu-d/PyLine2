import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    t = np.arange(n, dtype=np.float32) / float(sr)
    slow = 0.5 + 0.5 * np.sin(2.0 * np.pi * 0.03 * t)
    wobble = 1.0 + 0.003 * np.sin(2.0 * np.pi * 0.4 * t)

    tones = (np.sin(2.0 * np.pi * 140.0 * wobble * t) * 0.12 +
             np.sin(2.0 * np.pi * 280.0 * wobble * t) * 0.08 +
             np.sin(2.0 * np.pi * 420.0 * wobble * t) * 0.05)

    hiss = np.random.uniform(-1.0, 1.0, n).astype(np.float32) * 0.03
    y = (tones + hiss) * slow

    fade = np.linspace(0.0, 1.0, max(1, int(0.05 * n)), dtype=np.float32)
    y[: len(fade)] *= fade
    y[-len(fade):] *= fade[::-1]

    y = np.tanh(y * 1.3).astype(np.float32)
    stereo = np.stack([y, y], axis=1)
    return stereo


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
