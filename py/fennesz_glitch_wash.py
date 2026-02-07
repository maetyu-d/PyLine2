import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(12)
    t = np.arange(n, dtype=np.float32) / float(sr)

    base = np.sin(2.0 * np.pi * 90.0 * t) * 0.15
    noise = rng.uniform(-1.0, 1.0, n).astype(np.float32) * 0.06
    y = base + noise

    burst_count = max(1, int(duration * 10))
    for _ in range(burst_count):
        start = int(rng.integers(0, max(1, n - 1)))
        length = int(rng.integers(int(0.01 * sr), int(0.04 * sr)))
        end = min(n, start + length)
        env = np.linspace(1.0, 0.0, end - start, dtype=np.float32)
        tone = np.sin(2.0 * np.pi * rng.uniform(200.0, 1200.0) * t[: end - start])
        y[start:end] += tone * env * 0.4

    slow = 0.6 + 0.4 * np.sin(2.0 * np.pi * 0.12 * t)
    y *= slow
    y = np.tanh(y * 1.4).astype(np.float32)

    stereo = np.stack([y, y], axis=1)
    return stereo


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
