from dataclasses import dataclass
import random

class NetConfig:
    default = None
    def __init__(self):
        self.rss_loader_depth_limit = 10
        self.wait_after_load = 2.0

    def get_random_wait_after_load(self) -> float: 
        spread = self.wait_after_load * 0.6
        return random.uniform(self.wait_after_load - spread, self.wait_after_load + spread)
NetConfig.default = NetConfig()