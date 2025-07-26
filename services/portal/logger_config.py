import logging
import logging.handlers
from pathlib import Path
from datetime import datetime

class AutoFlushFileHandler(logging.handlers.RotatingFileHandler):
    def __init__(self, filename):
        super().__init__(filename, maxBytes=100*1024*1024, backupCount=10, encoding='utf-8')
        
    def emit(self, record):
        super().emit(record)
        self.flush()

def setup_logging(logger_name="portal_api"):
    """
    Setup logging configuration for the portal application
    
    Args:
        logger_name (str): Name for the specific logger instance
    
    Returns:
        logging.Logger: Configured logger instance
    """
    logs_dir = Path("/opt/lunaricorn/portal_data/logs")
    logs_dir.mkdir(parents=True, exist_ok=True)
    
    # Backup existing log file if it exists
    log_file = logs_dir / "portal_api.log"
    if log_file.exists():
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_file = logs_dir / f"portal_api_{timestamp}.log"
        try:
            log_file.rename(backup_file)
            print(f"Backed up existing log file to: {backup_file}")
        except Exception as e:
            print(f"Warning: Could not backup existing log file: {e}")
    
    # Create new log file
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    
    # Clear any existing handlers
    for handler in logger.handlers[:]:
        logger.removeHandler(handler)
    
    # Create formatters
    detailed_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(filename)s:%(lineno)d - %(funcName)s - %(message)s')
    simple_formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
    
    # File handler with rotation (100MB max size, keep 10 backup files)
    file_handler = AutoFlushFileHandler(log_file)
    file_handler.setLevel(logging.INFO)
    file_handler.setFormatter(detailed_formatter)
    
    # Console handler
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(simple_formatter)
    
    # Add handlers to root logger
    logger.addHandler(file_handler)
    logger.addHandler(console_handler)
    
    # Create specific logger for this application
    app_logger = logging.getLogger(logger_name)
    app_logger.info("Logging system initialized with file rotation")
    
    return app_logger 