import numpy as np

def generate(sr:int, duration:float, context:dict) -> np.ndarray:
    """
    Fragile marker-bed:
    - very low sine residue
    - sparse microclicks whose density depends on cell_index (later cells = more scarred)
    - optional marker clicks near sync points (projected into pattern time)
    """
    n = int(sr*duration)
    if n<=0: return np.zeros((0,), dtype=np.float32)
    rng = np.random.default_rng(context.get("track_index",0)*10007 + context.get("cell_index",0)*997)

    cell_i = int(context.get("cell_index",0))
    cells_total = max(1, int(context.get("cells_total",1)))
    scar = cell_i / max(1, (cells_total-1))

    t = np.linspace(0, duration, n, endpoint=False, dtype=np.float32)
    base_f = 120.0 + 220.0*scar
    y = (0.02*np.sin(2*np.pi*base_f*t) + 0.01*np.sin(2*np.pi*(base_f*2.01)*t)).astype(np.float32)

    # microclicks
    clicks = int(6 + scar*28)
    for _ in range(clicks):
        p = int(rng.integers(0, n))
        y[p] += float(rng.uniform(0.2, 0.9))
        if p+6 < n: y[p+6] -= float(rng.uniform(0.05, 0.35))

    # project master sync points into pattern time (approx)
    syncs = context.get("track_sync_points_master", []) or []
    pat_dur = float(context.get("track_pattern_duration", duration))
    off = float(context.get("track_offset", 0.0))
    cell_start = float(context.get("cell_start", 0.0))
    for sp in syncs:
        try:
            sp = float(sp)
        except Exception:
            continue
        # relative time within pattern cycle
        rel = (sp - off) % max(1e-6, pat_dur)
        # if sync lands within this cell, place a marker click near that relative time
        if cell_start <= rel < (cell_start + duration):
            local = rel - cell_start
            p = int(local * sr)
            if 0 <= p < n:
                y[p] += 0.9
                if p+3 < n: y[p+3] -= 0.4

    # soften
    y = np.tanh(y*1.4).astype(np.float32)*0.7
    # edge fade
    f = min(int(0.01*sr), n//2)
    if f>1:
        ramp=np.linspace(0,1,f,endpoint=True).astype(np.float32)
        y[:f]*=ramp; y[-f:]*=ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
