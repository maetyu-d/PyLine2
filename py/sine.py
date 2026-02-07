import numpy as np

def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)
    freq = 220.0
    t = np.arange(n, dtype=np.float32) / float(sr)
    y = 0.2 * np.sin(2.0 * np.pi * freq * t)
    stereo = np.stack([y, y], axis=1).astype(np.float32)
    return stereo


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
