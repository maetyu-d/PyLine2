import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Micropulse diffraction:
    - sparse sub-ms pulses
    - each pulse excites a tiny resonant "diffraction" burst (bandlimited-ish via short window)
    - slight per-pulse micro-detune + stereo-esque beating collapsed to mono
    """
    n = int(sr * duration)
    y = np.zeros(n, dtype=np.float32)
    if n <= 0:
        return y

    rng = np.random.default_rng()

    # pulse density scales with duration, but stays sparse
    pulses = max(6, int(duration * 80))
    base_freqs = np.array([430.0, 611.0, 863.0, 1217.0], dtype=np.float32)

    for _ in range(pulses):
        i0 = rng.integers(0, n)
        # burst length: 0.5ms..6ms
        L = int(rng.uniform(0.0005, 0.006) * sr)
        L = max(8, min(L, n - i0))
        if L <= 8:
            continue

        t = np.linspace(0.0, L / sr, L, endpoint=False, dtype=np.float32)
        w = np.hanning(L).astype(np.float32)

        f = float(rng.choice(base_freqs)) * float(rng.uniform(0.6, 1.6))
        det = float(rng.uniform(-6.0, 6.0))  # Hz
        # micro-diffraction = beating pair + weak noisy component
        burst = (
            np.sin(2*np.pi*(f)*t) +
            0.8*np.sin(2*np.pi*(f+det)*t) +
            0.15*rng.standard_normal(L).astype(np.float32)
        ).astype(np.float32)

        # short decay inside the window too
        decay = np.exp(-t * float(rng.uniform(120.0, 900.0))).astype(np.float32)
        burst = burst * w * decay

        amp = float(rng.uniform(0.15, 0.8))
        y[i0:i0+L] += (amp * burst).astype(np.float32)

    # very light soft clip
    y = np.tanh(y * 1.3).astype(np.float32) * 0.9

    # edge fade
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
