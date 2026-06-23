"""api/config.py — Central settings using pydantic-settings."""
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    redis_url:        str  = "redis://localhost:6379/0"
    database_url:     str  = "postgresql+asyncpg://quant:quant@localhost:5432/quant"
    data_dir:         str  = "/data"
    allowed_origins:  list[str] = ["http://localhost:3000", "http://localhost:5173"]
    log_level:        str  = "info"

    class Config:
        env_file = ".env"


settings = Settings()
