#!/usr/bin/env python3
# orchestrator.py

import subprocess
import sys
import time
import threading
import signal
import os
from datetime import datetime
import select
import fcntl
import queue

class ProcessMonitor:
    """Monitors and manages a single process"""
    
    def __init__(self, name, cmd, log_prefix="", delay=0):
        self.name = name
        self.cmd = cmd
        self.log_prefix = log_prefix
        self.delay = delay
        self.process = None
        self.running = False
        self.restart_count = 0
        self.max_restarts = 5
        self.restart_delay = 5
        
    def _make_non_blocking(self, file_obj):
        """Make file descriptor non-blocking"""
        fd = file_obj.fileno()
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
        
    def _read_output(self, pipe, output_queue):
        """Read output from pipe and put in queue"""
        try:
            while True:
                chunk = pipe.read()
                if chunk:
                    output_queue.put(chunk)
                else:
                    break
        except (IOError, OSError):
            pass
            
    def start(self):
        """Start the process"""
        if self.delay > 0:
            time.sleep(self.delay)
            
        try:
            self.process = subprocess.Popen(
                self.cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                bufsize=0
            )
            # Make stdout non-blocking
            self._make_non_blocking(self.process.stdout)
            
            self.running = True
            self.log(f"Started process (PID: {self.process.pid})")
            return True
            
        except Exception as e:
            self.log(f"Failed to start process: {e}")
            return False
            
    def stop(self):
        """Stop the process"""
        if self.process and self.process.poll() is None:
            self.log("Stopping process...")
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.log("Process did not terminate gracefully, forcing kill...")
                self.process.kill()
                self.process.wait()
            except Exception as e:
                self.log(f"Error stopping process: {e}")
        self.running = False
        
    def read_output(self):
        if not self.process or not self.running:
            return None

        try:
            output = b""
            while True:
                chunk = self.process.stdout.read(4096)
                if not chunk:
                    break
                output += chunk

            if output:
                return output.decode(errors="replace")
        except (IOError, OSError):
            pass

        return None
        
    def is_alive(self):
        """Check if process is still running"""
        if not self.process:
            return False
            
        return self.process.poll() is None
        
    def restart(self):
        """Restart the process if it died"""
        if not self.is_alive() and self.running:
            self.restart_count += 1
            self.log(f"Process died, restarting ({self.restart_count}/{self.max_restarts})...")
            
            if self.restart_count <= self.max_restarts:
                time.sleep(self.restart_delay)
                self.process = None
                return self.start()
            else:
                self.log(f"Max restart attempts ({self.max_restarts}) reached")
                return False
                
        return True
        
    def log(self, message):
        """Log a message with timestamp and process name"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"{timestamp} [{self.log_prefix}{self.name}] {message}")
        
    def format_output(self, output):
        """Format output from process"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        lines = output.strip().split('\n')
        formatted = []
        for line in lines:
            if line.strip():
                formatted.append(f"{timestamp} [{self.log_prefix}{self.name}] {line}")
        return '\n'.join(formatted)


