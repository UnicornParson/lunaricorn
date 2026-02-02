import threading
import time
import inspect

def line_numb():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno