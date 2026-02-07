import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng(7)
    y = np.zeros(n, dtype=np.float32)
    burst_count = max(1, int(duration * 6.0))
    burst_len = max(1, int(0.015 * sr))

    for _ in range(burst_count):
        start = int(rng.integers(0, max(1, n - burst_len)))
        end = min(n, start + burst_len)
        noise = rng.uniform(-1.0, 1.0, end - start).astype(np.float32)
        env = np.linspace(1.0, 0.0, end - start, dtype=np.float32)
        y[start:end] += noise * env * 0.4

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
