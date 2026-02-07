import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Spectral freeze grains:
    - make a short noisy buffer
    - pick random frames, freeze their magnitude spectra (keep random phases)
    - overlap-add tiny frozen grains => icy, static shimmer
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    src_len = int(rng.uniform(0.2, 0.6) * sr)
    src_len = max(2048, src_len)
    src = rng.standard_normal(src_len).astype(np.float32) * 0.25
    # add a faint tone residue
    t = np.linspace(0, src_len/sr, src_len, endpoint=False, dtype=np.float32)
    src += (0.05*np.sin(2*np.pi*float(rng.uniform(90, 220))*t)).astype(np.float32)

    block = 1024
    hop = 256
    win = np.hanning(block).astype(np.float32)
    bins = block//2 + 1

    y = np.zeros(n + block, dtype=np.float32)

    # choose a handful of "frozen" frames
    frames = max(8, int(rng.uniform(12, 40)))
    mags = []
    for _ in range(frames):
        a = int(rng.integers(0, src_len - block))
        x = src[a:a+block] * win
        X = np.fft.rfft(x)
        mag = np.abs(X).astype(np.float32)
        # emphasize highs slightly
        tilt = np.linspace(0.7, 1.2, bins).astype(np.float32)
        mags.append(mag * tilt)

    pos = 0
    while pos < n:
        mag = mags[int(rng.integers(0, len(mags)))]
        phase = rng.uniform(0, 2*np.pi, size=bins).astype(np.float32)
        X = (mag * (np.cos(phase) + 1j*np.sin(phase))).astype(np.complex64)
        x2 = np.fft.irfft(X).astype(np.float32)
        x2 *= win

        end = min(len(y), pos + block)
        L = end - pos
        y[pos:end] += x2[:L]
        pos += hop + int(rng.integers(0, hop))  # jittered spacing

    y = y[:n]
    # light normalization
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
