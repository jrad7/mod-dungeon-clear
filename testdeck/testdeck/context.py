"""The wiring every router needs, in one place.

Routers are plain modules with module-level `@router.get(...)` functions, so
they cannot take constructor arguments. Rather than reach into
`request.app.state` from every route body, they read this single context
object.

`init()` is called once by the app factory, and again by the test suite with a
temporary config. Nothing here is a global in the "set at import time" sense:
importing testdeck never touches the filesystem.
"""


class Context:
    def __init__(self):
        self.cfg = None
        self.bridge = None       # bridge.Bridge — worldserver command transport
        self.timelines = None    # routes.runs.TimelineStore
        self.throttle = None     # auth.LoginThrottle

    @property
    def ready(self):
        return self.cfg is not None


ctx = Context()


def init(cfg, bridge=None, timelines=None, throttle=None):
    ctx.cfg = cfg
    if bridge is not None:
        ctx.bridge = bridge
    if timelines is not None:
        ctx.timelines = timelines
    if throttle is not None:
        ctx.throttle = throttle
    return ctx
