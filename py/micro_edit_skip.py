import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Micro edit skip:
    - generates a busy percussive texture
    - introduces "DAW slip edits": tiny repeats, forward jumps, reverse blips
    - produces classic clicks'n'cuts temporal discontinuity
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()
    y = np.zeros(n, dtype=np.float32)

    # base: impulse + filtered noise burstlets
    hits = max(12, int(duration * 140))
    for _ in range(hits):
        i = int(rng.integers(0, n))
        L = int(rng.uniform(0.001, 0.01) * sr)
        L = max(16, min(L, n - i))
        if L <= 0:
            continue
        t = np.linspace(0, L/sr, L, endpoint=False, dtype=np.float32)
        env = np.exp(-t * float(rng.uniform(250, 1200))).astype(np.float32)
        burst = (rng.standard_normal(L).astype(np.float32) * 0.4)
        # crude "filter": cumulative sum highpass-ish
        burst = burst - np.mean(burst)
        burst = np.cumsum(burst).astype(np.float32)
        burst = burst / max(1e-9, float(np.max(np.abs(burst))))
        y[i:i+L] += (burst * env * float(rng.uniform(0.2, 1.0))).astype(np.float32)

    # slip edits: operate by reading from y into z with a moving read head
    z = np.zeros(n, dtype=np.float32)
    read = 0
    write = 0
    while write < n:
        if rng.random() < 0.08:
            # tiny repeat
            rep = int(rng.uniform(0.005, 0.03) * sr)
            rep = max(8, rep)
            for _ in range(int(rng.integers(2, 6))):
                end = min(n, write + rep)
                z[write:end] = y[read:read+(end-write)]
                write = end
                if write >= n:
                    break
        elif rng.random() < 0.04:
            # reverse blip
            bl = int(rng.uniform(0.004, 0.02) * sr)
            bl = max(8, bl)
            end = min(n, write + bl)
            seg = y[max(0, read-bl):read].copy()
            seg = seg[::-1]
            z[write:end] = seg[:end-write]
            write = end
        elif rng.random() < 0.06:
            # forward jump (skip)
            read += int(rng.uniform(0.01, 0.08) * sr)

        # normal copy
        chunk = int(rng.uniform(0.005, 0.04) * sr)
        chunk = max(16, chunk)
        end = min(n, write + chunk)
        # wrap read
        if read + (end-write) >= n:
            read = 0
        z[write:end] = y[read:read+(end-write)]
        read += (end - write)
        write = end

    # add occasional hard mutes (buffer underruns)
    for _ in range(max(1, int(duration * 6))):
        a = int(rng.integers(0, n))
        L = int(rng.uniform(0.003, 0.03) * sr)
        z[a:a+L] *= 0.0

    z = np.tanh(z * 1.6).astype(np.float32) * 0.85
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        z[:f] *= ramp
        z[-f:] *= ramp[::-1]
    return z


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