class ProcessOrchestrator:
    """Orchestrates multiple processes"""
    
    def __init__(self):
        self.processes = []
        self.running = False
        self.output_queue = queue.Queue()
        
    def add_process(self, name, cmd, log_prefix="", delay=0):
        """Add a process to monitor"""
        monitor = ProcessMonitor(name, cmd, log_prefix, delay)
        self.processes.append(monitor)
        return monitor
        
    def setup_processes(self):
        """Set up all processes to monitor"""
        
        # RabbitMQ Server - runs in foreground (not detached)
        rabbitmq_cmd = ["rabbitmq-server"]
        
        # FastAPI Producer
        producer_cmd = [
            "python3", 
            "-m", "uvicorn", 
            "producer:app",
            "--host", "0.0.0.0",
            "--port", "8000",
            "--access-log",
            "--log-level", "info"
        ]
        
        # Consumer
        consumer_cmd = ["python3", "consumer.py"]
        
        # Add processes with appropriate delays
        # RabbitMQ first, then others
        self.add_process("RabbitMQ", rabbitmq_cmd, log_prefix="", delay=0)
        self.add_process("Producer", producer_cmd, log_prefix="", delay=5)  # Wait for RabbitMQ
        self.add_process("Consumer", consumer_cmd, log_prefix="", delay=10)  # Wait a bit more
        
    def signal_handler(self, sig, frame):
        """Handle shutdown signals"""
        self.log("Shutdown signal received, stopping processes...")
        self.stop()
        sys.exit(0)
        
    def start_all(self):
        """Start all processes"""
        self.log("Starting all processes...")
        self.running = True
        
        for process in self.processes:
            if not process.start():
                self.log(f"Failed to start {process.name}, exiting...")
                self.stop()
                return False
                
        self.log("All processes started")
        return True
        
    def stop(self):
        """Stop all processes"""
        self.log("Stopping all processes...")
        self.running = False
        
        # Stop in reverse order
        for process in reversed(self.processes):
            process.stop()
            
        self.log("All processes stopped")
        
    def monitor_processes(self):
        """Monitor all processes and handle output"""
        self.log("Starting process monitoring...")
        
        while self.running:
            all_alive = True
            
            for process in self.processes:
                # Check if process is alive
                if not process.is_alive():
                    all_alive = False
                    if not process.restart():
                        self.log(f"Process {process.name} cannot be restarted, stopping all...")
                        self.stop()
                        break
                
                # Read and display output
                output = process.read_output()
                if output:
                    sys.stdout.write(process.format_output(output))
                    sys.stdout.flush()
            
            # Exit if any critical process cannot be restarted
            if not all_alive:
                for process in self.processes:
                    if process.name == "RabbitMQ" and not process.is_alive():
                        self.log("RabbitMQ is down, cannot continue")
                        self.stop()
                        break
            
            time.sleep(0.1)
            
    def log(self, message):
        """Log orchestrator message"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"{timestamp} [ORCHESTRATOR] {message}")
        
    def print_status(self):
        """Print current status of all processes"""
        self.log("Current process status:")
        for process in self.processes:
            status = "RUNNING" if process.is_alive() else "STOPPED"
            pid = process.process.pid if process.process else "N/A"
            self.log(f"  {process.name}: {status} (PID: {pid}, Restarts: {process.restart_count})")
            
    def run(self):
        """Main run method"""
        # Setup signal handlers
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        # Setup processes
        self.setup_processes()
        
        # Start all processes
        if not self.start_all():
            return
            
        try:
            # Start monitoring in background thread
            monitor_thread = threading.Thread(target=self.monitor_processes, daemon=True)
            monitor_thread.start()
            
            # Print status periodically
            while self.running:
                time.sleep(30)  # Print status every 30 seconds
                if self.running:
                    self.print_status()
                    
        except KeyboardInterrupt:
            self.stop()
        except Exception as e:
            self.log(f"Error in orchestrator: {e}")
            self.stop()


def check_dependencies():
    """Check if required dependencies are available"""
    required = ["rabbitmq-server", "python3", "uvicorn"]
    missing = []
    
    for dep in required:
        try:
            subprocess.run(["which", dep], capture_output=True, check=True)
        except subprocess.CalledProcessError:
            missing.append(dep)
            
    if missing:
        print(f"Missing dependencies: {', '.join(missing)}")
        print("Please install missing dependencies before running the orchestrator.")
        return False
        
    return True


def main():
    """Main entry point"""
    print("=" * 60)
    print("Process Orchestrator")
    print("Starting RabbitMQ, Producer, and Consumer")
    print("=" * 60)
    
    # Check dependencies
    if not check_dependencies():
        sys.exit(1)
    
    # Create and run orchestrator
    orchestrator = ProcessOrchestrator()
    orchestrator.run()


if __name__ == "__main__":
    main()