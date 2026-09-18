# A test convenience can move coverage off the shipped branch

*2026-09-18, lane L-API Wave 2, Task I.*

`ApiServer` binds one of two ways, and the choice is a branch in the
constructor:

```cpp
boundPort = (settings.port == 0) ? svr.bind_to_any_port(settings.bindAddress)
          : (svr.bind_to_port(settings.bindAddress, settings.port) ? settings.port : -1);
```

The shipped configuration is a fixed port (4736), so **production always takes
the second branch**. Every one of the twelve over-the-wire cases set
`settings.port = 0` and took the first.

That was not sloppiness — it was the right call, made for stated reasons the
plan spells out. A literal 4736 collides with a developer's own running
instance, collides between neighbouring cases in the same file, and makes the
shutdown/port-reuse loop unrunnable. An ephemeral bind also removes the startup
race, because `bind_to_any_port` returns the port on the constructing thread
and nothing has to sleep or poll to learn it.

**Each of those reasons is about the test. None is about the code under test.**
The result was a suite of twelve socket cases in which the line the customer
runs was executed zero times, and `boundPort()` — an accessor that exists only
because httplib's `bind_to_port` returns `bool` and discards the port — was
never once read back from that branch.

## What it costs to notice, and what it costs not to

Noticing is one test. Bind ephemerally to learn a port that is free right now,
read it back, destroy that server, then bind **that number** the fixed way:

```cpp
const int port = aPortThatWasFree();          // bind 0, read, release
ApiSettings settings; settings.port = port;   // NOT zero: the shipped branch
const ApiServer server(source, settings);
REQUIRE(server.boundPort() == port);
```

There is a window between the release and the re-bind. It is unavoidable for
any test of a fixed-port bind, and it is narrower than picking a number and
hoping — which is what the alternative amounts to.

Not noticing costs a defect class nothing in the suite can reach: a wrong
return-value convention on the `bool` branch, a `boundPort` left at `-1` after
a successful bind, or a `Host` allowlist comparing the wrong port. That last
one is real — the same task found the plan's `hostIsAllowed(..., settings.port)`
refuses every request under an ephemeral bind, so the two branches disagree
about what "the port" means, and only one of them was being asked.

## The question to ask, and when

**Ask it when a test picks a value to make the test easy: does the value put
the test on the same branch the product takes?** Ephemeral versus fixed ports,
in-memory versus on-disk, a stub clock versus the real one, a synthetic device
versus a real one. Every one of those is a legitimate test convenience, and
every one of them can silently relocate the coverage.

This is a sibling of
`a-fixed-defect-returns-through-the-silent-fallback.md`, from the other
direction. There, the *code* had a branch where the fix did not run. Here the
*code* is fine and the **test** chose the branch, so no fixture was short
enough to notice and no mutation of the production path would have gone red:
mutate `bind_to_port`'s handling and all twelve cases stay green, because none
of them calls it.

The cheapest form of the check is a grep, and it is worth running once per
suite: if a setting has two branches, does any test set it the way the shipped
default does?
