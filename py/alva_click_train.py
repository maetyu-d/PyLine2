import numpy as np


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    y = np.zeros(n, dtype=np.float32)
    rate = 8.0
    spacing = max(1, int(sr / rate))
    click_len = max(1, int(0.002 * sr))

    for i in range(0, n, spacing):
        end = min(n, i + click_len)
        env = np.linspace(1.0, 0.0, end - i, dtype=np.float32)
        y[i:end] += env * 0.9

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
