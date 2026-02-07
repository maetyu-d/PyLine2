import numpy as np


def _kick(n, sr, freq=58.0, decay=0.13):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    sweep = np.exp(-t * 7.0)
    return np.sin(2.0 * np.pi * (freq * (1.0 + 3.5 * sweep)) * t) * env


def _snare(n, sr, decay=0.12):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    noise = np.random.uniform(-1.0, 1.0, n).astype(np.float32)
    tone = np.sin(2.0 * np.pi * 190.0 * t) * 0.2
    return (noise * 0.8 + tone) * env


def _hat(n, sr, decay=0.035):
    t = np.arange(n, dtype=np.float32) / float(sr)
    env = np.exp(-t / decay)
    noise = np.random.uniform(-1.0, 1.0, n).astype(np.float32)
    return noise * env * 0.25


def generate(sr: int, duration: float, context=None):
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    bpm = 92.0
    beat = 60.0 / bpm
    step = beat / 4.0
    steps = int(duration / step)

    y = np.zeros(n, dtype=np.float32)
    kick_steps = {0, 7, 10}
    snare_steps = {4, 12}
    hat_steps = {1, 3, 5, 7, 9, 11, 13, 15}

    for s in range(steps):
        start = int(s * step * sr)
        if start >= n:
            break
        if s in kick_steps:
            k = _kick(int(0.6 * sr), sr)
            end = min(n, start + len(k))
            y[start:end] += k[: end - start]
        if s in snare_steps:
            sn = _snare(int(0.3 * sr), sr)
            end = min(n, start + len(sn))
            y[start:end] += sn[: end - start]
        if s in hat_steps:
            h = _hat(int(0.15 * sr), sr)
            end = min(n, start + len(h))
            y[start:end] += h[: end - start]

    y = np.clip(y, -1.0, 1.0)
    stereo = np.stack([y, y], axis=1)
    return stereo.astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
