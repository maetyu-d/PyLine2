import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Spectral dust (fixed edge handling):
    - FFT blocks with sparse drifting spectral mask ('bins that wink')
    - iFFT back + light flutter
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    block = 2048
    hop = 512
    win = np.hanning(block).astype(np.float32)

    # pad so overlap-add never runs off the end
    y = np.zeros(n + block, dtype=np.float32)

    bins = block // 2 + 1
    active = np.zeros(bins, dtype=np.float32)

    stars = rng.integers(low=20, high=bins-20, size=32)
    for b in stars:
        active[b] = float(rng.uniform(0.2, 1.0))

    drift_rate = float(rng.uniform(0.002, 0.02))
    decay = float(rng.uniform(0.90, 0.985))

    frames = int(np.ceil((n + block) / hop)) + 2
    tilt = np.linspace(0.6, 1.2, bins).astype(np.float32)

    for fi in range(frames):
        start = fi * hop
        if start >= n + block:
            break

        active *= decay
        if rng.random() < drift_rate:
            b = int(rng.integers(10, bins-10))
            active[b] = float(rng.uniform(0.4, 1.0))
        if rng.random() < drift_rate:
            base = int(rng.integers(40, min(bins-40, 800)))
            for k in range(1, 5):
                bb = min(bins-1, base * k)
                active[bb] = max(active[bb], float(rng.uniform(0.15, 0.7)))

        x = (rng.standard_normal(block).astype(np.float32) * 0.15) * win

        X = np.fft.rfft(x)
        mask = (active * (active > 0.01)).astype(np.float32)
        X = X * (mask * tilt)
        x2 = np.fft.irfft(X).astype(np.float32)
        x2 *= win

        # ---- FIX: only add the portion that fits the destination slice ----
        end = start + block
        if end > len(y):
            end = len(y)
        L = end - start
        if L <= 0:
            continue
        y[start:end] += x2[:L]

    y = y[:n]

    flutter_hz = float(rng.uniform(7.0, 22.0))
    t = np.linspace(0.0, duration, n, endpoint=False, dtype=np.float32)
    flutter = (0.75 + 0.25*np.sin(2*np.pi*flutter_hz*t + float(rng.uniform(0, 2*np.pi)))).astype(np.float32)
    y *= flutter

    m = float(np.max(np.abs(y))) if n else 0.0
    if m > 1e-9:
        y = (y / m * 0.35).astype(np.float32)

    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
