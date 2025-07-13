from fastapi import FastAPI, Request
from fastapi.staticfiles import StaticFiles
from api import cluster
import uvicorn
import logging
from fastapi.responses import FileResponse, JSONResponse
from datetime import datetime

logger = logging.getLogger(__name__)

app = FastAPI()

app.include_router(cluster.router, prefix="/api/cluster")

app.mount("/static", StaticFiles(directory="static", html=True), name="static")

@app.get("/", tags=["Root"])
async def root():
    """Serve the main HTML page"""
    return FileResponse("static/index.html")
    
@app.get("/favicon.ico", tags=["Root"])
async def favicon():
    return FileResponse("static/favicon.ico")

@app.get("/health", tags=["Health"])
async def health_check():
    """Health check endpoint"""
    logger.debug("Health check endpoint accessed")
    return {"status": "healthy", "timestamp": datetime.now().isoformat()}

# Error handlers
@app.exception_handler(404)
async def not_found_handler(request: Request, exc):
    """Custom 404 error handler"""
    logger.warning(f"404 error for path: {request.url.path}")
    return JSONResponse(
        status_code=404,
        content={"detail": f"Resource not found: {request.url.path}"}
    )

@app.exception_handler(500)
async def internal_error_handler(request: Request, exc):
    """Custom 500 error handler"""
    logger.error(f"500 error for path: {request.url.path}, error: {exc}")
    return JSONResponse(
        status_code=500,
        content={"detail": f"Internal server error: {request.url.path}"}
    )

if __name__ == "__main__":
    logger.info("Starting Portal API with uvicorn")
    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8000,
        reload=True,
        log_level="info"
    )
