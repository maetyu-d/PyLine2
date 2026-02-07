import numpy as np


def _kick(n, sr, freq=48.0, decay=0.16):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    sweep = np.exp(-t * 8.0)
    return np.sin(2.0 * np.pi * (freq * (1.0 + 4.0 * sweep)) * t) * env


def _hat(n, sr, decay=0.02):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    noise = np.random.uniform(-1.0, 1.0, n).astype(np.float32)
    return noise * env * 0.2


def _clap(n, sr, decay=0.06):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    noise = np.random.uniform(-1.0, 1.0, n).astype(np.float32)
    return noise * env * 0.5


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    bpm = 124.0
    beat = 60.0 / bpm
    step = beat / 4.0
    steps = int(duration / step)

    y = np.zeros(n, dtype=np.float32)
    kick_steps = {0, 4, 8, 12}
    hat_steps = {2, 6, 10, 14}
    clap_steps = {4, 12}

    for s in range(steps):
        start = int(s * step * sr)
        if start >= n:
            break
        if s in kick_steps:
            k = _kick(int(0.5 * sr), sr)
            end = min(n, start + len(k))
            y[start:end] += k[: end - start]
        if s in clap_steps:
            c = _clap(int(0.2 * sr), sr)
            end = min(n, start + len(c))
            y[start:end] += c[: end - start]
        if s in hat_steps:
            h = _hat(int(0.08 * sr), sr)
            end = min(n, start + len(h))
            y[start:end] += h[: end - start]

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
