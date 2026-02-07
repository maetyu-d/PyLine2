import numpy as np

def generate(sr:int, duration:float, context=None):
    """
    Slow choral interval motion (perfects + octaves).
    """
    n = int(sr*duration)
    if n <= 0:
        return np.zeros((0,), dtype=np.float32)

    base_freq = 110.0
    ratios = [1.0, 1.5, 2.0]
    t = np.arange(n) / sr

    y = np.zeros(n, dtype=np.float32)
    for r in ratios:
        y += np.sin(2*np.pi*base_freq*r*t)

    # gentle drift
    drift = 0.002 * np.sin(2*np.pi*0.05*t)
    y *= (1 + drift)

    y /= len(ratios)
    return np.tanh(y * 0.8).astype(np.float32)


if __name__ == "__main__":
    from _render_util import render_cli
    render_cli(generate)
