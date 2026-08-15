"""Live log tailing over SSE.

Only files matching *.log directly inside [paths] log_dir are offered, and the
requested name must be one of them — the log viewer must not become a way to
read arbitrary files off the host.
"""

import asyncio
import os
import time

from fastapi import APIRouter, HTTPException, Request
from fastapi.responses import StreamingResponse

from ..context import ctx
from ..sse import sse

router = APIRouter()

INITIAL_TAIL_BYTES = 131072    # ~128KB is plenty for the last few hundred lines
READ_CHUNK_BYTES = 262144
POLL_S = 0.5


def list_logs():
    out = []
    for p in sorted(ctx.cfg.log_dir.glob("*.log")):
        try:
            st = p.stat()
            out.append({"name": p.name, "size": st.st_size, "mtime": st.st_mtime})
        except OSError:
            pass
    return out


@router.get("/api/logs")
async def api_logs():
    return {"logs": list_logs()}


@router.get("/api/logs/stream")
async def api_log_stream(file: str, request: Request, lines: int = 200):
    if "/" in file or "\\" in file or file not in {l["name"] for l in list_logs()}:
        raise HTTPException(404, "unknown log file")
    path = ctx.cfg.log_dir / file

    async def gen():
        pos = 0
        try:
            with open(path, "rb") as f:
                f.seek(0, os.SEEK_END)
                end = f.tell()
                f.seek(max(0, end - INITIAL_TAIL_BYTES))
                chunk = f.read().decode("utf-8", "replace")
                tail = chunk.splitlines()[-lines:]
                pos = end
            yield sse("lines", tail)
        except OSError as e:
            yield sse("error", str(e))
            return
        beat = time.time()
        while True:
            if await request.is_disconnected():
                return
            try:
                size = path.stat().st_size
            except OSError:
                size = 0
            if size < pos:      # rotated / truncated
                pos = 0
                yield sse("lines", [f"--- {file} rotated ---"])
            if size > pos:
                with open(path, "rb") as f:
                    f.seek(pos)
                    new = f.read(READ_CHUNK_BYTES)
                    pos = f.tell()
                for line in new.decode("utf-8", "replace").splitlines():
                    yield sse("line", line)
                beat = time.time()
            elif time.time() - beat > 15:
                yield ": keepalive\n\n"
                beat = time.time()
            await asyncio.sleep(POLL_S)
    return StreamingResponse(gen(), media_type="text/event-stream",
                             headers={"Cache-Control": "no-cache"})
