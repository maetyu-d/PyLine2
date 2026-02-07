import numpy as np

def generate(sr:int, duration:float, context:dict) -> np.ndarray:
    """
    Cellwise degrade loop:
    - generates a tiny internal loop, then repeats it
    - degradation intensity depends on cell_index (later cells = more broken)
    """
    n = int(sr*duration)
    if n<=0: return np.zeros((0,), dtype=np.float32)

    cell_i = int(context.get("cell_index",0))
    cells_total = max(1, int(context.get("cells_total",1)))
    scar = cell_i / max(1, (cells_total-1))

    rng = np.random.default_rng(1337 + cell_i*99991 + int(context.get("track_index",0))*17)

    loop_len = max(128, int(sr * (0.02 + 0.08*(1.0-scar))))
    tL = np.linspace(0, loop_len/sr, loop_len, endpoint=False, dtype=np.float32)
    loop = (0.18*np.sin(2*np.pi*(110+scar*180)*tL) + 0.10*rng.standard_normal(loop_len).astype(np.float32)).astype(np.float32)
    loop *= np.hanning(loop_len).astype(np.float32)

    # apply degradation passes proportional to scar
    passes = int(1 + scar*6)
    gen = loop.copy()
    for _ in range(passes):
        if rng.random() < (0.25 + 0.5*scar):
            bits = int(7 - scar*4)  # fewer bits as scar increases
            bits = max(2, min(8, bits))
            q = float(2**bits - 1)
            gen = np.round(gen*q)/q
        if rng.random() < (0.15 + 0.55*scar):
            a = int(rng.integers(0, len(gen)))
            b = int(min(len(gen), a + rng.integers(len(gen)//10, len(gen)//2)))
            gen[a:b] *= 0.0
        if rng.random() < (0.10 + 0.30*scar):
            gen += float(rng.uniform(-0.01, 0.01))*scar

    y = np.zeros(n, dtype=np.float32)
    pos = 0
    while pos < n:
        L = min(len(gen), n-pos)
        y[pos:pos+L] += gen[:L]
        pos += L

    y = np.tanh(y*1.3).astype(np.float32)*0.8
    f = min(int(0.01*sr), n//2)
    if f>1:
        ramp=np.linspace(0,1,f,endpoint=True).astype(np.float32)
        y[:f]*=ramp; y[-f:]*=ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
