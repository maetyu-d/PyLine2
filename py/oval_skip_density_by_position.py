import numpy as np

def generate(sr:int, duration:float, context:dict) -> np.ndarray:
    """
    Skip density by position:
    - fragile dual-sine bed
    - skip/repeat/dropout behaviour changes with cell position (left->right morph)
    """
    n=int(sr*duration)
    if n<=0: return np.zeros((0,), dtype=np.float32)

    cell_i=int(context.get("cell_index",0))
    cells_total=max(1,int(context.get("cells_total",1)))
    p = cell_i / max(1,(cells_total-1))  # 0..1

    rng=np.random.default_rng(2021 + cell_i*4243 + int(context.get("track_index",0))*13)
    t=np.linspace(0,duration,n,endpoint=False,dtype=np.float32)
    f1=140 + 120*p
    f2=430 + 350*p
    y=(0.10*np.sin(2*np.pi*f1*t) + 0.06*np.sin(2*np.pi*f2*t)).astype(np.float32)

    # more damage as p increases
    events=int(2 + 12*p)
    for _ in range(events):
        a=int(rng.integers(0,n))
        L=int(rng.uniform(0.004, 0.06)*sr)
        L=max(16, min(L, n-a))
        if L<=0: continue
        mode = 0 if p<0.33 else (1 if p<0.66 else 2)
        if mode==0:
            # tiny repeats
            frag=y[a:a+L].copy()
            reps=int(rng.integers(2, 4))
            for r in range(reps):
                aa=a+r*L; bb=min(n, aa+L)
                if bb>aa: y[aa:bb]=frag[:bb-aa]
        elif mode==1:
            # stutter/decimate
            step=int(rng.integers(4, 32))
            frag=y[a:a+L].copy()
            frag=np.repeat(frag[::step], step)[:L]
            y[a:a+L]=frag.astype(np.float32)
        else:
            # dropout
            y[a:a+L]*=0.0

    y=np.tanh(y*1.6).astype(np.float32)*0.85
    f=min(int(0.01*sr), n//2)
    if f>1:
        ramp=np.linspace(0,1,f,endpoint=True).astype(np.float32)
        y[:f]*=ramp; y[-f:]*=ramp[::-1]
    return y


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
