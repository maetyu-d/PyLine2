import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Glitch tone fragments:
    - fragile tonal fragments appear and vanish
    - heavy silence, broken continuity
    """
    n = int(sr * duration)
    y = np.zeros(n, dtype=np.float32)
    if n <= 0:
        return y

    rng = np.random.default_rng()
    t = np.linspace(0, duration, n, endpoint=False, dtype=np.float32)

    fragments = max(4, int(duration * 8))
    for _ in range(fragments):
        a = int(rng.integers(0, n))
        L = int(rng.uniform(0.04, 0.25) * sr)
        L = min(L, n - a)
        if L <= 0:
            continue
        f = float(rng.uniform(110, 440))
        frag = np.sin(2*np.pi*f*t[:L]).astype(np.float32)
        frag *= np.hanning(L).astype(np.float32)
        y[a:a+L] += 0.25 * frag

        # erase part of it
        if rng.random() < 0.45:
            cut = int(rng.uniform(L*0.2, L*0.7))
            y[a+cut:a+L] *= 0.0

    y = np.tanh(y * 1.15).astype(np.float32) * 0.85

    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
