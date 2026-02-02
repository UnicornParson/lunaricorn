import uvicorn
import yaml
from lunaricorn.utils.logger_config import setup_logging

def load_config():
    with open("cfg/config.yaml", "r") as f:
        config = yaml.safe_load(f)
    return config

if __name__ == "__main__":
    logger = setup_logging("portal_main")
    logger.info("Starting Portal API with uvicorn")
    try:
        # Import app from app.py
        from app import app
        
        # Load configuration
        config = load_config()
        portal_config = config.get("portal", {})
        host = portal_config.get("host", "0.0.0.0")
        port = portal_config.get("port", 8000)
        logger.info(f"Starting uvicorn server on {host}:{port}")
        uvicorn.run(
            "app:app",
            host=host,
            port=port,
            reload=True,
            log_level="info"
        )
    except KeyboardInterrupt:
        logger.info("Received keyboard interrupt, shutting down...")
    except Exception as e:
        logger.error(f"Unexpected error: {e}")
        raise
