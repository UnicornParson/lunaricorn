from typing import Any

class IDataDumper:
    def dump(self, data: Any):
        pass

class EmptyDataDumper(IDataDumper):
    def dump(self, data: Any):
        pass

class FileDataDumper(IDataDumper):
    def __init__(self, file_path: str):
        self.file_path = file_path

    def dump(self, data: Any):
        if not data:
            print("no data no dump")
            return
        with open(self.file_path, "a", encoding="utf-8") as f:
            f.write(data)
            f.write("\n--------------------------------\n")
            f.flush()

class StdoutDataDumper(IDataDumper):
    def dump(self, data: Any):
        print(f"\n-----------------------\n{data}\n-----------------------\n")

