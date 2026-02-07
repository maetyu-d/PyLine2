import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    t = np.arange(n, dtype=np.float32) / float(sr)
    base = 220.0
    pulse_rate = 5.5
    gate = (np.sin(2.0 * np.pi * pulse_rate * t) > 0).astype(np.float32)
    tone = np.sin(2.0 * np.pi * base * t) * 0.4
    tick = np.sign(np.sin(2.0 * np.pi * 60.0 * t)) * 0.1
    y = (tone + tick) * gate

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
