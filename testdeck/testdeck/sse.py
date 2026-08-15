"""Server-Sent Events framing. The only SSE consumer left in Test Deck is the
log tail, which streams frames directly — no pub/sub hub needed."""

import json


def sse(event, data):
    """One Server-Sent Events frame."""
    return f"event: {event}\ndata: {json.dumps(data)}\n\n"
