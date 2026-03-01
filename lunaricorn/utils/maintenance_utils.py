import string
import time
import os

_BASE62_ALPHABET = string.digits + string.ascii_letters  # 0-9A-Za-z
def base62_encode(num: int) -> str:
    if num == 0:
        return _BASE62_ALPHABET[0]
    base = len(_BASE62_ALPHABET)
    chars = []
    while num:
        num, rem = divmod(num, base)
        chars.append(_BASE62_ALPHABET[rem])
    return ''.join(reversed(chars))


def apptoken() -> str:
    pid = os.getpid()
    ts_ns = time.time_ns()
    return f"t{base62_encode(pid)}_{base62_encode(ts_ns)}"