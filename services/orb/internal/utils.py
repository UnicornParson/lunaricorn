import threading
import time
import inspect

def line_numb():
    """Returns the current line number in our program."""
    # f_back refers to the caller's frame
    return inspect.currentframe().f_back.f_lineno