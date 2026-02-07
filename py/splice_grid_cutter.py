import numpy as np

def generate(sr: int, duration: float) -> np.ndarray:
    """
    Splice grid cutter:
    - builds a short source buffer (tone+noise)
    - hard-edits it into micro-slices and reorders them on a grid
    - adds tiny crossfades only sometimes -> audible cuts
    """
    n = int(sr * duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    rng = np.random.default_rng()

    # source: 80..250ms
    src_len = int(rng.uniform(0.08, 0.25) * sr)
    src_len = max(512, min(src_len, max(512, n)))
    t = np.linspace(0, src_len/sr, src_len, endpoint=False, dtype=np.float32)
    src = (
        0.22*np.sin(2*np.pi*float(rng.uniform(90, 420))*t) +
        0.12*np.sin(2*np.pi*float(rng.uniform(500, 2600))*t) +
        0.20*rng.standard_normal(src_len).astype(np.float32)
    ).astype(np.float32)
    src *= np.hanning(src_len).astype(np.float32)

    # slice size: 3..25ms
    sl = int(rng.uniform(0.003, 0.025) * sr)
    sl = max(32, sl)
    slices = []
    for a in range(0, src_len - sl + 1, sl):
        slices.append(src[a:a+sl].copy())
    if not slices:
        slices = [src.copy()]

    # output assembled on a "splice grid"
    y = np.zeros(n, dtype=np.float32)
    pos = 0
    xf = int(rng.uniform(0, 0.004) * sr)  # sometimes no crossfade
    xf = max(0, xf)

    while pos < n:
        frag = slices[int(rng.integers(0, len(slices)))].copy()

        # edit ops
        r = rng.random()
        if r < 0.25:
            frag = frag[::-1].copy()
        elif r < 0.45:
            # decimate (clickier)
            step = int(rng.integers(2, 6))
            frag = np.repeat(frag[::step], step)[:len(frag)].astype(np.float32)
        elif r < 0.60:
            # time-skew via index warp
            L = len(frag)
            warp = float(rng.uniform(0.7, 1.6))
            idx = (np.arange(L, dtype=np.float32) * warp) % L
            i0 = np.floor(idx).astype(np.int32)
            i1 = (i0 + 1) % L
            frac = (idx - i0).astype(np.float32)
            frag = (frag[i0]*(1-frac) + frag[i1]*frac).astype(np.float32)

        # occasional hard gate
        if rng.random() < 0.22:
            g = np.ones(len(frag), dtype=np.float32)
            cut = int(rng.integers(len(frag)//6, len(frag)//2))
            g[:cut] *= 0.0
            frag *= g

        L = len(frag)
        end = min(n, pos + L)
        Lw = end - pos
        if Lw <= 0:
            break

        if xf > 0 and rng.random() < 0.45 and pos > 0:
            # crossfade into current buffer
            f = min(xf, Lw, pos)
            if f > 1:
                a = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
                y[pos-f:pos] = y[pos-f:pos]*(1-a) + frag[:f]*a
                y[pos:pos+Lw] += frag[:Lw]
            else:
                y[pos:pos+Lw] += frag[:Lw]
        else:
            y[pos:pos+Lw] += frag[:Lw]

        # gaps
        gap = int(rng.uniform(0, 0.012) * sr)
        if rng.random() < 0.12:
            gap += int(rng.uniform(0.02, 0.08) * sr)  # bigger discontinuity
        pos += Lw + gap

    y = np.tanh(y * 1.4).astype(np.float32) * 0.85
    # fade
    f = min(int(0.01*sr), n//2)
    if f > 1:
        ramp = np.linspace(0, 1, f, endpoint=True).astype(np.float32)
        y[:f] *= ramp
        y[-f:] *= ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
