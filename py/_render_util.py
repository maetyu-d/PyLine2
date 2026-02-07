import sys
import wave
import struct
import inspect
import numpy as np


def _normalize_audio(y: np.ndarray) -> np.ndarray:
    if y is None:
        return np.zeros((0,), dtype=np.float32)
    y = np.asarray(y, dtype=np.float32)
    if y.ndim == 2:
        # Accept shape (channels, samples) or (samples, channels)
        if y.shape[0] in (1, 2) and y.shape[1] > 2:
            y = y.T
    if y.ndim == 1:
        return y
    if y.ndim == 2:
        return y
    return y.reshape(-1)


def _to_int16(y: np.ndarray) -> np.ndarray:
    y = np.clip(y, -1.0, 1.0)
    return (y * 32767.0).astype(np.int16)


def write_wav(path: str, sr: int, y: np.ndarray):
    y = _normalize_audio(y)
    if y.ndim == 1:
        channels = 1
        frames = y
    else:
        channels = y.shape[1]
        frames = y

    with wave.open(path, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(int(sr))

        if channels == 1:
            data = _to_int16(frames)
            wf.writeframes(data.tobytes())
        else:
            data = _to_int16(frames)
            wf.writeframes(data.tobytes())


def render_cli(generate_fn):
    if len(sys.argv) < 4:
        print("Usage: python synth.py output.wav sample_rate duration_seconds")
        sys.exit(1)

    out_path = sys.argv[1]
    sr = int(float(sys.argv[2]))
    duration = float(sys.argv[3])

    sig = inspect.signature(generate_fn)
    try:
        if len(sig.parameters) >= 3:
            y = generate_fn(sr, duration, {})
        else:
            y = generate_fn(sr, duration)
    except TypeError:
        y = generate_fn(sr, duration)

    write_wav(out_path, sr, y)


if __name__ == "__main__":
    print("This module provides render_cli().")
