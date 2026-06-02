import math

def clamp(val, min_val=-128, max_val=127):
    return max(min_val, min(max_val, int(round(val))))

scale = 0.0625

sigmoid = []
silu = []
mish = []

for i in range(256):
    x_int = i - 128
    x = x_int * scale
    
    # Sigmoid
    try:
        sig = 1.0 / (1.0 + math.exp(-x))
    except OverflowError:
        sig = 0.0 if x < 0 else 1.0
        
    sig_out = clamp(sig * 255 - 128)
    sigmoid.append(str(sig_out))
    
    # SiLU
    silu_val = x * sig
    silu_out = clamp(silu_val / scale)
    silu.append(str(silu_out))
    
    # Mish
    try:
        sp = math.log(1.0 + math.exp(x))
        mish_val = x * math.tanh(sp)
    except OverflowError:
        mish_val = x
    mish_out = clamp(mish_val / scale)
    mish.append(str(mish_out))

with open("lut_sigmoid.txt", "w") as f: f.write(", ".join(sigmoid))
with open("lut_silu.txt", "w") as f: f.write(", ".join(silu))
with open("lut_mish.txt", "w") as f: f.write(", ".join(mish))
