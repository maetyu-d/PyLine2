import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Comb dust resonator:
    - feed sparse noise impulses into a bank of simple comb delays
    - produces glassy resonant dust / ringing "infrastructure hum"
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    # impulse stream
    x = np.zeros(n, dtype=np.float32)
    hits = max(6, int(duration * 90))
    for _ in range(hits):
        i = int(rng.integers(0, n))
        x[i] += float(rng.uniform(-1.0, 1.0)) * 0.8

    # comb bank
    delays_ms = rng.uniform(6.0, 32.0, size=4).astype(np.float32)
    delays = [max(1, int(float(ms) * 0.001 * sr)) for ms in delays_ms]
    feedbacks = rng.uniform(0.72, 0.93, size=4).astype(np.float32)

    y = np.zeros(n, dtype=np.float32)
    for d, fb in zip(delays, feedbacks):
        buf = np.zeros(d, dtype=np.float32)
        w = 0
        out = np.zeros(n, dtype=np.float32)
        for i in range(n):
            yn = buf[w]
            buf[w] = x[i] + yn * float(fb)
            out[i] = yn
            w += 1
            if w >= d:
                w = 0
        y += out

    # gentle highpass by differentiation
    y = (y - np.concatenate(([0.0], y[:-1]))).astype(np.float32) * 0.8
    y = np.tanh(y * 1.6).astype(np.float32) * 0.7

    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
